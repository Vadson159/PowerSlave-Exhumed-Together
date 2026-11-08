// Focused real-ENet regressions. No KEX process, hooks, or native movement claim.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "enet_worker.h"
#include "input_sequence.h"
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <vector>

using namespace pse;
using Clock = std::chrono::steady_clock;
static unsigned checks = 0;
static void check(bool ok, const char* name) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) throw std::runtime_error(name);
}
static uint64_t nowNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
}
struct Receipt { MsgInputCommand input; uint64_t receivedNs; };
struct Fixture {
    ThreadBridge bridge;
    ENetHostServer host;
    std::mutex observerMutex;
    std::vector<ClientInputDispatch> observed;
    ENetWorker worker{bridge, ENetWorkerMode::CLIENT};
    ClientConfig config;
    std::vector<Receipt> received;
    std::vector<BridgeSessionEvent> events;
    uint32_t readyCount = 0, slot = 0, gen = 0;
    uint64_t key = 0, ordinal = 0;
    bool malformed = false;
    Fixture(uint16_t port, bool cadence) {
        bridge.registerSimulationThread();
        HostConfig hc;
        hc.bindAddress = "127.0.0.1";
        hc.port = port; hc.maxClients = 1; hc.maxClientSlots = 1;
        hc.joinToken = "SECOND_INSTANCE_TEST"; hc.bridgeMode = true;
        check(host.start(hc), "real host starts");
        host.setSessionMap("M12A_INPUT_ONLY", 1);
        host.setCustomPacketHandler([this](ENetPeer* peer, uint8_t type, ByteReader& reader, uint32_t) {
            if (type == MSG_INPUT_COMMAND) {
                MsgInputCommand input;
                uint8_t actualSlot = 0; uint32_t actualGen = 0;
                if (!input.deserialize(reader) || !host.getPeerSession(peer, actualSlot, actualGen) ||
                    input.slotId != actualSlot || input.connectionGeneration != actualGen) malformed = true;
                else received.push_back({input, nowNs()});
                return true;
            }
            if (type == MSG_MAP_READY) {
                MsgMapReady ready;
                if (!ready.deserialize(reader) || ready.slotId != 1 || ready.mapEpoch != 1) malformed = true;
                else ++readyCount;
                return true;
            }
            return false;
        });
        worker.setClientInputCadence(cadence);
        worker.setClientInputObserver([this](const ClientInputDispatch& item) {
            std::lock_guard<std::mutex> lock(observerMutex);
            observed.push_back(item);
        });
        config.hostPort = port; config.joinToken = hc.joinToken;
        config.participantId = "native_client_worker_test";
        check(worker.startClient(config), "real CLIENT worker starts");
    }
    ~Fixture() { worker.stop(); bridge.unregisterSimulationThread(); host.stop(); }
    void step() {
        host.poll(0);
        BridgeMessage m;
        while (bridge.pollInbound(m)) {
            if (const auto* event = std::get_if<BridgeSessionEvent>(&m)) {
                events.push_back(*event);
                if (event->type == BridgeSessionEvent::Type::CLIENT_JOINED || event->type == BridgeSessionEvent::Type::RECONNECTED) {
                    slot = event->slotId; gen = event->connectionGeneration; key = event->reconnectKey;
                }
            }
        }
        Sleep(1);
    }
    bool until(const std::function<bool()>& pred, int ms = 2500) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(ms);
        do { step(); if (pred()) return true; } while (Clock::now() < deadline);
        return pred();
    }
    void pump(int ms, const std::function<void()>& beforeStep = {}) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(ms);
        do { if (beforeStep) beforeStep(); step(); } while (Clock::now() < deadline);
    }
    void ready(uint32_t generation) {
        BridgeMapReady ready; ready.slotId = static_cast<uint8_t>(slot); ready.connectionGeneration = generation; ready.mapEpoch = 1;
        check(isSuccess(bridge.pushOutbound(ready)), "MAP_READY queued with captured authority identity");
    }
    void sample(float forward, bool valid = true, uint32_t generation = 0) {
        BridgeClientInputSample sample;
        sample.slotId = static_cast<uint8_t>(slot); sample.connectionGeneration = generation ? generation : gen;
        sample.sampleOrdinal = ++ordinal; sample.simulationTick = ordinal;
        sample.sampledSteadyNs = nowNs(); sample.valid = valid; sample.moveForward = forward;
        check(isSuccess(bridge.pushOutbound(sample)), "snapshot accepted by SPSC");
    }
    std::vector<ClientInputDispatch> observations() {
        std::lock_guard<std::mutex> lock(observerMutex); return observed;
    }
};

