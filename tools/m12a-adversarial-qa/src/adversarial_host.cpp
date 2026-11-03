#include "adversarial_host.h"
#include <iostream>
#include <sstream>
#include <thread>

namespace pse::qa {

const char* faultModeToString(FaultMode mode) {
    switch (mode) {
        case FaultMode::NONE:                    return "NONE (Conformant Validation)";
        case FaultMode::MALFORMED_WELCOME_SLOT:  return "MALFORMED_WELCOME_SLOT (slot >= 8)";
        case FaultMode::MALFORMED_WELCOME_GEN:   return "MALFORMED_WELCOME_GEN (gen = 0)";
        case FaultMode::PREMATURE_DISCONNECT:    return "PREMATURE_DISCONNECT (Drop mid-stream)";
        case FaultMode::STALE_RECONNECT_GEN:     return "STALE_RECONNECT_GEN (gen not incremented)";
        case FaultMode::FORCE_RECONNECT_REJECT:  return "FORCE_RECONNECT_REJECT (Host rejects reconnect)";
        case FaultMode::HANDSHAKE_TIMEOUT:       return "HANDSHAKE_TIMEOUT (Host unresponsive)";
        case FaultMode::STALE_DUPLICATE_PACKETS: return "STALE_DUPLICATE_PACKETS (Snapshot fuzzing)";
        case FaultMode::ACCEPT_FORGED_KEY:       return "ACCEPT_FORGED_KEY (Host bug: false acceptance)";
        case FaultMode::DROP_UNRELIABLE_INPUT:   return "DROP_UNRELIABLE_INPUT (Simulated packet loss)";
        default:                                 return "UNKNOWN";
    }
}

AdversarialHost::AdversarialHost() = default;

AdversarialHost::~AdversarialHost() {
    stop();
}

bool AdversarialHost::start(uint16_t port, const FaultConfig& config) {
    stop();

    if (enet_initialize() != 0) {
        std::cerr << "[AdvHost] Failed to initialize ENet" << std::endl;
        return false;
    }

    m_config = config;
    m_port = port;
    m_generation = 1;
    m_assignedSlot = 1; // P2
    m_lastForward = 0.0f;
    m_hadForward = false;
    m_snapshotSeq = 0;

    {
        LockGuard lock(m_telemetryMutex);
        m_telemetry = HostTelemetry{};
    }

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    m_enetHost = enet_host_create(&address, 8, pse::CHANNEL_COUNT, 0, 0);
    if (!m_enetHost) {
        std::cerr << "[AdvHost] Failed to bind to port " << port << std::endl;
        return false;
    }

    m_running.store(true);
    logEvent("Host started on port " + std::to_string(port) + " with fault mode: " + faultModeToString(config.mode));
    return true;
}

void AdversarialHost::stop() {
    if (m_running.exchange(false)) {
        if (m_enetHost) {
            enet_host_flush(m_enetHost);
            enet_host_destroy(m_enetHost);
            m_enetHost = nullptr;
        }
        logEvent("Host stopped.");
    }
}

void AdversarialHost::poll(uint32_t timeoutMs) {
    if (!m_enetHost) return;

    ENetEvent event;
    while (m_running.load() && enet_host_service(m_enetHost, &event, timeoutMs) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                LockGuard lock(m_telemetryMutex);
                m_telemetry.connectsReceived++;
                logEvent("Peer connected from address: " + std::to_string(event.peer->address.host));
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE:
                handleReceive(event.peer, event.packet, event.channelID);
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT: {
                LockGuard lock(m_telemetryMutex);
                m_telemetry.disconnectsReceived++;
                logEvent("Peer disconnected");
                break;
            }
            case ENET_EVENT_TYPE_NONE:
            default:
                break;
        }
    }
}

void AdversarialHost::handleReceive(ENetPeer* peer, ENetPacket* packet, uint8_t /*channel*/) {
    if (!packet || packet->dataLength < pse::MsgHeader::SERIALIZED_SIZE) return;

    pse::MsgHeader header;
    pse::ByteReader reader(packet->data, packet->dataLength);
    if (!header.deserialize(reader)) return;
    if (header.magic != pse::NET_MAGIC) return;

    switch (header.msgType) {
        case pse::MSG_HELLO:
            handleHello(peer, reader, header.sequence);
            break;
        case pse::MSG_INPUT_COMMAND:
            handleInputCommand(peer, reader, header.sequence);
            break;
        case pse::MSG_PLAYER_LEAVE:
            logEvent("Received MSG_PLAYER_LEAVE");
            break;
        default:
            break;
    }
}

