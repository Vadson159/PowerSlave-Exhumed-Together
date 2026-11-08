// Audit-only reproducer (no KEX, no game). Uses the frozen M1.2a Session/Worker unchanged.
#include "network_input.hpp"
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
static void record(const char* e, const char* fmt, ...) {
    std::printf("  [session] %s ", e); va_list a; va_start(a, fmt); std::vprintf(fmt, a); va_end(a); std::puts("");
}
static pse::ClientConfig cfg(std::uint16_t port, const char* id, std::uint64_t key = 0) {
    pse::ClientConfig c; c.hostPort = port; c.joinToken = "M12A_Loopback"; c.participantId = id; c.reconnectKey = key; return c;
}
// R1: a KICK queued by Session::recover() for generation N is routed by slot only and
// terminates the freshly reconnected generation N+1.
static int staleKick() {
    std::puts("R1 stale slot-targeted KICK");
    m12a::Session s(record); const std::uint16_t port = 28761;
    if (!s.start(port)) return 2;
    std::uint64_t tick = 0;
    pse::ENetClientNode a; if (!a.connect(cfg(port, "repro_p2"))) return 2;
    auto step = [&](pse::ENetClientNode& c) { c.poll(1); s.tick(++tick, true); Sleep(5); };
    for (int n = 0; n < 200 && !s.wantsPlayer(); ++n) step(a);
    if (!s.wantsPlayer()) return 3;
    const auto key = a.getReconnectKey();
    std::printf("gen1 ready: gen=%u key_nonzero=%d\n", s.generation(), key != 0);
    s.worker.setOutboundDrainPaused(true);           // models outbound backpressure / delayed drain
    s.bridge.requestResync(pse::RESYNC_EXTERNAL, "audit: recover gen1");
    step(a);                                          // recover(): KICK{slot=1} queued for gen1
    a.disconnect();                                   // gen1 transport ends independently
    for (int n = 0; n < 20; ++n) { s.tick(++tick, true); Sleep(5); }
    pse::ENetClientNode b; if (!b.connect(cfg(port, "repro_p2", key))) return 2;
    for (int n = 0; n < 200 && !s.wantsPlayer(); ++n) step(b);
    std::printf("gen2 state before drain: authenticated=%d session_gen=%u wantsPlayer=%d\n", b.isAuthenticated(), s.generation(), s.wantsPlayer());
    if (!b.isAuthenticated() || s.generation() != 2) return 4;
    s.worker.setOutboundDrainPaused(false);
    for (int n = 0; n < 100; ++n) step(b);
    std::printf("gen2 after drain: client_connected=%d authenticated=%d session_wantsPlayer=%d\n", b.isConnected(), b.isAuthenticated(), s.wantsPlayer());
    s.stop("audit_r1");
    const bool defect = !b.isConnected() || !s.wantsPlayer();
    std::printf("R1 RESULT: %s\n", defect ? "DEFECT_REPRODUCED gen2 terminated by gen1 KICK" : "not reproduced");
    return defect ? 10 : 0;
}
// R2: after a clean leave, a different participant gets slot 2; Session only serves slot 1,
// never readies or evicts it, and maxClients=1 then blocks the original participant's rejoin.
static int foreignSlot() {
    std::puts("R2 different participant after leave");
    m12a::Session s(record); const std::uint16_t port = 28762;
    if (!s.start(port)) return 2;
    std::uint64_t tick = 0;
    auto step = [&](pse::ENetClientNode& c) { c.poll(1); s.tick(++tick, true); Sleep(2); };
    pse::ENetClientNode a; if (!a.connect(cfg(port, "alpha"))) return 2;
    for (int n = 0; n < 200 && !s.wantsPlayer(); ++n) step(a);
    const auto key = a.getReconnectKey();
    a.disconnect();
    for (int n = 0; n < 30; ++n) { s.tick(++tick, true); Sleep(5); }
    pse::ENetClientNode b; if (!b.connect(cfg(port, "beta"))) return 2;
    for (int n = 0; n < 700; ++n) step(b);
    std::printf("beta: authenticated=%d slot=%u gen=%u session_wantsPlayer=%d session_gen=%u still_connected=%d\n",
        b.isAuthenticated(), b.getAssignedSlot(), b.getConnectionGeneration(), s.wantsPlayer(), s.generation(), b.isConnected());
    pse::ENetClientNode a2; a2.connect(cfg(port, "alpha", key));
    for (int n = 0; n < 300; ++n) { a2.poll(1); b.poll(1); s.tick(++tick, true); Sleep(2); }
    std::printf("alpha rejoin with valid key: authenticated=%d session_wantsPlayer=%d\n", a2.isAuthenticated(), s.wantsPlayer());
    const bool fixed = !b.isConnected() && !b.isAuthenticated() && a2.isAuthenticated() &&
        a2.getAssignedSlot()==1 && a2.getConnectionGeneration()==2 && s.wantsPlayer();
    std::printf("R2 LIVE: beta_connected=%d alpha_slot=%u alpha_gen=%u alpha_ready=%d fixed=%d\n",b.isConnected(),a2.getAssignedSlot(),a2.getConnectionGeneration(),s.wantsPlayer(),fixed);
    s.stop("audit_r2");
    std::printf("R2 FIX RESULT: %s\n",fixed?"PASS":"FAIL");
    return fixed?0:1;
}
int main(int argc, char** argv) {
    if (argc > 1 && !std::strcmp(argv[1], "r2")) return foreignSlot();
    return staleKick();
}
