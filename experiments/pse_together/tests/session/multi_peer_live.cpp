// Host Session with SEVEN real ENet clients over loopback in one process (host = slot 0, clients = slots 1..7).
// Clients are pse::ENetWorker CLIENT workers with their own ThreadBridge; the test thread is the simulation thread of
// every bridge. No KEX, no native code. Checks that nothing in the host session layer assumes a single peer.
#include "network_input.hpp"
#include "native_map111.hpp"
#include <windows.h>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>
using namespace pse;
namespace {
unsigned checks{}, failures{};
void check(bool ok, const char* name) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", name); } }
std::vector<std::string> journal;
void logger(const char* event, const char* fmt, ...) {
    char buf[1024];
    va_list va; va_start(va, fmt); std::vsnprintf(buf, sizeof buf, fmt, va); va_end(va);
    journal.push_back(std::string(event) + " {" + buf + "}");
}
unsigned countEvent(const char* name) {
    unsigned n = 0;
    for (const auto& line : journal) if (line.rfind(name, 0) == 0) ++n;
    return n;
}
using Clock = std::chrono::steady_clock;
constexpr unsigned Peers = 7;
constexpr std::uint32_t Epoch = 0x91070001;
const map56::Identity Map{"maps/TOMB.MAP", 0x1234, 1, 71, 28608}; // numeric fixture, not a live map witness

struct Client {
    std::string id;
    std::uint16_t port{};
    std::unique_ptr<ThreadBridge> bridge;
    std::unique_ptr<ENetWorker> worker;
    unsigned slot{};
    std::uint32_t generation{}, epoch{};
    std::uint64_t key{};
    bool joined{}, lost{}, mapReadySent{}, baselineSeen{};
    int rejected{-1};
    std::uint32_t inputSeq{};
    std::vector<BridgePlayerSnapshot> snapshots;
    std::vector<BridgeWorldEvent> events;
    std::vector<BridgeMapChange> orders, rebinds; // rebinds: MapChange with a new epoch (the host's next world inside the session)
    map56::Identity world = Map;
    unsigned worldSnapshots{}, baselines{};
    std::uint32_t lastBaselineEpoch{};
    void start(const std::string& participant, std::uint16_t hostPort, std::uint64_t reconnectKey = 0) {
        id = participant; port = hostPort;
        if (bridge) { worker.reset(); bridge->unregisterSimulationThread(); }
        bridge = std::make_unique<ThreadBridge>();
        bridge->registerSimulationThread();
        worker = std::make_unique<ENetWorker>(*bridge, ENetWorkerMode::CLIENT);
        worker->setNativePlayerSnapshots(true);
        ClientConfig cc; cc.hostPort = hostPort; cc.joinToken = "M12A_Loopback"; cc.participantId = participant;
        cc.displayName = participant.substr(0, 20); cc.reconnectKey = reconnectKey;
        joined = lost = mapReadySent = baselineSeen = false; rejected = -1; slot = 0; generation = 0; inputSeq = 0;
        snapshots.clear(); events.clear(); orders.clear(); rebinds.clear(); worldSnapshots = 0; baselines = 0;
        check(worker->startClient(cc), "client worker starts");
    }
    void pump() {
        BridgeMessage m;
        while (bridge->pollInbound(m)) {
            if (const auto* e = std::get_if<BridgeSessionEvent>(&m)) {
                if (e->type == BridgeSessionEvent::Type::CLIENT_JOINED || e->type == BridgeSessionEvent::Type::RECONNECTED) {
                    joined = true; slot = e->slotId; generation = e->connectionGeneration; key = e->reconnectKey; epoch = e->mapEpoch;
                } else if (e->type == BridgeSessionEvent::Type::REJECTED) rejected = e->reasonCode;
                else if (e->type == BridgeSessionEvent::Type::LOCAL_SESSION_LOST) lost = true;
            } else if (const auto* s = std::get_if<BridgePlayerSnapshot>(&m)) snapshots.push_back(*s);
            else if (const auto* w = std::get_if<BridgeWorldEvent>(&m)) events.push_back(*w);
            else if (const auto* c = std::get_if<BridgeMapChange>(&m)) { if (c->newMapEpoch == 0) orders.push_back(*c); else if (c->newMapEpoch != epoch) { rebinds.push_back(*c); epoch = c->newMapEpoch; } }
            else if (const auto* b = std::get_if<BridgeWorldBaseline>(&m)) { baselineSeen = true; ++baselines; lastBaselineEpoch = b->mapEpoch; }
            else if (std::get_if<BridgeWorldSnapshot>(&m)) ++worldSnapshots;
        }
    }
    void sendMapReady() {
        BridgeMapReady r; r.slotId = static_cast<std::uint8_t>(slot); r.mapEpoch = epoch; r.connectionGeneration = generation;
        r.mapName = world.name; r.mapStructure = map111::structure(world);
        mapReadySent = isSuccess(bridge->pushOutbound(BridgeMessage{r}));
    }
    void sendInput(std::uint32_t buttons, float forward, float x, float yaw, std::uint8_t weapon) {
        BridgeInputCommand c; c.slotId = static_cast<std::uint8_t>(slot); c.connectionGeneration = generation; c.sequence = ++inputSeq;
        c.buttons = buttons; c.moveForward = forward; c.posX = x; c.posY = x + 1; c.posZ = 5; c.sector = static_cast<std::int16_t>(slot);
        c.yaw = yaw; c.selectedWeapon = weapon;
        bridge->pushOutbound(BridgeMessage{c});
    }
    void stop() { if (worker) worker->stop(); }
    ~Client() { stop(); if (bridge) bridge->unregisterSimulationThread(); }
};

struct World {
    std::unique_ptr<m12a::Session> host;
    std::vector<std::unique_ptr<Client>> clients;
    std::uint64_t tick{};
    bool gameplay{true}; // false: the host is between worlds (loading, map screen)
    // One simulation-thread step: tick the host session, pump every client.
    void step() {
        host->tick(++tick, gameplay);
        for (auto& c : clients) c->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    template<class P> bool until(P done, int ms = 8000) {
        const auto end = Clock::now() + std::chrono::milliseconds(ms);
        do { step(); if (done()) return true; } while (Clock::now() < end);
        return done();
    }
    void settle(int ms) { const auto end = Clock::now() + std::chrono::milliseconds(ms); do step(); while (Clock::now() < end); }
};

BridgePlayerSnapshot snapshotFor(unsigned recipient, std::uint32_t sequence, std::uint32_t tick) {
    BridgePlayerSnapshot s;
    s.slotId = 0; s.entityId = 0x7000; s.entityGeneration = 1; s.sequence = sequence; s.serverTick = tick;
    s.posX = 1000.0f + recipient; s.posY = 2.0f; s.posZ = 3.0f;
    s.stateTimeNs = 1000 + sequence; s.resetToken = 1; s.discontinuitySequence = 1;
    return s;
}
} // namespace

int main() {
    World w;
    // Generous input timeout so ordinary scheduling jitter cannot blank a peer mid-check; the expiry phase below still proves it.
    std::uint16_t port = 0;
    for (unsigned i = 0; i < 16 && !port; ++i) {
        const auto candidate = static_cast<std::uint16_t>(31000 + (GetCurrentProcessId() % 20000) + i);
        w.host = std::make_unique<m12a::Session>(logger, m12a::InputTimeout{40, 400});
        if (!w.host->configureNativeMap(Map, Epoch)) { check(false, "configureNativeMap"); return 1; }
        if (w.host->start(candidate)) port = candidate;
    }
    check(port != 0, "host session starts on loopback");
    if (!port) return 1;
    auto& host = *w.host;
    check(host.connected() == 0 && !host.anyWantsPlayer() && !host.anySnapshotEligible(), "empty host has no peers");
    for (unsigned s = 0; s < 9; ++s) check(!host.wantsPlayer(s) && host.nativeButtons(s) == 0 && host.generation(s) == 0 &&
        host.participantKey(s).empty() && !host.snapshotEligible(s), "no peer state before anybody joins");
    for (unsigned s = 1; s < m12a::Session::MaxSlots; ++s)
        check(host.mapGate(s).world() == map56::HostGate::World::Verified && host.mapGate(s).mapEpoch() == Epoch, "every peer gate bound to the same epoch");

    // ---- 1. seven clients join at once --------------------------------------------------------------------------
    for (unsigned i = 1; i <= Peers; ++i) {
        w.clients.push_back(std::make_unique<Client>());
        w.clients.back()->start("p91_client_" + std::to_string(i), port);
    }
    check(w.until([&] { for (auto& c : w.clients) if (!c->joined) return false; return true; }), "all seven clients authenticate");
    bool slots[8]{};
    bool distinct = true, gens = true;
    for (auto& c : w.clients) {
        if (c->slot < 1 || c->slot > 7 || slots[c->slot]) distinct = false; else slots[c->slot] = true;
        if (!c->generation || c->epoch != Epoch) gens = false;
    }
    check(distinct, "seven distinct slots 1..7");
    check(gens, "each client got a connection generation and the session epoch in WELCOME");
    check(w.until([&] { return host.generation(1) && host.generation(2) && host.generation(3) && host.generation(4) && host.generation(5) && host.generation(6) && host.generation(7); }),
        "host session sees every join");
    for (auto& c : w.clients) {
        check(host.generation(c->slot) == c->generation, "host generation equals the client's WELCOME generation");
        check(host.participantKey(c->slot) == c->id, "participantKey carries the client's join participantId");
        check(!host.wantsPlayer(c->slot) && !host.snapshotEligible(c->slot), "joined but MapReady pending: not wanted, not eligible");
    }

    // ---- 2. an eighth client is refused with SERVER_FULL, nobody else is disturbed -------------------------------------
    {
        Client eighth;
        eighth.start("p91_client_8", port);
        check(w.until([&] { eighth.pump(); return eighth.rejected >= 0 || eighth.joined; }, 8000), "eighth client gets an answer");
        check(!eighth.joined && eighth.rejected == REJECT_SERVER_FULL, "eighth client refused: SERVER_FULL");
        for (auto& c : w.clients) check(host.generation(c->slot) == c->generation && host.participantKey(c->slot) == c->id, "refused 8th did not touch an existing peer");
    }

    // ---- 3. MapReady: six ready, the seventh stays silent and is kicked alone by the join timeout ------------------
    Client& lazy = *w.clients[6];
    for (unsigned i = 0; i < 6; ++i) w.clients[i]->sendMapReady();
    check(w.until([&] { for (unsigned i = 0; i < 6; ++i) if (!host.snapshotEligible(w.clients[i]->slot)) return false; return true; }),
        "six peers complete MapReady and become snapshot-eligible");
    check(host.connected() == 6 && !host.snapshotEligible(lazy.slot) && !host.wantsPlayer(lazy.slot), "the silent peer is not eligible");
    check(w.until([&] { for (unsigned i = 0; i < 6; ++i) if (!w.clients[i]->baselineSeen) return false; return true; }), "each ready peer got its own baseline");
    check(!lazy.baselineSeen, "the silent peer got no baseline");
    const auto serialBefore = host.joinSerial();
    check(serialBefore == 6, "joinSerial counts every ready peer");
    const std::uint32_t lazyGen = lazy.generation;
    const unsigned lazySlot = lazy.slot;
    w.tick += 700; // the join timeout is counted in simulation ticks
    check(w.until([&] { return lazy.lost; }), "the peer that never sent MapReady is kicked");
    check(countEvent("network_resync") >= 1, "single-slot recovery logged");
    check(!host.wantsPlayer(lazySlot), "kicked peer is gone from the host");
    for (unsigned i = 0; i < 6; ++i) {
        const auto& c = *w.clients[i];
        check(!c.lost && host.snapshotEligible(c.slot) && host.generation(c.slot) == c.generation, "kicking one peer leaves the others eligible with their generation");
    }
    // The kicked peer rejoins its reserved slot with its secret key: fresh generation, then MapReady.
    lazy.stop();
    const auto lazyKey = lazy.key;
    lazy.start("p91_client_7", port, lazyKey);
    check(w.until([&] { return lazy.joined; }), "kicked peer rejoins with its reconnect key");
    check(lazy.slot == lazySlot && lazy.generation == lazyGen + 1 && lazy.key == lazyKey, "same slot, next generation, same key");
    lazy.sendMapReady();
    check(w.until([&] { return host.snapshotEligible(lazy.slot); }), "rejoined peer completes MapReady");
    check(host.connected() == 7 && host.anySnapshotEligible() && host.joinSerial() == serialBefore + 1, "all seven eligible; joinSerial advanced once");
    for (auto& c : w.clients) check(host.mapGate(c->slot).peer() == map56::HostGate::Peer::MapReadyAccepted, "every gate promoted");

    // ---- 4. per-slot input reaches only its slot -----------------------------------------------------------------------
    constexpr std::uint32_t NativeAttack = 1u << 0, NativeJump = 1u << 1;
    const auto wire = [](unsigned slot) { return slot % 2 ? BUTTON_ATTACK : BUTTON_JUMP; };
    const auto native = [&](unsigned slot) { return slot % 2 ? NativeAttack : NativeJump; };
    bool crossTalk = false;
    bool seen[8]{};
    const auto feedAndObserve = [&] {
        for (auto& c : w.clients) c->sendInput(wire(c->slot), 0.0f, 100.0f * c->slot, 0.1f * c->slot, static_cast<std::uint8_t>(c->slot));
    };
    const bool inputOk = w.until([&] {
        feedAndObserve();
        bool all = true;
        for (unsigned s = 1; s <= Peers; ++s) {
            const auto b = host.nativeButtons(s);
            const auto& p = host.pose(s);
            if (b && b != native(s)) crossTalk = true;
            if (p.valid && (p.x != 100.0f * s || p.sector != static_cast<int>(s))) crossTalk = true;
            if (host.selectedWeapon(s) != 0xff && host.selectedWeapon(s) != s) crossTalk = true;
            if (b == native(s) && p.valid && p.x == 100.0f * s && host.selectedWeapon(s) == s && host.sequence(s) && host.acceptedAt(s)) seen[s] = true;
            all = all && seen[s];
        }
        return all;
    });
    check(inputOk, "every slot's own buttons, pose and weapon arrive on that slot");
    check(!crossTalk, "no slot ever showed another slot's input");
    check(host.takeNativeEdges(0) == 0 && host.takeNativeEdges(9) == 0, "no edges for non-peer slots");
    for (unsigned s = 1; s <= Peers; ++s) host.takeNativeEdges(s); // drain the attack/jump press edges of the first inputs
    check(host.takeNativeEdges(1) == 0, "edges are consumed once");
    // A rising edge on one client is delivered to that slot's edge latch only.
    {
        auto& c = *w.clients[2];
        const unsigned slot = c.slot;
        c.sendInput(0, 0, 100.0f * slot, 0.1f * slot, static_cast<std::uint8_t>(slot));
        c.sendInput(BUTTON_USE, 0, 100.0f * slot, 0.1f * slot, static_cast<std::uint8_t>(slot));
        std::uint32_t edgesMine = 0, edgesOthers = 0;
        w.until([&] {
            feedAndObserve();
            edgesMine |= host.takeNativeEdges(slot);
            for (unsigned s = 1; s <= Peers; ++s) if (s != slot) edgesOthers |= host.takeNativeEdges(s);
            return edgesMine != 0;
        });
        constexpr std::uint32_t NativeUse = 1u << 10;
        check((edgesMine & NativeUse) && !(edgesOthers & NativeUse), "a use-press edge latches on its own slot only");
    }
    // input of a stale generation / unknown peer is not accepted: neutral input keeps the slot at its last value
    check(host.sequence(w.clients[0]->slot) > 0, "sequence tracked per slot");

    // ---- 5. unicast native snapshot reaches exactly its recipient -------------------------------------------------------
    std::uint32_t snapSeq = 100;
    check(!host.publishNativeSnapshot(0, snapshotFor(0, ++snapSeq, 1)) && !host.publishNativeSnapshot(8, snapshotFor(8, ++snapSeq, 1)),
        "no snapshot to slot 0 or an out-of-range slot");
    const bool snapOk = w.until([&] {
        feedAndObserve();
        bool all = true;
        for (auto& c : w.clients) {
            if (c->snapshots.empty()) { host.publishNativeSnapshot(c->slot, snapshotFor(c->slot, ++snapSeq, static_cast<std::uint32_t>(w.tick))); all = false; }
        }
        return all;
    });
    check(snapOk, "every client receives a unicast native snapshot");
    w.settle(150);
    bool snapWrong = false;
    for (auto& c : w.clients) {
        check(!c->snapshots.empty(), "client has snapshots");
        for (const auto& s : c->snapshots)
            if (s.posX != 1000.0f + c->slot || s.connectionGeneration != c->generation) snapWrong = true;
    }
    check(!snapWrong, "each client only ever got the snapshot addressed to it, stamped with its own generation");

    // ---- 6. world event / world snapshot / next map order reach all seven ---------------------------------------------
    check(host.publishWorldEvent(7, 11, 22, 33), "world event accepted with peers eligible");
    check(w.until([&] { for (auto& c : w.clients) if (c->events.empty()) return false; return true; }), "world event reaches all seven");
    w.settle(100);
    std::uint32_t evSeq = w.clients[0]->events[0].eventSequence;
    bool evOk = true;
    for (auto& c : w.clients) evOk = evOk && c->events.size() == 1 && c->events[0].eventSequence == evSeq && c->events[0].eventType == 7 &&
        c->events[0].targetEntityId == 22 && c->events[0].eventValue == 33;
    check(evOk, "exactly one event, one shared sequence, same payload everywhere");
    std::vector<EntitySnapshot> ents(1);
    ents[0].entityId = 5; ents[0].type = 2;
    check(host.publishWorldSnapshot(std::move(ents)), "world snapshot accepted");
    check(w.until([&] { for (auto& c : w.clients) if (!c->worldSnapshots) return false; return true; }), "world snapshot reaches all seven");
    check(!host.announceNextMap("bad name\"") && !host.announceNextMap(""), "invalid next-map names are refused");
    check(host.announceNextMap("maps/NEXT.MAP"), "next-map order accepted");
    check(w.until([&] { for (auto& c : w.clients) if (c->orders.empty()) return false; return true; }), "next-map order reaches all seven");
    w.settle(100);
    bool orderOk = true;
    for (auto& c : w.clients) orderOk = orderOk && c->orders.size() == 1 && c->orders[0].mapName == "maps/NEXT.MAP";
    check(orderOk, "exactly one order per client");
    check(countEvent("network_next_map_announced") == Peers, "one announce log line per slot");

    // ---- 7. input expiry is per slot --------------------------------------------------------------------------------------
    {
        const unsigned keep = w.clients[3]->slot;
        bool othersNeutral = false, keepAlive = false;
        w.until([&] {
            auto& k = *w.clients[3];
            k.sendInput(wire(keep), 0, 100.0f * keep, 0.1f * keep, static_cast<std::uint8_t>(keep));
            othersNeutral = true;
            for (unsigned s = 1; s <= Peers; ++s) if (s != keep && host.nativeButtons(s) != 0) othersNeutral = false;
            keepAlive = keepAlive || host.nativeButtons(keep) == native(keep);
            return othersNeutral && keepAlive;
        });
        check(othersNeutral, "silent peers go neutral by themselves (input timeout is per slot)");
        check(keepAlive, "the peer that keeps sending is not blanked by the others' timeouts");
        check(countEvent("network_input_expired") >= 1, "expiry logged with its slot");
    }

    // ---- 8. one client leaves and rejoins; nobody else is disturbed ----------------------------------------------------------
    {
        Client& leaver = *w.clients[1];
        const unsigned slot = leaver.slot;
        const std::uint32_t gen = leaver.generation;
        const auto key = leaver.key;
        const std::string id = leaver.id;
        std::uint32_t others[8]{};
        for (auto& c : w.clients) others[c->slot] = c->generation;
        const auto serial = host.joinSerial();
        leaver.stop();
        check(w.until([&] { return !host.wantsPlayer(slot); }), "leaver disappears from the host");
        check(host.participantKey(slot).empty() && !host.snapshotEligible(slot) && host.connected() == 6, "leaver slot reports not connected");
        check(!host.publishNativeSnapshot(slot, snapshotFor(slot, ++snapSeq, 1)), "no snapshot to a slot that left");
        for (auto& c : w.clients) if (c->slot != slot)
            check(host.snapshotEligible(c->slot) && host.generation(c->slot) == others[c->slot], "others keep eligibility and generation while one leaves");
        // A brand-new identity cannot take the slot of a disconnected, reserved participant: the server is full.
        {
            Client stranger;
            stranger.start("p91_stranger", port);
            check(w.until([&] { stranger.pump(); return stranger.rejected >= 0 || stranger.joined; }), "stranger gets an answer");
            check(!stranger.joined && stranger.rejected == REJECT_SERVER_FULL, "reserved slots are not handed to a new identity");
        }
        leaver.start(id, port, key);
        check(w.until([&] { return leaver.joined; }), "leaver rejoins");
        check(leaver.slot == slot && leaver.generation == gen + 1, "slot reused, generation is fresh");
        check(w.until([&] { return host.generation(slot) == gen + 1 && host.participantKey(slot) == id; }), "host session picked up the new generation");
        check(!host.wantsPlayer(slot), "rejoined peer must complete MapReady again");
        leaver.sendMapReady();
        check(w.until([&] { return host.snapshotEligible(slot); }), "rejoined peer eligible again");
        check(host.joinSerial() == serial + 1, "joinSerial advanced for the rejoin");
        for (auto& c : w.clients) if (c->slot != slot) check(host.generation(c->slot) == others[c->slot] && !c->lost, "others' generations never moved");
        // old-generation input from before the leave must not be accepted: only new-generation commands reach the slot
        leaver.sendInput(BUTTON_ATTACK, 0, 777.0f, 0, 1);
        check(w.until([&] { leaver.sendInput(BUTTON_ATTACK, 0, 777.0f, 0, 1); return host.pose(slot).valid && host.pose(slot).x == 777.0f; }), "new generation input accepted on the reused slot");
    }

    // ---- 9. one session for the whole game: the host's next world (StateExit -> next level) keeps every client connected -----------
    {
        constexpr std::uint32_t Epoch2 = 0x91070002;
        const map56::Identity Map2{"maps/KARNAK.MAP", 0x5678, 1, 93, 31337};
        std::uint32_t worldGens[8]{};
        for (auto& c : w.clients) worldGens[c->slot] = c->generation;
        const auto serial = host.joinSerial();
        host.leaveWorld("StateExit");
        check(host.connected() == 0 && host.activePeers() == 7 && !host.anySnapshotEligible(), "world left: nobody ready, everybody still connected");
        check(!host.publishWorldEvent(1, 0, 0, 1) && !host.publishNativeSnapshot(w.clients[0]->slot, snapshotFor(w.clients[0]->slot, ++snapSeq, 1)), "nothing is published between worlds");
        // A long stay on the map screen / a slow load: far beyond the 600-tick join timeout, nobody is kicked.
        w.gameplay = false;
        for (int i = 0; i < 900; ++i) { w.tick += 1; w.step(); }
        bool kept = true;
        for (auto& c : w.clients) kept = kept && !c->lost && host.generation(c->slot) == worldGens[c->slot];
        check(kept, "no client is kicked or disconnected while the host is between worlds");
        // The next world is stable: re-bind with a new epoch, every client is told inside its existing connection.
        check(!host.rebindNativeMap(Map2, host.mapEpoch()), "re-binding to the same epoch is refused");
        w.gameplay = true;
        check(host.rebindNativeMap(Map2, Epoch2) && host.mapEpoch() == Epoch2 && host.worldBindings() == 1, "host session re-bound to the next world");
        check(w.until([&] { for (auto& c : w.clients) if (c->rebinds.empty()) return false; return true; }), "every client receives the new world binding");
        bool sameConn = true, newWorld = true;
        for (auto& c : w.clients) {
            sameConn = sameConn && c->generation == worldGens[c->slot] && !c->lost;
            newWorld = newWorld && c->rebinds.size() == 1 && c->rebinds[0].newMapEpoch == Epoch2 && c->rebinds[0].mapName == Map2.name;
        }
        check(sameConn, "same connection generation for every client across the world change");
        check(newWorld, "exactly one MapChange with the new epoch and map name per client");
        for (auto& c : w.clients) check(!host.wantsPlayer(c->slot) && host.mapGate(c->slot).peer() == map56::HostGate::Peer::AwaitMapReady, "each peer must become ready again");
        // A MapReady of the old world is refused; the new one makes the peer ready again.
        {
            auto& c = *w.clients[0];
            BridgeMapReady old; old.slotId = static_cast<std::uint8_t>(c.slot); old.mapEpoch = Epoch; old.connectionGeneration = c.generation;
            old.mapName = Map.name; old.mapStructure = map111::structure(Map);
            c.bridge->pushOutbound(BridgeMessage{old});
            w.settle(150);
            check(!host.wantsPlayer(c.slot), "MapReady of the previous world is refused");
        }
        for (auto& c : w.clients) { c->world = Map2; c->baselineSeen = false; c->sendMapReady(); }
        check(w.until([&] { for (auto& c : w.clients) if (!host.snapshotEligible(c->slot)) return false; return true; }), "every client ready in the next world");
        check(w.until([&] { for (auto& c : w.clients) if (!c->baselineSeen || c->lastBaselineEpoch != Epoch2) return false; return true; }), "each client got the baseline of the new world");
        check(host.joinSerial() == serial + 7, "joinSerial advanced once per client (shared world state is re-sent)");
        // Snapshots of the new epoch pass both workers' epoch filters (host session map, client node epoch).
        for (auto& c : w.clients) c->snapshots.clear();
        const bool snap2 = w.until([&] {
            bool all = true;
            for (auto& c : w.clients) if (c->snapshots.empty()) { host.publishNativeSnapshot(c->slot, snapshotFor(c->slot, ++snapSeq, static_cast<std::uint32_t>(w.tick))); all = false; }
            return all;
        });
        check(snap2, "native snapshots of the new world reach every client");
        bool epochOk = true;
        for (auto& c : w.clients) for (const auto& s : c->snapshots) epochOk = epochOk && s.mapEpoch == Epoch2;
        check(epochOk, "every delivered snapshot carries the new epoch");
        auto& c0 = *w.clients[0];
        check(w.until([&] { c0.sendInput(BUTTON_JUMP, 0, 4321.0f, 0, 1); return host.pose(c0.slot).valid && host.pose(c0.slot).x == 4321.0f; }), "input accepted in the new world");
        // The client's own world restarts while the host's does not: same epoch MapReady again = re-ready (baseline again).
        const auto before = c0.baselines;
        const auto serial2 = host.joinSerial();
        c0.sendMapReady();
        check(w.until([&] { return c0.baselines > before; }), "re-ready in the same world gets a fresh baseline");
        check(host.joinSerial() == serial2 + 1 && host.snapshotEligible(c0.slot) && countEvent("network_reready") >= 1, "re-ready re-sends the shared world state and keeps the peer eligible");
        // A client that drops and comes back after the world change gets the new world in its WELCOME.
        auto& r = *w.clients[4];
        const auto rslot = r.slot; const auto rkey = r.key; const auto rid = r.id; const auto rgen = r.generation;
        r.stop();
        check(w.until([&] { return !host.wantsPlayer(rslot); }), "dropped client leaves");
        r.start(rid, port, rkey);
        check(w.until([&] { return r.joined; }), "dropped client reconnects");
        check(r.slot == rslot && r.generation == rgen + 1 && r.epoch == Epoch2, "reconnect: same slot, next generation, WELCOME carries the current world");
    }

    // ---- 10. shutdown --------------------------------------------------------------------------------------------------------
    host.stop("test_done");
    check(!host.running() && host.connected() == 0, "session stopped, no peer wanted");
    for (auto& c : w.clients) c->stop();
    std::printf("MULTI_PEER_LIVE checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
