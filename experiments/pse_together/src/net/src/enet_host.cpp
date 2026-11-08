#include "enet_host.h"
#include "ui/model.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <iostream>
#include <random>

namespace pse {

static uint64_t getSteadyTimeMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

ENetHostServer::ENetHostServer()
    : m_sim(60)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(100000, 999999);
    m_sessionId = dist(gen);
}

ENetHostServer::~ENetHostServer() {
    stop();
}

bool ENetHostServer::start(const HostConfig& config) {
    if (m_running.load()) return true;
    if (config.maxClientSlots >= MAX_PLAYERS || !ui::name(config.hostName)) return false;
    if (BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&m_sessionId),sizeof(m_sessionId),BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 || !m_sessionId) return false;

    if (enet_initialize() != 0) {
        std::cerr << "[Host] Failed to initialize ENet." << std::endl;
        return false;
    }

    m_config = config;
    m_sim.resetSessionLedger();
    m_snapshotSequence = 0;
    m_participantSlotMap.clear();
    m_sim.setTickRate(config.tickRate);
    m_sessionMapName = m_sim.getMapName();
    m_sessionMapEpoch = m_sim.getMapEpoch();
    m_netSim.setDropRatePercent(config.simDropPercent);
    m_netSim.setLatencyMs(config.simLatencyMs);

    ENetAddress address;
    if (config.bindAddress.empty() || config.bindAddress == "0.0.0.0") {
        address.host = ENET_HOST_ANY;
    } else {
        if (enet_address_set_host(&address, config.bindAddress.c_str()) != 0) {
            std::cerr << "[Host] Failed to resolve bind address: " << config.bindAddress << std::endl;
            enet_deinitialize();
            return false;
        }
    }
    address.port = config.port;

    m_enetHost = enet_host_create(&address, config.maxClients, CHANNEL_COUNT, 0, 0);
    if (!m_enetHost) {
        std::cerr << "[Host] Failed to create ENet host on " << config.bindAddress << ":" << config.port << std::endl;
        enet_deinitialize();
        return false;
    }

    // Initialize Host Slot (P1 / Slot 0)
    m_sim.initHostSlot(config.participantId, config.hostName);
    m_participantSlotMap[config.participantId] = HOST_SLOT_ID;

    m_lastTickTimeMs = getSteadyTimeMs();
    m_lastSnapshotTimeMs = m_lastTickTimeMs;
    m_running.store(true);

    std::cout << "[Host] Listening on " << config.bindAddress << ":" << config.port
              << " (Max clients: " << (int)config.maxClients
              << ", Tick: " << config.tickRate << " Hz, Snapshots: " << config.snapshotRate << " Hz)" << std::endl;
    return true;
}

void ENetHostServer::stop() {
    if (!m_running.load()) return;
    m_running.store(false);

    if (m_enetHost) {
        for (auto& [peer, info] : m_peers) {
            if (peer) {
                enet_peer_disconnect_now(peer, LEAVE_SERVER_SHUTDOWN);
            }
        }
        enet_host_flush(m_enetHost);
        enet_host_destroy(m_enetHost);
        m_enetHost = nullptr;
    }

    m_peers.clear();
    enet_deinitialize();
    std::cout << "[Host] Stopped." << std::endl;
}