void AdversarialHost::handleHello(ENetPeer* peer, pse::ByteReader& reader, uint32_t /*seq*/) {
    pse::MsgHello hello;
    if (!hello.deserialize(reader)) return;

    {
        LockGuard lock(m_telemetryMutex);
        m_telemetry.hellosReceived++;
    }
    logEvent("Received MSG_HELLO: partId=" + hello.participantId + " key=0x" + std::to_string(hello.reconnectKey));

    // Fault 5: Handshake Timeout (deliberately drop hello, do not respond)
    if (m_config.mode == FaultMode::HANDSHAKE_TIMEOUT) {
        logEvent("FAULT INJECTED: HANDSHAKE_TIMEOUT -> Dropping MSG_HELLO without response");
        return;
    }

    // Fault 1A: Malformed Welcome Slot
    if (m_config.mode == FaultMode::MALFORMED_WELCOME_SLOT) {
        pse::MsgWelcome welcome;
        welcome.protocolVersion = pse::NET_PROTOCOL_VERSION;
        welcome.sessionId = m_sessionId;
        welcome.assignedSlot = m_config.malformedSlot; // 0xFF
        welcome.connectionGeneration = 1;
        welcome.mapEpoch = 1;
        welcome.serverTickRate = 60;
        welcome.serverSnapshotRate = 30;
        welcome.mapName = "KARNAK_SANCTUARY";
        welcome.reconnectKey = m_config.validReconnectKey;

        auto encoded = pse::encodePacket(pse::MSG_WELCOME, ++m_snapshotSeq, welcome);
        sendPacket(peer, pse::CHANNEL_RELIABLE_CONTROL, encoded, true);
        {
            LockGuard lock(m_telemetryMutex);
            m_telemetry.welcomesSent++;
        }
        logEvent("FAULT INJECTED: Sent MALFORMED_WELCOME_SLOT (slot=" + std::to_string(welcome.assignedSlot) + ")");
        return;
    }

    // Fault 1B: Malformed Welcome Generation
    if (m_config.mode == FaultMode::MALFORMED_WELCOME_GEN) {
        pse::MsgWelcome welcome;
        welcome.protocolVersion = pse::NET_PROTOCOL_VERSION;
        welcome.sessionId = m_sessionId;
        welcome.assignedSlot = 1;
        welcome.connectionGeneration = m_config.malformedGen; // 0
        welcome.mapEpoch = 1;
        welcome.serverTickRate = 60;
        welcome.serverSnapshotRate = 30;
        welcome.mapName = "KARNAK_SANCTUARY";
        welcome.reconnectKey = m_config.validReconnectKey;

        auto encoded = pse::encodePacket(pse::MSG_WELCOME, ++m_snapshotSeq, welcome);
        sendPacket(peer, pse::CHANNEL_RELIABLE_CONTROL, encoded, true);
        {
            LockGuard lock(m_telemetryMutex);
            m_telemetry.welcomesSent++;
        }
        logEvent("FAULT INJECTED: Sent MALFORMED_WELCOME_GEN (gen=" + std::to_string(welcome.connectionGeneration) + ")");
        return;
    }

    // Fault 3: Stale Reconnect Generation
    if (m_config.mode == FaultMode::STALE_RECONNECT_GEN && hello.reconnectKey != 0) {
        pse::MsgWelcome welcome;
        welcome.protocolVersion = pse::NET_PROTOCOL_VERSION;
        welcome.sessionId = m_sessionId;
        welcome.assignedSlot = m_assignedSlot;
        welcome.connectionGeneration = 1; // STALE: gen 1 instead of 2!
        welcome.mapEpoch = 1;
        welcome.serverTickRate = 60;
        welcome.serverSnapshotRate = 30;
        welcome.mapName = "KARNAK_SANCTUARY";
        welcome.reconnectKey = m_config.validReconnectKey;

        auto encoded = pse::encodePacket(pse::MSG_WELCOME, ++m_snapshotSeq, welcome);
        sendPacket(peer, pse::CHANNEL_RELIABLE_CONTROL, encoded, true);
        {
            LockGuard lock(m_telemetryMutex);
            m_telemetry.welcomesSent++;
        }
        logEvent("FAULT INJECTED: Sent STALE_RECONNECT_GEN (gen=1 retained after reconnect)");
        return;
    }

    // Fault: Force Reconnect Reject
    if (m_config.mode == FaultMode::FORCE_RECONNECT_REJECT && hello.reconnectKey != 0) {
        pse::MsgReject reject;
        reject.reasonCode = pse::REJECT_HIJACK_ATTEMPT;
        reject.reasonMessage = "Adversarial forced rejection";

        auto encoded = pse::encodePacket(pse::MSG_REJECT, ++m_snapshotSeq, reject);
        sendPacket(peer, pse::CHANNEL_RELIABLE_CONTROL, encoded, true);
        {
            LockGuard lock(m_telemetryMutex);
            m_telemetry.rejectsSent++;
        }
        logEvent("FAULT INJECTED: Sent FORCE_RECONNECT_REJECT (code=8)");
        return;
    }

    // Fault: Host Buggy Acceptance of Forged Key
    if (m_config.mode == FaultMode::ACCEPT_FORGED_KEY && hello.reconnectKey == 0xDEADBEEFCAFEULL) {
        pse::MsgWelcome welcome;
        welcome.protocolVersion = pse::NET_PROTOCOL_VERSION;
        welcome.sessionId = m_sessionId;
        welcome.assignedSlot = m_assignedSlot;
        welcome.connectionGeneration = 2;
        welcome.mapEpoch = 1;
        welcome.serverTickRate = 60;
        welcome.serverSnapshotRate = 30;
        welcome.mapName = "KARNAK_SANCTUARY";
        welcome.reconnectKey = hello.reconnectKey;

        auto encoded = pse::encodePacket(pse::MSG_WELCOME, ++m_snapshotSeq, welcome);
        sendPacket(peer, pse::CHANNEL_RELIABLE_CONTROL, encoded, true);
        {
            LockGuard lock(m_telemetryMutex);
            m_telemetry.welcomesSent++;
        }
        logEvent("FAULT INJECTED: Host erroneously ACCEPTED forged key 0xDEADBEEFCAFEULL!");
        return;
    }

    // Standard / Reconnect check
    if (hello.reconnectKey != 0) {
        if (hello.reconnectKey == m_config.validReconnectKey) {
            // Authorized reconnect!
            m_generation++;
            pse::MsgWelcome welcome;
            welcome.protocolVersion = pse::NET_PROTOCOL_VERSION;
            welcome.sessionId = m_sessionId;
            welcome.assignedSlot = m_assignedSlot;
            welcome.connectionGeneration = m_generation;
            welcome.mapEpoch = 1;
            welcome.serverTickRate = 60;
            welcome.serverSnapshotRate = 30;
            welcome.mapName = "KARNAK_SANCTUARY";
            welcome.reconnectKey = m_config.validReconnectKey;

            auto encoded = pse::encodePacket(pse::MSG_WELCOME, ++m_snapshotSeq, welcome);
            sendPacket(peer, pse::CHANNEL_RELIABLE_CONTROL, encoded, true);
            {
                LockGuard lock(m_telemetryMutex);
                m_telemetry.welcomesSent++;
            }
            logEvent("Authorized reconnect accepted: slot=P" + std::to_string(m_assignedSlot + 1) + " gen=" + std::to_string(m_generation));
        } else {
            // Invalid key / hijack attempt
            pse::MsgReject reject;
            reject.reasonCode = pse::REJECT_HIJACK_ATTEMPT;
            reject.reasonMessage = "Session Hijack Attempt / Invalid Reconnect Key";

            auto encoded = pse::encodePacket(pse::MSG_REJECT, ++m_snapshotSeq, reject);
            sendPacket(peer, pse::CHANNEL_RELIABLE_CONTROL, encoded, true);
            {
                LockGuard lock(m_telemetryMutex);
                m_telemetry.rejectsSent++;
            }
            logEvent("Forged reconnectKey rejected: code 8");
        }
        return;
    }

    // Fresh session
    pse::MsgWelcome welcome;
    welcome.protocolVersion = pse::NET_PROTOCOL_VERSION;
    welcome.sessionId = m_sessionId;
    welcome.assignedSlot = m_assignedSlot;
    welcome.connectionGeneration = 1;
    welcome.mapEpoch = 1;
    welcome.serverTickRate = 60;
    welcome.serverSnapshotRate = 30;
    welcome.mapName = "KARNAK_SANCTUARY";
    welcome.reconnectKey = m_config.validReconnectKey;

    auto encoded = pse::encodePacket(pse::MSG_WELCOME, ++m_snapshotSeq, welcome);
    sendPacket(peer, pse::CHANNEL_RELIABLE_CONTROL, encoded, true);
    {
        LockGuard lock(m_telemetryMutex);
        m_telemetry.welcomesSent++;
    }
    logEvent("Fresh session welcomed: slot=P" + std::to_string(m_assignedSlot + 1) + " gen=1");

    // Fault 6: Stale / Duplicate Packets injection right after welcome
    if (m_config.mode == FaultMode::STALE_DUPLICATE_PACKETS) {
        logEvent("FAULT INJECTED: Sending duplicate & stale snapshots to client");
        pse::MsgPlayerSnapshot snap1;
        snap1.snapshotSequence = 100;
        snap1.serverTick = 500;
        snap1.mapEpoch = 1;
        auto snapEnc = pse::encodePacket(pse::MSG_PLAYER_SNAPSHOT, 100, snap1);

        sendPacket(peer, pse::CHANNEL_UNRELIABLE_SEQUENCED, snapEnc, false);
        sendPacket(peer, pse::CHANNEL_UNRELIABLE_SEQUENCED, snapEnc, false); // duplicate!

        pse::MsgPlayerSnapshot snapOld;
        snapOld.snapshotSequence = 50; // stale!
        snapOld.serverTick = 450;
        snapOld.mapEpoch = 1;
        auto snapOldEnc = pse::encodePacket(pse::MSG_PLAYER_SNAPSHOT, 50, snapOld);
        sendPacket(peer, pse::CHANNEL_UNRELIABLE_SEQUENCED, snapOldEnc, false);
    }
}

