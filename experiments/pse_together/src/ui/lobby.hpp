#pragma once
#include "protocol.hpp"
#include "enet_host.h"
#include "enet_client.h"
#include <chrono>
#include <memory>

namespace pse::ui {
inline std::uint64_t nowMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
// The endpoint is single-thread owned. Native integration must marshal copies
// through an owned mailbox; no engine pointers or untrusted sender identity.
class LobbyHost {
    std::unique_ptr<ENetHostServer> owned_;
    ENetHostServer& server_;
    Model model_;
    std::array<ChatLimit, Capacity> limits_{};
    std::uint64_t rosterSequence_{}, chatSequence_{}, lastPublishMs_{};
    std::uint32_t epoch_ = 1;
    std::uint32_t wireSequence_{};
    bool publish() {
        if (!server_.isRunning()) return false;
        Roster roster; roster.sequence = next(rosterSequence_); roster.epoch = epoch_;
        for (std::uint8_t slot = 0; slot < Capacity; ++slot) {
            const auto& p = server_.getSimulation().getSlot(slot);
            if (!p.connected) continue;
            auto state = State::Waiting;
            // No native state has been supplied: roster does not pretend mock
            // simulation health/position is authoritative KEX state.
            if (const auto& old = model_.players()[slot]; old && old->generation == p.connectionGeneration) state = old->state;
            std::uint16_t ping = 0;
            if (auto* peer = server_.getPeerForSlot(slot)) ping = static_cast<std::uint16_t>((std::min)(peer->roundTripTime, 65535u));
            roster.entries.push_back({slot,p.connectionGeneration,p.displayName,state,ping});
        }
        if (!roster.valid() || !model_.roster(roster.sequence, epoch_, roster.entries)) return false;
        server_.broadcastPacket(CHANNEL_RELIABLE_CONTROL, encodePacket(MSG_UI_ROSTER, ++wireSequence_, roster), true);
        lastPublishMs_ = nowMs(); return true;
    }
    bool receive(ENetPeer* peer, std::uint8_t type, ByteReader& reader) {
        if (type != MSG_CHAT_REQUEST) return false;
        std::uint8_t slot{}; std::uint32_t generation{}; Request request;
        if (!server_.getPeerSession(peer,slot,generation) || !complete(reader,request) ||
            request.generation != generation || !limits_[slot].accept(request.sequence,nowMs())) return true;
        emit(slot,generation,std::move(request.message)); return true;
    }
    bool emit(std::uint8_t slot, std::uint32_t generation, std::string message) {
        const auto& p = model_.players()[slot];
        if (!p || p->generation != generation || !chat(message)) return false;
        Broadcast b{{next(chatSequence_),slot,generation,p->displayName,std::move(message)}};
        if (!model_.message(b.line)) return false;
        server_.broadcastPacket(CHANNEL_RELIABLE_CONTROL,encodePacket(MSG_CHAT_BROADCAST,++wireSequence_,b),true);
        return true;
    }
    void bind(bool handler) {
        if(handler) server_.setCustomPacketHandler([this](ENetPeer* p,std::uint8_t t,ByteReader& r,std::uint32_t) { return receive(p,t,r); });
        const auto authenticated=server_.onClientAuthenticatedCallback;
        server_.onClientAuthenticatedCallback = [this,authenticated](ENetPeer* peer,std::uint8_t slot,std::uint32_t gen,bool reconnect) {
            if(authenticated) authenticated(peer,slot,gen,reconnect);
            limits_[slot] = {}; publish();
            // History follows the roster on the same reliable channel. Never
            // replay a line attributed to an obsolete connection generation.
            for (const auto& line : model_.messages()) {
                const auto& p = model_.players()[line.slot];
                if (!p || p->generation != line.generation || p->displayName != line.displayName) continue;
                server_.sendPacket(peer,CHANNEL_RELIABLE_CONTROL,
                    encodePacket(MSG_CHAT_BROADCAST,++wireSequence_,Broadcast{line}),true);
            }
        };
        // The base marks the simulation ledger disconnected before this callback.
        const auto ended=server_.onClientSessionEndedCallback;
        server_.onClientSessionEndedCallback = [this,ended](std::uint8_t slot,std::uint32_t gen) { if(ended)ended(slot,gen);publish(); };
    }
public:
    LobbyHost():owned_(std::make_unique<ENetHostServer>()),server_(*owned_) { bind(true); }
    // NETWORK owns the external server and this binding, retaining both through
    // stop/join. Existing gameplay callbacks are chained rather than replaced.
    explicit LobbyHost(ENetHostServer& server):server_(server) {
        model_.reset(0);epoch_=server.getSessionMapEpoch();bind(false);publish();
    }
    bool handle(ENetPeer* p,std::uint8_t t,ByteReader& r) { return receive(p,t,r); }
    bool nativePose(Pose p,State state) {
        if (p.slot>=Capacity || !model_.players()[p.slot] || model_.players()[p.slot]->generation!=p.generation ||
            p.epoch!=server_.getSessionMapEpoch() || !PosePacket{p}.valid()) return false;
        const bool changed=epoch_!=p.epoch || model_.players()[p.slot]->state!=state;
        if(changed) {
            epoch_=p.epoch;
            std::vector<Player> entries;
            for(const auto& player:model_.players()) if(player) { auto row=*player;if(row.slot==p.slot)row.state=state;entries.push_back(row); }
            if(!model_.roster(next(rosterSequence_),epoch_,entries)) return false;
            if(!publish())return false;
        }
        p.receivedMs=nowMs();
        if(!model_.pose(p))return false;
        server_.broadcastPacket(CHANNEL_UNRELIABLE_SEQUENCED,encodePacket(MSG_UI_POSE,++wireSequence_,PosePacket{p}),false);
        return true;
    }
    bool start(const Menu& menu, std::uint16_t actualPort = 0) {
        if (!owned_ || !menu.host || !menu.valid() || server_.isRunning()) return false;
        model_.reset(0); limits_ = {}; rosterSequence_ = chatSequence_ = lastPublishMs_ = 0; wireSequence_ = 0; epoch_ = 1;
        HostConfig c; c.port = actualPort ? actualPort : *port(menu.portText);
        c.bindAddress = menu.address; c.hostName = menu.displayName; c.joinToken = menu.token;
        c.bridgeMode = true; c.rejectedPeerTimeoutMs = 1000;
        if (!server_.start(c)) return false;
        server_.setSessionMap("LOBBY",epoch_,{});
        return publish();
    }
    void pollUi() { if (nowMs() - lastPublishMs_ >= 1000) publish(); }
    void poll() { server_.poll(0);pollUi(); }
    bool say(std::string message) {
        return server_.isRunning() && chat(message) && limits_[0].accept(limits_[0].lastSequence+1,nowMs()) && emit(0,1,std::move(message));
    }
    void stop() { if(owned_)server_.stop(); model_.reset(0); }
    const Model& view() const { return model_; }
    // Test seam / future gameplay binder. This endpoint does not start KEX.
    ENetHostServer& transport() { return server_; }
};
class LobbyClient {
    std::unique_ptr<ENetClientNode> owned_;
    ENetClientNode& client_;
    Model model_;
    std::uint64_t sequence_{};
    std::uint32_t wireSequence_{};
    std::string error_;
    bool connecting_{};
    bool terminal_{};
    std::string identityScope_;
    void bind(bool handler) {
        const auto welcome=client_.onWelcomeCallback;
        client_.onWelcomeCallback = [this,welcome](const MsgWelcome& w) { connecting_ = false; model_.reset(w.assignedSlot); sequence_ = 0; error_.clear();if(welcome)welcome(w); };
        const auto rejected=client_.onRejectCallback;
        client_.onRejectCallback = [this,rejected](const MsgReject& r) { terminal_ = true; connecting_ = false; error_ = r.reasonMessage; model_.reset();if(rejected)rejected(r); };
        const auto disconnected=client_.onDisconnectedCallback;
        client_.onDisconnectedCallback = [this,disconnected]() { terminal_ = true; connecting_ = false; model_.reset(); error_ = "Disconnected";if(disconnected)disconnected(); };
        if(handler) client_.setCustomPacketHandler([this](std::uint8_t t,ByteReader& r,std::uint32_t) { return handle(t,r); });
    }
public:
    LobbyClient():owned_(std::make_unique<ENetClientNode>()),client_(*owned_) {bind(true);}
    explicit LobbyClient(ENetClientNode& client):client_(client) {bind(false);}
    bool handle(std::uint8_t t,ByteReader& r) {
            if (t == MSG_UI_ROSTER) { Roster v; if (complete(r,v)) model_.roster(v.sequence,v.epoch,v.entries); return true; }
            if (t == MSG_CHAT_BROADCAST) { Broadcast v; if (complete(r,v)) model_.message(std::move(v.line)); return true; }
            if (t == MSG_UI_POSE) { PosePacket v;if(complete(r,v)) {v.pose.receivedMs=nowMs();model_.pose(v.pose);} return true; }
            return false;
    }
    bool connect(const Menu& menu, const std::string& participant) {
        if (!owned_ || menu.host || !menu.valid() || !ui::participant(participant) || connecting_ || client_.isConnected()) return false;
        model_.reset(); error_.clear(); wireSequence_ = 0;
        const auto scope = menu.address+":"+menu.portText+":"+participant;
        if (scope != identityScope_) client_.clearReconnectKey();
        identityScope_ = scope;
        ClientConfig c; c.hostAddress = menu.address; c.hostPort = *port(menu.portText);
        c.joinToken = menu.token; c.displayName = menu.displayName; c.participantId = participant; c.bridgeMode = true;
        connecting_ = client_.connect(c); return connecting_;
    }
    void poll() {
        client_.poll(0);
        // Destroy the terminal transport only after its receive loop returns.
        if (terminal_ && owned_) { client_.disconnect(); terminal_ = false; }
    }
    bool say(std::string message) {
        if (!client_.isAuthenticated() || !chat(message)) return false;
        Request r{next(sequence_),client_.getConnectionGeneration(),std::move(message)};
        client_.sendPacket(CHANNEL_RELIABLE_CONTROL,encodePacket(MSG_CHAT_REQUEST,++wireSequence_,r),true); return true;
    }
    void stop() { if(owned_)client_.disconnect(); connecting_ = terminal_ = false; model_.reset(); }
    const Model& view() const { return model_; }
    const std::string& error() const { return error_; }
    ENetClientNode& transport() { return client_; }
};
} // namespace pse::ui