static void cadence() {
    std::puts("CASE native_snapshot_cadence_and_neutral");
    Fixture f(28785, true);
    check(f.until([&] { return f.gen == 1; }), "authoritative WELCOME slot/generation received");
    check(f.slot == 1 && f.key != 0, "slot assigned by host; private reconnect key retained");
    f.sample(1);
    f.pump(70);
    check(f.received.empty(), "sample cannot bypass MAP_READY gate");
    f.ready(1);
    check(f.until([&] { return f.readyCount == 1 && f.received.size() >= 2; }), "input readiness then neutral packets decoded by host");
    check(f.received.back().input.moveForward == 0, "pre-ready held snapshot is not reused");
    const auto first = f.observations().size();
    uint64_t lastSample = 0;
    f.pump(850, [&] {
        if (nowNs() - lastSample >= 10000000ull) { f.sample(1); lastSample = nowNs(); }
    });
    auto observations = f.observations();
    const auto last = observations.size();
    check(last > first + 35, "cadence generates sustained stream independent of samples");
    const double hz = (last - first - 1) * 1e9 /
        static_cast<double>(observations[last - 1].generatedSteadyNs - observations[first].generatedSteadyNs);
    check(hz > 55 && hz < 65, "actual NETWORK dispatch rate bounded around60Hz");
    bool validIds = true, held = false;
    for (size_t i = 0; i < observations.size(); ++i) {
        const auto& o = observations[i];
        validIds &= o.slotId == 1 && o.generation == 1 &&
            validateInputSequence(i != 0, i ? observations[i - 1].sequence : 0, o.sequence) == InputSequenceResult::ACCEPT;
        held |= o.forward == 1 && o.sampleFresh && o.sampleOrdinal != 0;
    }
    check(validIds && held, "per-generation command sequence and sampled identity valid");
    const size_t beforeStale = f.received.size();
    f.pump(250);
    size_t neutralReceipts = 0;
    for (size_t i = beforeStale; i < f.received.size(); ++i) if (f.received[i].input.moveForward == 0) ++neutralReceipts;
    check(neutralReceipts >= 3, "missing fresh sample repeatedly sends neutral, not one release");
    check(f.observations().back().forward == 0 && !f.observations().back().sampleFresh, "stale snapshot neutral in worker");
    f.sample(-1);
    check(f.until([&] { return !f.received.empty() && f.received.back().input.moveForward == -1; }), "backward native snapshot reaches actual host");
    f.sample(1, false);
    check(f.until([&] { return !f.received.empty() && f.received.back().input.moveForward == 0; }), "invalid gameplay snapshot immediately neutralizes held state");
    f.sample(0);
    f.pump(100);
    check(f.received.back().input.moveForward == 0, "explicit release remains neutral");
    check(!f.malformed && f.bridge.getAffinityViolationCount() == 0, "real wire identity and bridge ownership preserved");
    std::printf("CADENCE_EVIDENCE rate=%.3f dispatches=%zu decoded=%zu repeated_stale_neutral=%zu affinity=0\n", hz, f.observations().size(), f.received.size(), neutralReceipts);
}

