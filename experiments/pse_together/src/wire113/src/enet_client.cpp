#include "enet_client.h"
#include <iostream>
#include <chrono>

namespace pse {

static uint64_t getSteadyTimeMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

ENetClientNode::ENetClientNode() {
}

ENetClientNode::~ENetClientNode() {
    disconnect();
}

bool ENetClientNode::connect(const ClientConfig& config) {
    if (m_enetClient) return false; // Includes an unpolled pending connection.

    if (enet_initialize() != 0) {
        std::cerr << "[Client] Failed to initialize ENet." << std::endl;
        return false;
    }

    m_enetInitialized = true;
    if (m_config.hostAddress != config.hostAddress || m_config.hostPort != config.hostPort ||
        m_config.participantId != config.participantId) m_reconnectKey = 0;
    m_config = config;
    m_assignedSlot = 0xff; m_connectionGeneration = 0; m_mapEpoch = 0; m_mapName.clear();
    m_sessionId = 0; m_lastSnapshot = {}; m_highestReceivedSnapshotSeq = 0;
    m_netSim.setDropRatePercent(config.simDropPercent);
    m_netSim.setLatencyMs(config.simLatencyMs);

    m_enetClient = enet_host_create(nullptr, 1, CHANNEL_COUNT, 0, 0);
    if (!m_enetClient) {
        std::cerr << "[Client] Failed to create ENet client host." << std::endl;
        disconnect();
        return false;
    }

    ENetAddress address;
    if (enet_address_set_host(&address, config.hostAddress.c_str()) != 0) {
        std::cerr << "[Client] Failed to resolve host address: " << config.hostAddress << std::endl;
        disconnect();
        return false;
    }
    address.port = config.hostPort;

    m_serverPeer = enet_host_connect(m_enetClient, &address, CHANNEL_COUNT, 0);
    if (!m_serverPeer) {
        std::cerr << "[Client] Failed to initiate connection to " << config.hostAddress << ":" << config.hostPort << std::endl;
        disconnect();
        return false;
    }

    std::cout << "[Client] Connecting to " << config.hostAddress << ":" << config.hostPort
              << " as [" << config.participantId << "] " << config.displayName << "..." << std::endl;
    return true;
}

void ENetClientNode::disconnect(uint8_t reason) {
    if (m_serverPeer) {
        enet_peer_disconnect(m_serverPeer, reason);
        // Dispatch pending disconnect packet before tearing down
        if (m_enetClient) {
            ENetEvent event;
            for (int i = 0; i < 20; ++i) {
                if (enet_host_service(m_enetClient, &event, 5) > 0) {
                    if (event.type == ENET_EVENT_TYPE_RECEIVE && event.packet) {
                        enet_packet_destroy(event.packet);
                    } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                        break;
                    }
                }
            }
        }
        m_serverPeer = nullptr;
    }
    if (m_enetClient) {
        enet_host_flush(m_enetClient);
        enet_host_destroy(m_enetClient);
        m_enetClient = nullptr;
    }
    m_connected.store(false);
    m_authenticated.store(false);
    if (m_enetInitialized) { enet_deinitialize(); m_enetInitialized = false; }
}

void ENetClientNode::poll(uint32_t timeoutMs) {
    if (!m_enetClient) return;

    ENetEvent event;
    while (enet_host_service(m_enetClient, &event, timeoutMs) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                m_connected.store(true);
                // Send HELLO handshake
                MsgHello hello;
                hello.clientProtocolVersion = NET_PROTOCOL_VERSION;
                hello.clientBuildVersion = "1.0.0";
                hello.joinToken = m_config.joinToken;
                hello.participantId = m_config.participantId;
                hello.displayName = m_config.displayName;
                hello.reconnectKey = m_reconnectKey != 0 ? m_reconnectKey : m_config.reconnectKey;

                auto encoded = encodePacket(MSG_HELLO, ++m_commandSequence, hello);
                sendPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE:
                handleReceive(event.packet, event.channelID);
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                m_connected.store(false);
                m_authenticated.store(false);
                if (onDisconnectedCallback) onDisconnectedCallback();
                break;
            case ENET_EVENT_TYPE_NONE:
            default:
                break;
        }
    }

    uint64_t now = getSteadyTimeMs();

    // Send diagnostic ping every 1000ms
    if (m_authenticated.load() && now >= m_lastPingTimeMs + 1000) {
        sendPing();
        m_lastPingTimeMs = now;
    }

    // Flush network simulator delayed packets
    m_netSim.update(now, [this](uint8_t channel, const uint8_t* data, size_t size, void* peerPtr, bool reliable) {
        ENetPeer* peer = static_cast<ENetPeer*>(peerPtr);
        if (peer && m_enetClient) {
            uint32_t flags = reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED;
            ENetPacket* pkt = enet_packet_create(data, size, flags);
            enet_peer_send(peer, channel, pkt);
            m_metrics.recordBytesSent(size);
            m_metrics.recordPacketSent();
        }
    });
}

