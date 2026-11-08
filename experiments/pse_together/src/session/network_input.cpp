#include "network_input.hpp"
#include <windows.h>
#include <cmath>
#include "snapshot_contract.hpp"
#ifdef PSE_FEATURE_P4
#include "native_map111.hpp"
#endif

namespace m12a {
namespace {
constexpr std::size_t OutboxCap = 8192;
// Diagnostic control domain, not a claim to replicate or load a KEX map remotely.
constexpr char ControlDomain[] = "M12A_INPUT_ONLY";
}
bool Session::configureNativeSnapshots(std::uint32_t levelEpoch) {
    if(nativeMap)return false;
    return configureSnapshots(snapshot55::Domain,levelEpoch);
}
bool Session::configureNativeMap(const map56::Identity& identity,std::uint32_t levelEpoch) {
    if(started || stopped || nativeSnapshots || !gate.bind(identity,levelEpoch))return false;
    nativeMap=true;nativeDomain=identity.name;
#ifdef PSE_FEATURE_P4
    worker.setInitialSessionMap(identity.name,levelEpoch,map111::structure(identity));
#endif
    return configureSnapshots(nativeDomain,levelEpoch);
}
const char* Session::domain() const { return nativeMap ? nativeDomain.c_str() : nativeSnapshots ? snapshot55::Domain : ControlDomain; }
void Session::verifyNativeMap(map56::Fault fault,const map56::Identity& current) {
    bridge.assertSimulationThread();
    if(!nativeMap || gate.world()!=map56::HostGate::World::Verified)return;
    if(gate.verify(fault,current))return;
    // Terminal for this binding: no READY, snapshot or P2 eligibility until a new contract.
    log("map_identity_invalidated","\"fault\":\"%s\",\"bound\":\"%s\",\"observed\":\"%s\",\"epoch\":%u,\"generation\":%u,\"tick\":%llu,\"ready_before\":%s",
        map56::text(fault),gate.bound().name.c_str(),current.name.c_str(),epoch,gen,nowTick,ready ? "true" : "false");
    if(ready)neutral("map_identity_invalidated");
    ready=false;
}
bool Session::snapshotEligible() const {
    return running() && wantsPlayer() && levelReady && (!nativeMap || gate.snapshotEligible(gen,levelReady));
}
bool Session::configureSnapshots(const std::string& name,std::uint32_t levelEpoch) {
    if(started || stopped || !levelEpoch)return false;
    epoch=levelEpoch;nativeSnapshots=true;worker.setNativePlayerSnapshots(true);
    // startHost already applies this stored map before launching NETWORK, so a
    // very early WELCOME cannot advertise the old diagnostic domain/epoch.
#ifdef PSE_FEATURE_P4
    worker.setInitialSessionMap(name,epoch,map111::structure(gate.bound()));
#else
    worker.setInitialSessionMap(name,epoch);
#endif
    worker.setPlayerSnapshotObserver([this](bool sending,const pse::BridgePlayerSnapshot& s,std::size_t bytes) {
        log(sending ? "snapshot_worker_send" : "snapshot_worker_receive",
            "\"slot\":%u,\"snapshot_seq\":%u,\"server_tick\":%u,\"map_epoch\":%u,\"connection_generation\":%u,\"entity_id\":%u,\"entity_generation\":%u,\"bytes\":%zu,\"opcode\":32,\"channel\":1,\"kex_reads\":0",
            s.slotId,s.sequence,s.serverTick,s.mapEpoch,s.connectionGeneration,s.entityId,s.entityGeneration,bytes);
    });
    return true;
}
bool Session::publishNativeSnapshot(pse::BridgePlayerSnapshot snapshot) {
    bridge.assertSimulationThread();
    if(!nativeSnapshots || !snapshotEligible())return false;
    snapshot.targetSlot=1;snapshot.connectionGeneration=gen;snapshot.mapEpoch=epoch;
    // Best effort never enters the reliable event outbox or causes a resync.
    return pse::isSuccess(bridge.pushOutbound(pse::BridgeMessage{std::move(snapshot)}));
}
bool Session::start(std::uint16_t port) {
    bridge.registerSimulationThread();
    pse::HostConfig config;
    config.bindAddress = "127.0.0.1";
    config.port = port;
    config.maxClients = 1;
    config.maxClientSlots = 1;
    config.rejectedPeerTimeoutMs = 250;
    config.joinToken = "M12A_Loopback";
    if (!worker.startHost(config)) {
        bridge.unregisterSimulationThread();
        stopped = true;
        log("network_start_failed", "\"port\":%u", port);
        return false;
    }
    started = true;
    pse::BridgeMapChange map;
    map.mapName = domain();
    map.newMapEpoch = epoch;
#ifdef PSE_FEATURE_P4
    map.mapStructure=map111::structure(gate.bound());
#endif
    enqueue(std::move(map));
    flush();
#ifdef PSE_FEATURE_P4
    log("network_started", "\"port\":%u,\"max_clients\":1,\"protocol\":273,\"thread_checks\":true,\"headless_control_domain\":true,\"timeout_ticks\":%llu,\"timeout_wall_ms\":%llu", port,inputTimeout.ticks,inputTimeout.wallMs);
#else
    log("network_started", "\"port\":%u,\"max_clients\":1,\"protocol\":272,\"thread_checks\":true,\"headless_control_domain\":true,\"timeout_ticks\":%llu,\"timeout_wall_ms\":%llu", port,inputTimeout.ticks,inputTimeout.wallMs);
#endif
    return true;
}
void Session::enqueue(pse::BridgeMessage message) {
    if (outbox.size() >= OutboxCap) {
        // Owner explicitly abandons in-session delivery; terminate/reconnect.
        bridge.requestResync(pse::RESYNC_OUTBOUND_LOSS, "M12a outbox bound exceeded; reconnect required");
        overflowed = true;
        return;
    }
    outbox.push_back(std::move(message));
}
void Session::flush() {
    while (!outbox.empty()) {
        if (!pse::isSuccess(bridge.pushOutbound(std::move(outbox.front())))) break;
        outbox.pop_front();
    }
}
void Session::neutral(const char* reason) {
    log("network_neutral", "\"reason\":\"%s\",\"tick\":%llu,\"slot\":1,\"generation\":%u,\"input_seq\":%u,\"previous_buttons\":%u", reason, nowTick, gen, lastSeq, buttons);
    buttons = 0;
}
void Session::baseline() {
    pse::BridgeWorldBaseline b;
    b.targetSlot = 1;
    b.connectionGeneration = gen;
    b.mapEpoch = epoch;
    b.serverTick = static_cast<std::uint32_t>(nowTick);
    b.mapName = domain();
    // Empty authoritative replicated domain: no world/entity replication in M1.2a.
    enqueue(std::move(b));
}
void Session::recover(std::uint32_t reasons) {
    neutral("resync_reconnect");
    active = ready = false;
    gate.left(gen);
    // Reconnect is DOC-40's terminal alternative to full baseline recovery.
    // Pending events are explicitly superseded by session termination, never
    // treated as delivered. Nothing continues on the old session.
    log("network_resync", "\"reasons\":%u,\"retired_outbox\":%zu,\"tick\":%llu,\"action\":\"kick_reconnect\"", reasons, outbox.size(), nowTick);
    outbox.clear();
    overflowed = false;
    pse::BridgeControl kick;
    kick.slotId = 1;
    kick.connectionGeneration = gen;
    kick.reasonCode = pse::LEAVE_KICKED;
    enqueue(std::move(kick));
}
void Session::consume(pse::BridgeMessage& message) {
    if (const auto* e = std::get_if<pse::BridgeSessionEvent>(&message)) {
        log("network_session", "\"type\":%u,\"slot\":%u,\"generation\":%u,\"tick\":%llu", static_cast<unsigned>(e->type), e->slotId, e->connectionGeneration, nowTick);
        if (e->slotId != 1 || !e->connectionGeneration) return;
        if (e->type == pse::BridgeSessionEvent::Type::CLIENT_JOINED || e->type == pse::BridgeSessionEvent::Type::RECONNECTED) {
            if (e->connectionGeneration <= gen) return;
            neutral("new_generation");
            gen = e->connectionGeneration;
            active = true; ready = false; hasSeq = false; lastSeq = 0;
            joinedTick = nowTick;
            if (nativeMap) gate.authenticated(1, gen);
            pse::BridgeMapChange map;
            map.targetSlot = 1; map.newMapEpoch = epoch; map.mapName = domain();
            map.connectionGeneration = gen;
#ifdef PSE_FEATURE_P4
            map.mapStructure=map111::structure(gate.bound());
#endif
            enqueue(std::move(map));
        } else if (e->type == pse::BridgeSessionEvent::Type::CLIENT_LEFT && e->connectionGeneration == gen) {
            neutral("disconnect");
            active = ready = false;
            gate.left(gen);
        }
    } else if (const auto* r = std::get_if<pse::BridgeMapReady>(&message)) {
        if (nativeMap) {
#ifdef PSE_FEATURE_P4
            // Check authenticated routing first: a stale/foreign DTO cannot revoke
            // the current peer. A current identity mismatch is terminal for that
            // peer generation, before gate promotion, baseline or snapshot eligibility.
            if(active && r->slotId==1 && r->connectionGeneration==gen && r->mapEpoch==epoch &&
                !map111::matches(gate.bound(),r->mapName,r->mapStructure)) {
                ready=false;gate.left(gen);neutral("map_identity_mismatch_v111");
                log("map_ready_rejected","\"reason\":\"client_map_identity_mismatch_v111\",\"generation\":%u,\"epoch\":%u,\"promotion\":false,\"baseline\":false",gen,epoch);
                return;
            }
#endif
            // v1.10 MapReady carries only slot/epoch: this verifies the authenticated peer's
            // echo of the epoch bound to the host-verified identity, not the client's map itself.
            const auto verdict = gate.mapReady(r->slotId, r->connectionGeneration, r->mapEpoch, levelReady && active);
            if (verdict != map56::HostGate::Ready::Accepted) {
                log("map_ready_rejected", "\"reason\":\"%s\",\"slot\":%u,\"generation\":%u,\"epoch\":%u,\"current_generation\":%u,\"current_epoch\":%u,\"tick\":%llu", map56::HostGate::text(verdict), r->slotId, r->connectionGeneration, r->mapEpoch, gen, epoch, nowTick);
                return;
            }
            ready = true;
            baseline();
#ifdef PSE_FEATURE_P4
            log("network_ready", "\"slot\":1,\"generation\":%u,\"tick\":%llu,\"epoch\":%u,\"map\":\"%s\",\"map_ready_accepted\":true,\"identity_source\":\"native_name_structure_v111\",\"psm_version\":%u,\"count_a\":%llu,\"count_b\":%llu,\"world_synchronized\":false",gen,nowTick,epoch,nativeDomain.c_str(),gate.bound().version,gate.bound().countA,gate.bound().countB);
#else
            log("network_ready", "\"slot\":1,\"generation\":%u,\"tick\":%llu,\"epoch\":%u,\"map\":\"%s\",\"map_ready_accepted\":true,\"identity_source\":\"client_attested_epoch_echo_v110\",\"world_synchronized\":false", gen, nowTick, epoch, nativeDomain.c_str());
#endif
            return;
        }
        if (active && r->slotId == 1 && r->connectionGeneration == gen && r->mapEpoch == epoch && !ready) {
            ready = true;
            baseline();
            log("network_ready", "\"slot\":1,\"generation\":%u,\"tick\":%llu,\"epoch\":%u", gen, nowTick, epoch);
        }
    } else if (const auto* c = std::get_if<pse::BridgeInputCommand>(&message)) {
        // Worker validates each untrusted wire advance before mailbox mutation.
        // Coalescing can legitimately span many such advances during a long
        // simulation pause, so this trusted DTO consumer keeps monotonicity
        // rather than reapplying a per-packet jump limit to the merged state.
        const bool valid = active && ready && levelReady && c->slotId == 1 && c->connectionGeneration == gen &&
            (!hasSeq || c->sequence > lastSeq) && std::isfinite(c->moveForward) && std::isfinite(c->moveRight) &&
            std::isfinite(c->yaw) && std::isfinite(c->pitch);
        if (!valid) {
            log("network_input_rejected", "\"slot\":%u,\"generation\":%u,\"input_seq\":%u,\"tick\":%llu", c->slotId, c->connectionGeneration, c->sequence, nowTick);
            return;
        }
        hasSeq = true; lastSeq = c->sequence; lastInputTick = nowTick; lastInputMs = GetTickCount64();
        // Wire buttons are NOT native KEX button bits. M1.2a maps only the
        // forward/back axis to the already accepted digital command seam.
        buttons = c->moveForward > 0.5f ? 4u : c->moveForward < -0.5f ? 8u : 0u;
        log("network_input_accepted", "\"slot\":1,\"generation\":%u,\"input_seq\":%u,\"client_tick\":%u,\"tick\":%llu,\"forward\":%.6f,\"native_buttons\":%u,\"wire_buttons_ignored\":%u", gen, lastSeq, c->clientTick, nowTick, c->moveForward, buttons, c->buttons);
    } else if (const auto* req = std::get_if<pse::BridgeRespawnRequest>(&message)) {
        // Explicitly refuse features outside this phase instead of acknowledging
        // a world mutation that did not occur. Stale/foreign requests must not
        // manufacture a fresh KICK for the current session (DOC-45 P1-1).
        if (active && req->slotId == 1 && req->connectionGeneration == gen) recover(pse::RESYNC_EXTERNAL);
    } else if (const auto* interaction = std::get_if<pse::BridgeInteractionRequest>(&message)) {
        if (active && interaction->slotId == 1 && interaction->connectionGeneration == gen) recover(pse::RESYNC_EXTERNAL);
    }
}
void Session::tick(std::uint64_t tick, bool gameplay) {
    if (!running()) return;
    bridge.assertSimulationThread();
    nowTick = tick; levelReady = gameplay;
    flush();
    if (const auto reasons = bridge.consumeResyncReasons()) recover(reasons);
    pse::BridgeMessage message;
    // Bound work to queue capacity even under a continuously producing peer.
    for (std::size_t n = 0; n < pse::THREAD_BRIDGE_INBOUND_CAPACITY && bridge.pollInbound(message); ++n) consume(message);
    if (buttons) {
        if(!gameplay) neutral("level_not_running");
        else {
            const auto elapsedTicks=nowTick-lastInputTick,elapsedMs=GetTickCount64()-lastInputMs;
            if(inputTimeout.expired(elapsedTicks,elapsedMs)) {
                log("network_input_expired","\"tick\":%llu,\"elapsed_ticks\":%llu,\"elapsed_wall_ms\":%llu,\"threshold_ticks\":%llu,\"threshold_wall_ms\":%llu",nowTick,elapsedTicks,elapsedMs,inputTimeout.ticks,inputTimeout.wallMs);
                neutral("input_timeout");
            }
        }
    }
    if (active && !ready && nowTick - joinedTick >= 600) recover(pse::RESYNC_EXTERNAL);
    if (overflowed) recover(bridge.consumeResyncReasons() | pse::RESYNC_OUTBOUND_LOSS);
    flush();
}
void Session::stop(const char* reason) {
    if (!started || stopped) return;
    bridge.assertSimulationThread();
    stopped = true;
    neutral(reason); active = ready = false;
    gate.left(gen);
    flush();
    log("network_stop_begin", "\"reason\":\"%s\",\"outbox\":%zu,\"running_before\":%s", reason, outbox.size(), worker.isRunning() ? "true" : "false");
    worker.stop(); // synchronous join; ENet teardown/callbacks finish before return
    std::size_t cancelled = 0;
    pse::BridgeMessage message;
    while (bridge.pollInbound(message)) {
        if (pse::isCriticalMessage(message)) ++cancelled;
    }
    const auto pendingOutbound = bridge.getOutboundOccupancy() + outbox.size();
    if (pendingOutbound) bridge.requestResync(pse::RESYNC_OUTBOUND_LOSS, "M12a session ended before outbound delivery");
    const auto reasons = bridge.consumeResyncReasons();
    const auto stats = worker.getStats();
    log("network_joined", "\"reason\":\"%s\",\"worker_running\":%s,\"poll_loops\":%llu,\"inputs_received\":%llu,\"rejected_foreign\":%llu,\"rejected_stale\":%llu,\"rejected_generation\":%llu,\"rejected_jump\":%llu,\"lost_on_stop\":%llu,\"cancelled_critical\":%zu,\"retired_outbound\":%zu,\"resync_reasons\":%u,\"affinity_violations\":%llu,\"terminal_session\":true", reason, worker.isRunning() ? "true" : "false", stats.pollLoops, stats.inputsReceived, stats.inputsRejectedForeign, stats.inputsRejectedStale, stats.inputsRejectedGeneration, stats.inputsRejectedJump, stats.lostOnStop, cancelled, pendingOutbound, reasons, bridge.getAffinityViolationCount());
    outbox.clear();
    bridge.unregisterSimulationThread();
    bridge.resetForNewSession(); // worker joined, no producer/consumer remains
    log("network_retired", "\"reason\":\"%s\",\"inbound\":%zu,\"outbound\":%zu,\"storage_retained\":true", reason, bridge.getInboundOccupancy(), bridge.getOutboundOccupancy());
}
} // namespace m12a
