#include "m12a_client.h"
#include "m12a_logger.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <thread>

namespace pse::m12a {

const char* clientStateToString(ClientState st) {
    switch (st) {
        case ClientState::DISCONNECTED:  return "DISCONNECTED";
        case ClientState::CONNECTING:    return "CONNECTING";
        case ClientState::CONNECTED:     return "CONNECTED";
        case ClientState::AUTHENTICATED: return "AUTHENTICATED";
        case ClientState::DISCONNECTING: return "DISCONNECTING";
        case ClientState::ERROR_STATE:   return "ERROR_STATE";
        default:                         return "UNKNOWN";
    }
}

M12aClient::M12aClient() {
    m_epochTime = std::chrono::steady_clock::now();
}

M12aClient::~M12aClient() {
    disconnect(pse::LEAVE_USER_QUIT);
}

void M12aClient::setState(ClientState newState, const std::string& ctx) {
    ClientState oldState = m_state.exchange(newState);
    if (oldState != newState) {
        M12aLogger::instance().logState(clientStateToString(oldState), clientStateToString(newState), ctx);
    }
}

bool M12aClient::connect(const std::string& host, uint16_t port,
                         const std::string& participantId, const std::string& displayName,
                         const std::string& joinToken, uint64_t reconnectKey) {
    if (m_state.load() != ClientState::DISCONNECTED && m_state.load() != ClientState::ERROR_STATE) {
        disconnect();
    }

    m_currentConfig.hostAddress = host;
    m_currentConfig.hostPort = port;
    m_currentConfig.participantId = participantId;
    m_currentConfig.displayName = displayName;
    m_currentConfig.joinToken = joinToken;
    m_currentConfig.reconnectKey = reconnectKey;
    m_currentConfig.bridgeMode = false;

    m_hasReject = false;
    m_lastReject = pse::MsgReject{};

    m_clientNode = std::make_unique<pse::ENetClientNode>();
    setupClientCallbacks();

    setState(ClientState::CONNECTING, host + ":" + std::to_string(port) +
             " as [" + participantId + "] '" + displayName +
             (reconnectKey != 0 ? "' (reconnectKey set)" : "' (new session)"));

    if (!m_clientNode->connect(m_currentConfig)) {
        setState(ClientState::ERROR_STATE, "ENetClientNode::connect failed to initiate");
        return false;
    }

    return true;
}

void M12aClient::disconnect(uint8_t reason) {
    ClientState st = m_state.load();
    if (st == ClientState::DISCONNECTED) {
        return;
    }

    setState(ClientState::DISCONNECTING, "reason=" + std::to_string(reason));

    if (m_clientNode) {
        m_clientNode->onDisconnectedCallback = nullptr;
        m_clientNode->disconnect(reason);
        m_clientNode.reset();
    }

    setState(ClientState::DISCONNECTED, "closed");
}

void M12aClient::poll(uint32_t timeoutMs) {
    if (!m_clientNode) return;

    m_clientNode->poll(timeoutMs);

    // Track internal ENet connection state before welcome
    if (m_state.load() == ClientState::CONNECTING && m_clientNode->isConnected()) {
        setState(ClientState::CONNECTED, "ENet socket connected, awaiting MsgWelcome");
    }
}

bool M12aClient::waitAuthenticated(uint32_t timeoutMs) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        poll(5);
        if (m_state.load() == ClientState::AUTHENTICATED) {
            return true;
        }
        if (m_state.load() == ClientState::ERROR_STATE) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    poll(1);
    return m_state.load() == ClientState::AUTHENTICATED;
}

bool M12aClient::sendInputCommand(float forward, float right, float pitch, float yaw,
                                  uint32_t buttons, uint8_t weapon) {
    if (m_state.load() != ClientState::AUTHENTICATED || !m_clientNode) {
        return false;
    }

    ++m_localCommandSeq;
    ++m_localClientTick;

    pse::MsgInputCommand cmd;
    cmd.slotId = m_assignedSlot;
    cmd.connectionGeneration = m_connectionGeneration;
    cmd.commandSequence = m_localCommandSeq;
    cmd.clientTick = m_localClientTick;
    cmd.pitch = pitch;
    cmd.yaw = yaw;
    cmd.moveForward = forward;
    cmd.moveRight = right;
    cmd.buttons = buttons;
    cmd.selectedWeapon = weapon;

    auto encoded = pse::encodePacket(pse::MSG_INPUT_COMMAND, m_localCommandSeq, cmd);
    m_clientNode->sendPacket(pse::CHANNEL_UNRELIABLE_SEQUENCED, encoded, false);

    double nowMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_epochTime).count();

    SentCommand record;
    record.sequence = m_localCommandSeq;
    record.tick = m_localClientTick;
    record.slotId = m_assignedSlot;
    record.connectionGeneration = m_connectionGeneration;
    record.forward = forward;
    record.right = right;
    record.pitch = pitch;
    record.yaw = yaw;
    record.buttons = buttons;
    record.weapon = weapon;
    record.timestampMs = nowMs;
    m_sentCommands.push_back(record);

    M12aLogger::instance().logOutboundCommand(m_localCommandSeq, m_localClientTick,
                                             m_assignedSlot, m_connectionGeneration,
                                             forward, right, pitch, yaw, buttons, weapon);
    return true;
}