void ENetClientNode::handleReceive(ENetPacket* packet, uint8_t channel) {
    if (!packet || packet->dataLength < 11) return;
    if (packet->dataLength > MAX_PACKET_SIZE) return;

    m_metrics.recordBytesReceived(packet->dataLength);
    m_metrics.recordPacketReceived();

    MsgHeader header;
    ByteReader reader(packet->data, packet->dataLength);
    if (!header.deserialize(reader)) return;
    if (header.magic != NET_MAGIC) return;
    if(header.version!=NET_PROTOCOL_VERSION)return;
    if (header.msgType >= MSG_CHAT_REQUEST && header.msgType <= MSG_UI_POSE) {
        if (header.msgType == MSG_CHAT_REQUEST || !m_authenticated.load()) return;
        const bool pose=header.msgType==MSG_UI_POSE;
        if(pose && (packet->flags&ENET_PACKET_FLAG_UNSEQUENCED))return;
        if (channel != (pose?CHANNEL_UNRELIABLE_SEQUENCED:CHANNEL_RELIABLE_CONTROL) ||
            static_cast<bool>(packet->flags & ENET_PACKET_FLAG_RELIABLE)==pose) return;
        if (m_customHandler) m_customHandler(header.msgType, reader, header.sequence);
        return;
    }
    if (header.msgType == MSG_PLAYER_SNAPSHOT &&
        (header.version != NET_PROTOCOL_VERSION || channel != CHANNEL_UNRELIABLE_SEQUENCED)) return;

    if (m_config.bridgeMode &&
        (header.msgType == MSG_PLAYER_SNAPSHOT || header.msgType == MSG_RESPAWN_CONFIRM ||
         header.msgType == MSG_MAP_CHANGE)) {
        // Owned by the client simulation behind the bridge (no automatic MAP_READY).
        if (m_customHandler) {
            m_customHandler(header.msgType, reader, header.sequence);
        }
        return;
    }

    switch (header.msgType) {
        case MSG_WELCOME:
            onMsgWelcome(reader, header.sequence);
            break;
        case MSG_REJECT:
            onMsgReject(reader, header.sequence);
            break;
        case MSG_PLAYER_SNAPSHOT:
            onMsgPlayerSnapshot(reader, header.sequence);
            break;
        case MSG_RESPAWN_CONFIRM:
            onMsgRespawnConfirm(reader, header.sequence);
            break;
        case MSG_MAP_CHANGE:
            onMsgMapChange(reader, header.sequence);
            break;
        case MSG_PONG:
            onMsgPong(reader, header.sequence);
            break;
        default:
            if (m_customHandler) {
                m_customHandler(header.msgType, reader, header.sequence);
            }
            break;
    }
}

void ENetClientNode::onMsgWelcome(ByteReader& reader, uint32_t /*seq*/) {
    MsgWelcome welcome;
    if (!welcome.deserialize(reader) || reader.remaining()!=0 || welcome.protocolVersion!=NET_PROTOCOL_VERSION) return;

    m_sessionId = welcome.sessionId;
    m_assignedSlot = welcome.assignedSlot;
    m_connectionGeneration = welcome.connectionGeneration;
    m_reconnectKey = welcome.reconnectKey;
    m_config.reconnectKey = welcome.reconnectKey;
    m_mapEpoch = welcome.mapEpoch;
    m_mapName = welcome.mapName;
    m_serverTickRate = welcome.serverTickRate;
    m_serverSnapshotRate = welcome.serverSnapshotRate;
    m_authenticated.store(true);

    std::cout << "[Client] Authenticated! Slot: P" << (int)(m_assignedSlot + 1)
              << ", Gen: " << m_connectionGeneration
              << ", Map: " << m_mapName << " (Epoch " << m_mapEpoch << ")" << std::endl;

    if (onWelcomeCallback) onWelcomeCallback(welcome);
}

void ENetClientNode::onMsgReject(ByteReader& reader, uint32_t /*seq*/) {
    MsgReject reject;
    if (!reject.deserialize(reader)) return;

    std::cerr << "[Client] Connection rejected by host: code " << (int)reject.reasonCode
              << " (" << reject.reasonMessage << ")" << std::endl;
    m_authenticated.store(false);

    if (onRejectCallback) onRejectCallback(reject);
}

void ENetClientNode::onMsgPlayerSnapshot(ByteReader& reader, uint32_t /*seq*/) {
    MsgPlayerSnapshot snap;
    if (!snap.deserialize(reader)) return;

    // Reject out-of-order or stale snapshots
    if (snap.snapshotSequence <= m_highestReceivedSnapshotSeq && m_highestReceivedSnapshotSeq != 0) {
        return; // Stale snapshot discarded
    }
    m_highestReceivedSnapshotSeq = snap.snapshotSequence;
    m_lastSnapshot = snap;

    // Check if input latency measurement is active
    if (onSnapshotCallback) onSnapshotCallback(snap);
}