void ENetHostServer::poll(uint32_t timeoutMs) {
    if (!m_running.load() || !m_enetHost) return;

    ENetEvent event;
    while (enet_host_service(m_enetHost, &event, timeoutMs) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                handleConnect(event.peer);
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                handleReceive(event.peer, event.packet, event.channelID);
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                handleDisconnect(event.peer);
                break;
            case ENET_EVENT_TYPE_NONE:
            default:
                break;
        }
    }

    uint64_t now = getSteadyTimeMs();

    // Rejected peers have no slot/generation or session callbacks to retire.
    for (auto it = m_peers.begin(); it != m_peers.end();) {
        if (!it->second.authenticated && it->second.rejectDeadlineMs && now >= it->second.rejectDeadlineMs) {
            std::cout << "[Host] Rejected unauthenticated peer expired (reason " << (int)it->second.rejectReason << ")" << std::endl;
            enet_peer_disconnect_now(it->first, it->second.rejectReason);
            it = m_peers.erase(it);
        } else ++it;
    }

    // Bridge mode: no gameplay simulation on the network thread. The authoritative
    // simulation runs behind the ThreadBridge and publishes snapshots itself.
    if (!m_config.bridgeMode) {
        // Step Simulation Tick with fixed-timestep accumulator (decoupled from network polling)
        uint32_t tickIntervalMs = 1000 / m_config.tickRate;
        if (tickIntervalMs == 0) tickIntervalMs = 16;
        if (m_lastTickTimeMs == 0) m_lastTickTimeMs = now;

        uint64_t elapsed = now - m_lastTickTimeMs;
        uint32_t ticksToRun = static_cast<uint32_t>(elapsed / tickIntervalMs);
        if (ticksToRun > 4) {
            ticksToRun = 4; // Catch-up clamp to prevent spiral of death
        }

        for (uint32_t t = 0; t < ticksToRun; ++t) {
            m_sim.tick();
            if (onTickCallback) {
                onTickCallback(static_cast<float>(tickIntervalMs) / 1000.0f);
            }
            m_lastTickTimeMs += tickIntervalMs;
        }
        // Prevent time drift from accumulating unbounded if paused
        if (now > m_lastTickTimeMs + 4 * tickIntervalMs) {
            m_lastTickTimeMs = now;
        }

        // Broadcast Snapshot
        uint32_t snapshotIntervalMs = 1000 / m_config.snapshotRate;
        if (now >= m_lastSnapshotTimeMs + snapshotIntervalMs) {
            broadcastSnapshot();
            if (onSnapshotBroadcastCallback) {
                onSnapshotBroadcastCallback();
            }
            m_lastSnapshotTimeMs = now;
        }
    }

    // Update Network Impairment Simulator delayed queue
    m_netSim.update(now, [this](uint8_t channel, const uint8_t* data, size_t size, void* peerPtr, bool reliable) {
        ENetPeer* peer = static_cast<ENetPeer*>(peerPtr);
        if (peer && m_running.load()) {
            uint32_t flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
            if(channel==CHANNEL_DIAGNOSTIC || channel==CHANNEL_WORLD_SNAPSHOT)flags=ENET_PACKET_FLAG_UNSEQUENCED;
            ENetPacket* pkt = enet_packet_create(data, size, flags);
            enet_peer_send(peer, channel, pkt);
            m_metrics.recordBytesSent(size);
            m_metrics.recordPacketSent();
        }
    });

    // Record system usage and queue sizes
    m_metrics.recordQueueSize(m_peers.size());
}

void ENetHostServer::handleConnect(ENetPeer* peer) {
    PeerInfo info;
    info.slotId = 0xFF;
    info.authenticated = false;
    info.connectTimeMs = getSteadyTimeMs();
    m_peers[peer] = info;
}