static void staleQueuedInput() {
    std::puts("CASE stale_queued_input_restart");
    Fixture f(28786, false);
    check(f.until([&] { return f.gen == 1; }), "default Worker gen1 authenticated");
    f.worker.setOutboundDrainPaused(true);
    BridgeInputCommand old; old.slotId = 1; old.connectionGeneration = 1; old.sequence = 8000; old.moveForward = 1;
    check(isSuccess(f.bridge.pushOutbound(old)), "old held input retained in paused outbound");
    f.config.reconnectKey = f.key;
    f.worker.stop();
    f.pump(60);
    check(f.worker.startClient(f.config), "same bridge Worker restart with authority reconnect key");
    check(f.until([&] { return f.gen == 2; }), "actual host rejoin assigns gen2");
    f.worker.setOutboundDrainPaused(false);
    f.pump(100);
    check(f.received.empty(), "old held INPUT was not relabeled into gen2");
    BridgeInputCommand current; current.slotId = 1; current.connectionGeneration = 2; current.sequence = 1;
    check(isSuccess(f.bridge.pushOutbound(current)), "fresh gen2 command queued");
    check(f.until([&] { return f.received.size() == 1; }), "fresh gen2 INPUT decoded");
    check(f.received[0].input.connectionGeneration == 2 && f.received[0].input.commandSequence == 1 &&
        f.received[0].input.moveForward == 0, "fresh neutral identity exact");
    check(f.worker.getStats().outboundNoPeer >= 1 && f.bridge.getAffinityViolationCount() == 0, "stale command rejected with no role violation");
}

static void reconnectCadence() {
    std::puts("CASE optin_reconnect_resets_sequence_and_samples");
    Fixture f(28787, true);
    check(f.until([&] { return f.gen == 1; }), "optin gen1 authenticated");
    f.ready(1);
    check(f.until([&] { return f.readyCount == 1; }), "gen1 ready decoded");
    f.sample(1);
    check(f.until([&] { return !f.received.empty() && f.received.back().input.moveForward == 1; }), "gen1 held sample delivered");
    BridgeControl reconnect; reconnect.type = BridgeControl::Type::RECONNECT; reconnect.connectionGeneration = 1;
    check(isSuccess(f.bridge.pushOutbound(reconnect)), "scoped reconnect enqueued");
    check(f.until([&] { return f.gen == 2; }), "reconnect gets new generation");
    f.sample(1, true, 1); // deliberately old generation
    f.ready(1); // deliberately stale ready
    f.pump(75);
    bool premature = false;
    for (const auto& r : f.received) premature |= r.input.connectionGeneration == 2;
    check(!premature, "old sample/ready cannot enable new generation");
    f.ready(2);
    check(f.until([&] { return !f.received.empty() && f.received.back().input.connectionGeneration == 2; }), "gen2 produces neutral after its own ready");
    const Receipt* first2 = nullptr;
    for (const auto& r : f.received) if (r.input.connectionGeneration == 2) { first2 = &r; break; }
    check(first2 && first2->input.commandSequence <= 3 && first2->input.moveForward == 0, "new gen low initial sequence and no inherited hold");
    f.sample(-1);
    check(f.until([&] { return f.received.back().input.connectionGeneration == 2 && f.received.back().input.moveForward == -1; }), "fresh gen2 sample resumes input");
    reconnect.connectionGeneration = 1;
    check(isSuccess(f.bridge.pushOutbound(reconnect)), "old reconnect control queued for guard");
    f.pump(80);
    check(f.gen == 2 && f.worker.getStats().reconnects == 1, "stale reconnect cannot disrupt gen2");
    check(f.bridge.getAffinityViolationCount() == 0, "reconnect ownership violations zero");
    // Pure scheduling boundary seam: no simulated billions of sends are claimed.
    check(!clientInputMustReconnect(CLIENT_INPUT_ROTATE_SEQUENCE - 1, 1, 2), "long-session sequence below rotation remains usable");
    check(clientInputMustReconnect(CLIENT_INPUT_ROTATE_SEQUENCE, 1, 2), "rotation requested before uint32 wrap");
    check(!clientInputMustReconnect(1, 100, 100 + CLIENT_INPUT_MAX_DISPATCH_GAP_NS), "20s exact gap boundary stays bounded");
    check(clientInputMustReconnect(1, 100, 101 + CLIENT_INPUT_MAX_DISPATCH_GAP_NS), "long dispatch silence requests new generation");
}

int main() {
    try { cadence(); staleQueuedInput(); reconnectCadence(); }
    catch (const std::exception& e) { std::printf("FAIL_EXCEPTION %s checks=%u\n", e.what(), checks); return 10; }
    std::printf("CLIENT_WORKER_RESULT checks=%u failures=0 scenarios=3 (DTO/ENet only, no native acceptance)\n", checks);
    return 0;
}