void ENetClientNode::onMsgRespawnConfirm(ByteReader& reader, uint32_t /*seq*/) {
    MsgRespawnConfirm confirm;
    if (!confirm.deserialize(reader)) return;

    if (confirm.slotId == m_assignedSlot) {
        m_respawnRequested = false;
        std::cout << "[Client] Respawn confirmed at (" << confirm.spawnX << ", " << confirm.spawnY
                  << ", " << confirm.spawnZ << "), weapons reset to starter loadout." << std::endl;
    }

    if (onRespawnConfirmCallback) onRespawnConfirmCallback(confirm);
}

void ENetClientNode::onMsgMapChange(ByteReader& reader, uint32_t /*seq*/) {
    MsgMapChange change;
    if (!change.deserialize(reader)) return;

    m_mapEpoch = change.newMapEpoch;
    m_mapName = change.mapName;
    std::cout << "[Client] Map changed to " << m_mapName << " (Epoch " << m_mapEpoch << ")" << std::endl;

    // Send MAP_READY acknowledgment
    MsgMapReady ready;
    ready.slotId = m_assignedSlot;
    ready.mapEpoch = m_mapEpoch;
    auto encoded = encodePacket(MSG_MAP_READY, ++m_commandSequence, ready);
    sendPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);

    if (onMapChangeCallback) onMapChangeCallback(change);
}

void ENetClientNode::onMsgPong(ByteReader& reader, uint32_t /*seq*/) {
    MsgPong pong;
    if (!pong.deserialize(reader)) return;

    uint64_t now = getSteadyTimeMs();
    if (now >= pong.timestampMs) {
        double rtt = static_cast<double>(now - pong.timestampMs);
        m_metrics.recordRtt(rtt);
    }
}

void ENetClientNode::sendInputCommand(float forward, float right, float pitch, float yaw, uint32_t buttons, uint8_t weapon) {
    if (!m_authenticated.load() || !m_serverPeer) return;

    MsgInputCommand cmd;
    cmd.slotId = m_assignedSlot;
    cmd.connectionGeneration = m_connectionGeneration;
    cmd.commandSequence = ++m_commandSequence;
    cmd.clientTick = ++m_clientTick;
    cmd.pitch = pitch;
    cmd.yaw = yaw;
    cmd.moveForward = forward;
    cmd.moveRight = right;
    cmd.buttons = buttons;
    cmd.selectedWeapon = weapon;

    auto encoded = encodePacket(MSG_INPUT_COMMAND, m_commandSequence, cmd);
    sendPacket(CHANNEL_UNRELIABLE_SEQUENCED, encoded, false);
}

bool ENetClientNode::triggerManualRespawn() {
    if (!m_authenticated.load() || !m_serverPeer) return false;

    // Check if player is dead
    const PlayerStateSnapshot* localState = getLocalPlayerState();
    if (!localState || localState->lifeState != LIFE_DEAD) {
        return false; // Can only respawn when dead
    }

    // Single-shot debounce
    if (m_respawnRequested) {
        return false; // Already requested
    }

    m_respawnRequested = true;
    MsgRespawnRequest req;
    req.slotId = m_assignedSlot;
    req.mapEpoch = m_mapEpoch;
    req.deathSequence = localState->deathSequence;

    auto encoded = encodePacket(MSG_RESPAWN_REQUEST, ++m_commandSequence, req);
    sendPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);
    std::cout << "[Client] Sent manual respawn request (deathSeq=" << req.deathSequence
              << ", epoch=" << req.mapEpoch << ")" << std::endl;
    return true;
}

const PlayerStateSnapshot* ENetClientNode::getLocalPlayerState() const {
    for (const auto& p : m_lastSnapshot.players) {
        if (p.slotId == m_assignedSlot) {
            return &p;
        }
    }
    return nullptr;
}

void ENetClientNode::sendPing() {
    if (!m_serverPeer) return;

    MsgPing ping;
    ping.timestampMs = getSteadyTimeMs();
    ping.pingSequence = ++m_pingSequence;

    auto encoded = encodePacket(MSG_PING, m_pingSequence, ping);
    sendPacket(CHANNEL_DIAGNOSTIC, encoded, false);
}

void ENetClientNode::sendPacket(uint8_t channel, const std::vector<uint8_t>& data, bool reliable) {
    if (!m_serverPeer || !m_enetClient) return;

    if (!m_netSim.processOutbound(channel, data.data(), data.size(), m_serverPeer, reliable)) {
        return; // Dropped by simulator
    }

    if (m_netSim.getLatencyMs() > 0) {
        return; // Queued in delay queue
    }

    uint32_t flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
    if (channel == CHANNEL_DIAGNOSTIC || channel == CHANNEL_WORLD_SNAPSHOT) {
        flags = ENET_PACKET_FLAG_UNSEQUENCED;
    }
    ENetPacket* packet = enet_packet_create(data.data(), data.size(), flags);
    enet_peer_send(m_serverPeer, channel, packet);
    m_metrics.recordBytesSent(data.size());
    m_metrics.recordPacketSent();
}

} // namespace pse