void ENetHostServer::handleReceive(ENetPeer* peer, ENetPacket* packet, uint8_t channel) {
    const auto pending = m_peers.find(peer);
    if (pending != m_peers.end() && pending->second.rejectDeadlineMs) return; // terminal rejection
    if (!packet || packet->dataLength < 11) {
        return; // Malformed / truncated packet dropped
    }
    if (packet->dataLength > MAX_PACKET_SIZE) {
        return; // Oversized packet dropped
    }

    m_metrics.recordBytesReceived(packet->dataLength);
    m_metrics.recordPacketReceived();

    MsgHeader header;
    ByteReader reader(packet->data, packet->dataLength);
    if (!header.deserialize(reader)) return;
    if (header.magic != NET_MAGIC) return;
    if(header.version!=NET_PROTOCOL_VERSION) { rejectPeer(peer,REJECT_PROTOCOL_MISMATCH,"Protocol version mismatch");return; }

    // UI is accepted only from an authenticated peer, reliable control channel.
    // Reject the opposite direction even when a custom handler is installed.
    if (header.msgType >= MSG_CHAT_REQUEST && header.msgType <= MSG_UI_POSE) {
        if (header.msgType != MSG_CHAT_REQUEST || channel != CHANNEL_RELIABLE_CONTROL ||
            !(packet->flags & ENET_PACKET_FLAG_RELIABLE) || pending == m_peers.end() ||
            !pending->second.authenticated) return;
        if (m_customHandler) m_customHandler(peer, header.msgType, reader, header.sequence);
        return;
    }
    if (m_config.bridgeMode &&
        (header.msgType == MSG_INPUT_COMMAND || header.msgType == MSG_MAP_READY ||
         header.msgType == MSG_RESPAWN_REQUEST)) {
        // Gameplay messages belong to the simulation behind the bridge. Only
        // authenticated peers may reach it; slot ownership is checked by the worker.
        auto it = m_peers.find(peer);
        if (it == m_peers.end() || !it->second.authenticated) return;
        if (m_customHandler) {
            m_customHandler(peer, header.msgType, reader, header.sequence);
        }
        return;
    }

    switch (header.msgType) {
        case MSG_HELLO:
            onMsgHello(peer, reader, header.sequence);
            break;
        case MSG_INPUT_COMMAND:
            onMsgInputCommand(peer, reader, header.sequence);
            break;
        case MSG_MAP_READY:
            onMsgMapReady(peer, reader, header.sequence);
            break;
        case MSG_RESPAWN_REQUEST:
            onMsgRespawnRequest(peer, reader, header.sequence);
            break;
        case MSG_PING:
            onMsgPing(peer, reader, header.sequence);
            break;
        default:
            if (m_customHandler) {
                m_customHandler(peer, header.msgType, reader, header.sequence);
            }
            break;
    }
}

void ENetHostServer::handleDisconnect(ENetPeer* peer) {
    auto it = m_peers.find(peer);
    if (it != m_peers.end()) {
        uint8_t slotId = it->second.slotId;
        uint32_t gen = it->second.connectionGeneration;
        std::string partId = it->second.participantId;
        if (slotId < MAX_PLAYERS) {
            m_sim.disconnectSlot(slotId, LEAVE_USER_QUIT);
            broadcastPlayerLeave(slotId, partId, LEAVE_USER_QUIT);
            if (onClientDisconnectedCallback) {
                onClientDisconnectedCallback(slotId);
            }
            if (onClientSessionEndedCallback) {
                onClientSessionEndedCallback(slotId, gen);
            }
            std::cout << "[Host] Client disconnected from slot P" << (int)(slotId + 1)
                      << " (" << partId << ")" << std::endl;
        }
        m_peers.erase(it);
    }
}

void ENetHostServer::rejectPeer(ENetPeer* peer, RejectReason reason, const std::string& message) {
    sendReject(peer, reason, message);
    auto it = m_peers.find(peer);
    if (m_config.rejectedPeerTimeoutMs && it != m_peers.end() && !it->second.authenticated) {
        it->second.rejectDeadlineMs = getSteadyTimeMs() + m_config.rejectedPeerTimeoutMs;
        it->second.rejectReason = static_cast<uint8_t>(reason);
    }
    enet_peer_disconnect_later(peer, reason);
}