void M12aClient::resetSessionState() {
    if (m_clientNode) {
        m_clientNode->onDisconnectedCallback = nullptr;
        m_clientNode->disconnect(pse::LEAVE_USER_QUIT);
        m_clientNode.reset();
    }

    setState(ClientState::DISCONNECTED, "Session state explicitly purged");

    // Invalidate stale session identities
    m_assignedSlot = 0xFF;
    m_connectionGeneration = 0;
    m_localCommandSeq = 0;
    m_localClientTick = 0;
    m_hasReject = false;
    m_lastReject = pse::MsgReject{};

    M12aLogger::instance().logHandshake("Session state reset: local seq=0, tick=0, gen=0, slot=0xFF (no stale carryover)");
}

void M12aClient::setupClientCallbacks() {
    if (!m_clientNode) return;

    m_clientNode->onWelcomeCallback = [this](const pse::MsgWelcome& w) {
        m_sessionId = w.sessionId;
        m_assignedSlot = w.assignedSlot;
        m_connectionGeneration = w.connectionGeneration;
        m_reconnectKey = w.reconnectKey;
        m_currentConfig.reconnectKey = w.reconnectKey;
        m_mapEpoch = w.mapEpoch;
        m_mapName = w.mapName;
        m_serverTickRate = w.serverTickRate;
        m_serverSnapshotRate = w.serverSnapshotRate;

        std::ostringstream ss;
        ss << "AssignedSlot=P" << (int)(m_assignedSlot + 1)
           << " (id=" << (int)m_assignedSlot << ")"
           << " Gen=" << m_connectionGeneration
           << " SessionId=" << m_sessionId
           << " Map=" << m_mapName << " (Epoch " << m_mapEpoch << ")"
           << " TickRate=" << m_serverTickRate << "Hz"
           << " ReconnectKey=0x" << std::hex << m_reconnectKey << std::dec;

        setState(ClientState::AUTHENTICATED, ss.str());

        M12aLogger::instance().logHandshake("WELCOME: " + ss.str());

        double nowMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_epochTime).count();
        m_receivedMessages.push_back({ pse::MSG_WELCOME, 0, ss.str(), nowMs });
    };

    m_clientNode->onRejectCallback = [this](const pse::MsgReject& r) {
        m_lastReject = r;
        m_hasReject = true;

        std::ostringstream ss;
        ss << "Code=" << (int)r.reasonCode
           << " (" << pse::getRejectReasonString(static_cast<pse::RejectReason>(r.reasonCode))
           << "): " << r.reasonMessage;

        setState(ClientState::ERROR_STATE, ss.str());

        M12aLogger::instance().logHandshake("REJECT: " + ss.str());

        double nowMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_epochTime).count();
        m_receivedMessages.push_back({ pse::MSG_REJECT, 0, ss.str(), nowMs });
    };

    m_clientNode->onSnapshotCallback = [this](const pse::MsgPlayerSnapshot& snap) {
        double nowMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_epochTime).count();
        std::ostringstream ss;
        ss << "snapshotSeq=" << snap.snapshotSequence
           << " serverTick=" << snap.serverTick
           << " epoch=" << snap.mapEpoch
           << " playerCount=" << snap.players.size();

        if (snap.snapshotSequence % 30 == 0 || snap.snapshotSequence == 1) {
            M12aLogger::instance().logInboundMessage(pse::MSG_PLAYER_SNAPSHOT, snap.snapshotSequence, ss.str());
        }

        m_receivedMessages.push_back({ pse::MSG_PLAYER_SNAPSHOT, snap.snapshotSequence, ss.str(), nowMs });
    };

    m_clientNode->onDisconnectedCallback = [this]() {
        if (m_state.load() != ClientState::DISCONNECTING && m_state.load() != ClientState::DISCONNECTED) {
            setState(ClientState::DISCONNECTED, "Server closed session");
        }
    };

    m_clientNode->setCustomPacketHandler([this](uint8_t msgType, pse::ByteReader& /*reader*/, uint32_t seq) -> bool {
        double nowMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_epochTime).count();
        std::ostringstream ss;
        ss << "custom/extended type=0x" << std::hex << (int)msgType << std::dec;
        m_receivedMessages.push_back({ msgType, seq, ss.str(), nowMs });
        return true;
    });
}

size_t M12aClient::getCommandsSentWithGen(uint32_t gen) const {
    size_t count = 0;
    for (const auto& cmd : m_sentCommands) {
        if (cmd.connectionGeneration == gen) ++count;
    }
    return count;
}

size_t M12aClient::getForwardCommandsCount() const {
    size_t count = 0;
    for (const auto& cmd : m_sentCommands) {
        if (cmd.forward > 0.5f) ++count;
    }
    return count;
}

size_t M12aClient::getNeutralCommandsCount() const {
    size_t count = 0;
    for (const auto& cmd : m_sentCommands) {
        if (cmd.forward == 0.0f && cmd.right == 0.0f && cmd.buttons == 0) ++count;
    }
    return count;
}

bool M12aClient::hadExplicitReleaseCommand() const {
    for (size_t i = 1; i < m_sentCommands.size(); ++i) {
        if (m_sentCommands[i - 1].forward > 0.5f && m_sentCommands[i].forward == 0.0f) {
            return true;
        }
    }
    return false;
}

} // namespace pse::m12a