void AdversarialHost::handleInputCommand(ENetPeer* peer, pse::ByteReader& reader, uint32_t /*seq*/) {
    pse::MsgInputCommand cmd;
    if (!cmd.deserialize(reader)) return;

    // Fault 8: Drop specific unreliable input packet
    if (m_config.mode == FaultMode::DROP_UNRELIABLE_INPUT) {
        if (cmd.commandSequence == m_config.dropSequence) {
            LockGuard lock(m_telemetryMutex);
            m_telemetry.commandsDropped++;
            logEvent("FAULT INJECTED: Dropped input command seq=" + std::to_string(cmd.commandSequence) + " (simulated network loss)");
            return; // drop!
        }
    }

    // Fault 2: Premature Disconnect during W hold
    if (m_config.mode == FaultMode::PREMATURE_DISCONNECT) {
        uint32_t totalSoFar = 0;
        {
            LockGuard lock(m_telemetryMutex);
            totalSoFar = m_telemetry.commandsReceived;
        }
        if (totalSoFar >= m_config.disconnectAfterCommands) {
            logEvent("FAULT INJECTED: PREMATURE_DISCONNECT triggered after " + std::to_string(totalSoFar) + " commands");
            enet_peer_disconnect_now(peer, pse::LEAVE_KICKED);
            return;
        }
    }

    // Telemetry recording
    LockGuard lock(m_telemetryMutex);
    m_telemetry.commandsReceived++;
    m_telemetry.receivedSequences.push_back(cmd.commandSequence);
    m_telemetry.receivedGenerations.push_back(cmd.connectionGeneration);
    m_telemetry.receivedForwards.push_back(cmd.moveForward);
    m_telemetry.receivedSlots.push_back(cmd.slotId);

    if (cmd.moveForward > 0.5f) {
        m_telemetry.forwardCommandsReceived++;
        m_hadForward = true;
    } else if (m_hadForward && cmd.moveForward == 0.0f) {
        m_telemetry.releaseCommandsReceived++;
        m_hadForward = false;
    }

    if (cmd.moveForward == 0.0f && cmd.moveRight == 0.0f && cmd.buttons == 0) {
        m_telemetry.neutralCommandsReceived++;
    }

    m_lastForward = cmd.moveForward;
}

void AdversarialHost::sendPacket(ENetPeer* peer, uint8_t channel, const std::vector<uint8_t>& data, bool reliable) {
    if (!peer || !m_enetHost) return;
    uint32_t flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
    ENetPacket* pkt = enet_packet_create(data.data(), data.size(), flags);
    enet_peer_send(peer, channel, pkt);
}

void AdversarialHost::logEvent(const std::string& evt) {
    LockGuard lock(m_telemetryMutex);
    m_telemetry.eventLog.push_back(evt);
}

HostTelemetry AdversarialHost::getTelemetry() const {
    LockGuard lock(m_telemetryMutex);
    return m_telemetry;
}

} // namespace pse::qa