void ENetHostServer::onMsgHello(ENetPeer* peer, ByteReader& reader, uint32_t /*seq*/) {
    const auto existing = m_peers.find(peer);
    if (existing == m_peers.end() || existing->second.authenticated) return;
    MsgHello msg;
    if (!msg.deserialize(reader) || reader.remaining() || !ui::name(msg.displayName) || !ui::participant(msg.participantId)) {
        rejectPeer(peer, REJECT_MALFORMED, "Malformed HELLO payload");
        return;
    }

    // Protocol version verification
    if (msg.clientProtocolVersion != NET_PROTOCOL_VERSION) {
        rejectPeer(peer, REJECT_PROTOCOL_MISMATCH, "Protocol version mismatch");
        return;
    }

    // Token authorization
    if (!m_config.joinToken.empty() && msg.joinToken != m_config.joinToken) {
        rejectPeer(peer, REJECT_INVALID_TOKEN, "Invalid session join token");
        return;
    }

    // Allocate or retrieve slot for participant
    uint64_t assignedKey = 0;
    RejectReason rejectReason = REJECT_MALFORMED;
    ENetPeer* oldPeerToDisconnect = nullptr;
    int8_t slot = m_sim.allocateClientSlot(msg.participantId, msg.displayName, msg.reconnectKey, peer, assignedKey, rejectReason, &oldPeerToDisconnect, m_config.maxClientSlots);
    if (slot < 0) {
        rejectPeer(peer, rejectReason, getRejectReasonString(rejectReason));
        return;
    }

    // If reconnecting displaced an old peer (ghost/half-open peer), clean it up
    if (oldPeerToDisconnect) {
        auto oldIt = m_peers.find(oldPeerToDisconnect);
        if (oldIt != m_peers.end()) {
            m_peers.erase(oldIt);
        }
        enet_peer_disconnect_now(oldPeerToDisconnect, LEAVE_KICKED);
    }

    uint8_t slotId = static_cast<uint8_t>(slot);
    auto& pSlot = m_sim.getSlot(slotId);

    // Update peer state
    PeerInfo& info = m_peers[peer];
    info.slotId = slotId;
    info.participantId = msg.participantId;
    info.displayName = msg.displayName;
    info.authenticated = true;
    info.connectionGeneration = pSlot.connectionGeneration;

    m_participantSlotMap[msg.participantId] = slotId;

    // Send Welcome
    sendWelcome(peer, slotId, pSlot.connectionGeneration, assignedKey);

    // Broadcast Player Join to all clients
    broadcastPlayerJoin(slotId, msg.participantId, msg.displayName, pSlot.connectionGeneration);

    if (onClientAuthenticatedCallback) {
        onClientAuthenticatedCallback(peer, slotId, pSlot.connectionGeneration, (pSlot.connectionGeneration > 1));
    }

    std::cout << "[Host] Assigned slot P" << (int)(slotId + 1) << " to participant: "
              << msg.displayName << " [" << msg.participantId << "], gen="
              << pSlot.connectionGeneration << std::endl;
}

void ENetHostServer::onMsgInputCommand(ENetPeer* peer, ByteReader& reader, uint32_t /*seq*/) {
    auto it = m_peers.find(peer);
    if (it == m_peers.end() || !it->second.authenticated) return;

    MsgInputCommand cmd;
    if (!cmd.deserialize(reader)) return;

    // Verify slot ownership
    if (cmd.slotId != it->second.slotId) return;

    m_sim.enqueueCommand(it->second.slotId, cmd);
}

void ENetHostServer::onMsgMapReady(ENetPeer* peer, ByteReader& reader, uint32_t /*seq*/) {
    auto it = m_peers.find(peer);
    if (it == m_peers.end() || !it->second.authenticated) return;

    MsgMapReady msg;
    if (!msg.deserialize(reader)) return;

    if (msg.mapEpoch == m_sim.getMapEpoch()) {
        std::cout << "[Host] Player P" << (int)(it->second.slotId + 1)
                  << " confirmed ready for epoch " << msg.mapEpoch << std::endl;
    }
}

void ENetHostServer::onMsgRespawnRequest(ENetPeer* peer, ByteReader& reader, uint32_t /*seq*/) {
    auto it = m_peers.find(peer);
    if (it == m_peers.end() || !it->second.authenticated) return;

    MsgRespawnRequest req;
    if (!req.deserialize(reader)) return;

    if (req.slotId != it->second.slotId) return;

    MsgRespawnConfirm confirm;
    WorldSimulation::RespawnResult res = m_sim.processRespawnRequest(req, confirm);
    if (res == WorldSimulation::RespawnResult::SUCCESS) {
        // Broadcast confirmation to ALL players so everyone's world updates puppet & armory
        auto encoded = encodePacket(MSG_RESPAWN_CONFIRM, ++m_snapshotSequence, confirm);
        broadcastPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);
        std::cout << "[Host] Confirmed manual respawn for slot P" << (int)(req.slotId + 1)
                  << " at (" << confirm.spawnX << ", " << confirm.spawnY << ")" << std::endl;
    } else {
        std::cout << "[Host] Rejected respawn request from P" << (int)(req.slotId + 1)
                  << ", code=" << (int)res << std::endl;
    }
}

void ENetHostServer::onMsgPing(ENetPeer* peer, ByteReader& reader, uint32_t seq) {
    MsgPing ping;
    if (!ping.deserialize(reader)) return;

    MsgPong pong;
    pong.timestampMs = ping.timestampMs;
    pong.pingSequence = ping.pingSequence;

    auto encoded = encodePacket(MSG_PONG, seq, pong);
    sendPacket(peer, CHANNEL_DIAGNOSTIC, encoded, false);
}

void ENetHostServer::sendPacket(ENetPeer* peer, uint8_t channel, const std::vector<uint8_t>& data, bool reliable) {
    if (!peer || !m_enetHost) return;

    // Check through NetworkSimulator
    if (!m_netSim.processOutbound(channel, data.data(), data.size(), peer, reliable)) {
        return; // Dropped by simulator
    }

    if (m_netSim.getLatencyMs() > 0) {
        return; // Queued in delay buffer
    }

    uint32_t flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
    if (channel == CHANNEL_DIAGNOSTIC || channel == CHANNEL_WORLD_SNAPSHOT) {
        flags = ENET_PACKET_FLAG_UNSEQUENCED;
    }
    ENetPacket* packet = enet_packet_create(data.data(), data.size(), flags);
    enet_peer_send(peer, channel, packet);
    m_metrics.recordBytesSent(data.size());
    m_metrics.recordPacketSent();
}

void ENetHostServer::broadcastPacket(uint8_t channel, const std::vector<uint8_t>& data, bool reliable, ENetPeer* ignorePeer) {
    for (auto& [peer, info] : m_peers) {
        if (peer && peer != ignorePeer && info.authenticated) {
            sendPacket(peer, channel, data, reliable);
        }
    }
}

void ENetHostServer::sendWelcome(ENetPeer* peer, uint8_t slotId, uint32_t connectionGeneration, uint64_t reconnectKey) {
    MsgWelcome welcome;
    welcome.protocolVersion = NET_PROTOCOL_VERSION;
    welcome.sessionId = m_sessionId;
    welcome.assignedSlot = slotId;
    welcome.connectionGeneration = connectionGeneration;
    welcome.mapEpoch = m_config.bridgeMode ? m_sessionMapEpoch : m_sim.getMapEpoch();
    welcome.serverTickRate = m_config.tickRate;
    welcome.serverSnapshotRate = m_config.snapshotRate;
    welcome.mapName = m_config.bridgeMode ? m_sessionMapName : m_sim.getMapName();
    welcome.mapStructure=m_sessionMapStructure;
    welcome.reconnectKey = reconnectKey;

    auto encoded = encodePacket(MSG_WELCOME, ++m_snapshotSequence, welcome);
    sendPacket(peer, CHANNEL_RELIABLE_CONTROL, encoded, true);
}

void ENetHostServer::sendReject(ENetPeer* peer, RejectReason reason, const std::string& msg) {
    MsgReject reject;
    reject.reasonCode = static_cast<uint8_t>(reason);
    reject.reasonMessage = msg;

    auto encoded = encodePacket(MSG_REJECT, ++m_snapshotSequence, reject);
    sendPacket(peer, CHANNEL_RELIABLE_CONTROL, encoded, true);
}

void ENetHostServer::broadcastPlayerJoin(uint8_t slotId, const std::string& partId, const std::string& name, uint32_t gen) {
    MsgPlayerJoin join;
    join.slotId = slotId;
    join.participantId = partId;
    join.displayName = name;
    join.connectionGeneration = gen;

    auto encoded = encodePacket(MSG_PLAYER_JOIN, ++m_snapshotSequence, join);
    broadcastPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);
}

void ENetHostServer::broadcastPlayerLeave(uint8_t slotId, const std::string& partId, LeaveReason reason) {
    MsgPlayerLeave leave;
    leave.slotId = slotId;
    leave.participantId = partId;
    leave.reasonCode = static_cast<uint8_t>(reason);

    auto encoded = encodePacket(MSG_PLAYER_LEAVE, ++m_snapshotSequence, leave);
    broadcastPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);
}

void ENetHostServer::broadcastSnapshot() {
    MsgPlayerSnapshot snap = m_sim.generateSnapshot(++m_snapshotSequence);
    auto encoded = encodePacket(MSG_PLAYER_SNAPSHOT, m_snapshotSequence, snap);

    // Unreliable sequenced snapshot
    for (auto& [peer, info] : m_peers) {
        if (peer && info.authenticated) {
            sendPacket(peer, CHANNEL_UNRELIABLE_SEQUENCED, encoded, false);
        }
    }
}

void ENetHostServer::changeMap(const std::string& newMapName) {
    m_sim.changeMap(newMapName);
    if (onMapChangeCallback) {
        onMapChangeCallback(newMapName, m_sim.getMapEpoch());
    }
    MsgMapChange change;
    change.newMapEpoch = m_sim.getMapEpoch();
    change.mapName = newMapName;

    auto encoded = encodePacket(MSG_MAP_CHANGE, ++m_snapshotSequence, change);
    broadcastPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);
    std::cout << "[Host] Changed map to " << newMapName << " (Epoch: " << change.newMapEpoch << ")" << std::endl;
}

void ENetHostServer::triggerDamageOnSlot(uint8_t slotId, int32_t amount) {
    m_sim.applyDamage(slotId, amount);
}

uint32_t ENetHostServer::getConnectedClientCount() const {
    uint32_t count = 0;
    for (const auto& [peer, info] : m_peers) {
        if (info.authenticated) count++;
    }
    return count;
}

int8_t ENetHostServer::getSlotForPeer(ENetPeer* peer) const {
    auto it = m_peers.find(peer);
    if (it != m_peers.end() && it->second.authenticated) {
        return static_cast<int8_t>(it->second.slotId);
    }
    return -1;
}

bool ENetHostServer::getPeerSession(ENetPeer* peer, uint8_t& outSlot, uint32_t& outGeneration) const {
    auto it = m_peers.find(peer);
    if (it == m_peers.end() || !it->second.authenticated || it->second.slotId >= MAX_PLAYERS) {
        return false;
    }
    outSlot = it->second.slotId;
    outGeneration = it->second.connectionGeneration;
    return true;
}

void ENetHostServer::setSessionMap(const std::string& mapName,uint32_t mapEpoch,NativeMapStructure structure) {
    m_sessionMapStructure=structure;
    m_sessionMapName = mapName;
    m_sessionMapEpoch = mapEpoch;
}

bool ENetHostServer::kickSlot(uint8_t slotId, uint8_t reason) {
    ENetPeer* peer = getPeerForSlot(slotId);
    if (!peer) return false;
    enet_peer_disconnect(peer, reason);
    return true;
}

ENetPeer* ENetHostServer::getPeerForSlot(uint8_t slotId) const {
    for (const auto& [peer, info] : m_peers) {
        if (info.authenticated && info.slotId == slotId) {
            return peer;
        }
    }
    return nullptr;
}

} // namespace pse
