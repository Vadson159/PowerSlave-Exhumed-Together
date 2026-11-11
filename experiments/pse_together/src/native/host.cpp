// P7 isolated host candidate. Copied foundation; canonical host.cpp is unchanged.
#ifndef PSE_FEATURE_P7
#error "host_p7.cpp requires PSE_FEATURE_P7"
#endif
// M1.1 exact-build adapter. Opaque KEX objects; this is not reconstructed KEX source.
// All KEX calls and all hook changes execute on the launch primary / attested tick thread.
#include "portable_policy.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <MinHook.h>
#include "ui_native.hpp"
#include "ui/shared_p23.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <intrin.h>
#include <pse_together/config.hpp>
#include "weapon_pickup_p16.hpp"
#include "lifecycle_p16.hpp"
#include "respawn_p16.hpp"
#include <deque>
#include <map>
#include <string>
#include <cstring>
#include <optional>
#include "build_profile.hpp"
#include "seam.hpp"
#include "native_proxy.hpp"
#include "world_sync.hpp"
#include "log_throttle.hpp"
#include "async_journal.hpp"
#ifdef PSE_FEATURE_P8
#include "sprite_binding.hpp"
#endif
#ifdef PSE_FEATURE_P14
#include "gameplay_p14.hpp"
#endif
#ifdef PSE_FEATURE_P4
#include "spawn_anchor.hpp"
#include "proxy_profile.hpp"
#endif
#ifdef PSE_ROLE_CLIENT
#error "host.cpp is the host/bootstrap DLL; the client DLL is client.cpp"
#endif
#ifdef PSE_ROLE_HOST
#include "network_input.hpp"
#include "background_policy.hpp"
#include "background_profile.hpp"
#endif
#ifdef PSE_FEATURE_SNAPSHOT
#include "snapshot_contract.hpp"
#endif
#ifdef PSE_FEATURE_MAPREADY
#include "map_reader.hpp"
#endif

namespace {
using Byte = unsigned char;
using Unary = void(__fastcall*)(void*);
using Getter = void*(__fastcall*)(void*);
struct Vec3 { float x, y, z; };
using Spawn = void*(__fastcall*)(void*, int, const Vec3*, float, int);
using Ready = void(__fastcall*)(void*, int);
using Final = void(__fastcall*)();
enum class Phase { Waiting, Armed, Constructing, Live, Complete, WorldExit, WorldExited, Shutdown, Blocked };

// Static storage and every MinHook trampoline are deliberately retained until process exit.
// No cleanup/destructor/unload API is exported. Even StateExit does not unload this M1.1 DLL.
struct alignas(16) GuardedPlayer { Byte before[16]; alignas(16) Byte bytes[0x198]; Byte after[16]; };
static_assert(offsetof(GuardedPlayer, bytes) == 16);
struct Identity { Byte* actor{}; Byte* node{}; bool destroyed{}; };
// Milestone 5 (up to 8 players): the host is slot 0 (P1, the engine's own player); every connected client is slot 1..7 and has its own
// Player block + Puppet owned by this DLL. What the single-P2 host kept in g.* is per Remote now; the code that works on "the remote player"
// reads it through R() (the remote selected by g.cur) so it stays the same code for every slot.
constexpr unsigned MaxRemote = 7;
struct P2Persist {
    bool valid{}; Byte weapons[8]{}; std::uint16_t ammo[8]{}, ankhs{}, ankhMask{}, artifacts{}, abilities{}, quest{};
    std::uint32_t dolls{}, extra{}; int weapon{}; std::int16_t health{200};
};
struct Remote {
    GuardedPlayer storage{}; Identity p2{};
    bool exists{};                       // Player + Puppet constructed in this world
    std::uint32_t held[13]{};
    std::uint64_t p2Tick{}, motionTick{}, p2Teleports{}, p2TeleportRefused{}, secondaryUpdates{}, secondaryPuppetTicks{};
    std::uint32_t appliedSequence{}, appliedGeneration{}, appliedButtons{};
    unsigned commandCalls{};
    Vec3 p2Start{}, motionBefore{}; float maxP2Distance{};
    P2Persist entry;                     // what this player had when this world started (a Restart Level gives that back)
    std::string key;                     // stable client identity (survives a map change; slots are handed out in join order)
};
struct LocalState {
    std::array<std::uint32_t, 32> input{};
    std::array<Byte, 8> owned{};
    std::array<std::uint16_t, 8> ammo{};
    Byte* node{}; Byte* actor{}; Byte* owner{}; void* animation{};
    std::int16_t health{}; std::int32_t weapon{}, pending{};
    bool operator==(const LocalState&) const = default;
};
struct Context {
    HMODULE module{}; Byte* base{}; DWORD expectedTid{}; DWORD observedTid{};
    HANDLE log{INVALID_HANDLE_VALUE}; SRWLOCK logLock = SRWLOCK_INIT;
    std::atomic<unsigned> prepared{0}, permit{0};
    Phase phase{Phase::Waiting}; bool inTick{}, schedule{}, scope{}, p2Update{}, resetAttempted{}, worldExit{}, shutdown{}, failed{};
    bool created[profile::Count]{}, enabled[profile::Count]{};
    void* original[profile::Count]{};
    Identity p1{}; Remote remotes[MaxRemote]; unsigned cur{1}; // cur = the remote (slot 1..7) the P2-context code below works on
    std::uint64_t seq{}, ticks{}, activeTicks{}, localUpdates{}, primaryPuppetTicks{};
    std::uint64_t attestSamples{}, inputComparisons{}, foreignCalls{}, dtorReturns{}, stateExitReturns{}, shutdownTicks{};
    unsigned localThisTick{}, resolverHits{}, inputSamples{};
#ifdef PSE_FEATURE_P14
    std::uint64_t ankhSerial{}, p14QuietEvents{}; std::int16_t ankhPrimaryHealth{};
#endif
#ifdef PSE_FEATURE_P4
    bool anchorValid{};pse::placement::Box anchor{};float anchorYaw{},anchorFloor{},anchorCeiling{};int anchorSector{};
#endif
#ifdef PSE_ROLE_HOST
    m12a::Session* network{}; // retained, never a CRT global destructor
    void* focusOriginal{}; // one additional process-lifetime trampoline, never freed
    background::FocusLease focusLease{};
    std::uint64_t focusSerial{}, backgroundTicks{}, neutralUpdates{};
    bool focusKnown{}, focusLost{}, backgroundStopping{}, backgroundPeriod{};
    bool neutralPrimary{}, resumeNeutral{}, bindingsResetPending{}, uiCaptureWas{};
    DWORD foregroundPid{};
#ifdef PSE_FEATURE_SNAPSHOT
    std::uint32_t snapshotSequence{}, entityGeneration[8]{};
    std::uint32_t p7DeathSequence[8]{};bool p7Dead[8]{};
    std::uint64_t p7KeyProbeSerial{},p7KeyReviewSerial{};
    std::uint32_t p7KeyProbeGeneration{},p7KeyProbeConnection{},p7KeyProbeEpoch{};bool p7KeysPermitted{};
#endif
#ifdef PSE_FEATURE_MAPREADY
    bool snapshotEligibleLogged{};
    map56::Fault lastMapFault{map56::Fault::None};
#endif
#endif
} g;

template<class T> T read(const void* p, std::size_t offset = 0) { return *reinterpret_cast<const T*>(static_cast<const Byte*>(p) + offset); }
Remote& R() { return g.remotes[g.cur - 1]; }
Remote& R(unsigned slot) { return g.remotes[slot - 1]; }
struct Ctx { unsigned prev; explicit Ctx(unsigned slot) : prev(g.cur) { g.cur = slot; } ~Ctx() { g.cur = prev; } };
template<class T> void own(std::size_t offset, T value) { *reinterpret_cast<T*>(R().storage.bytes + offset) = value; }
void* at(profile::Index i) { return g.base + profile::Sites[i].rva; }
Byte* player() { return R().storage.bytes; }
// The remote whose Player block is p (nullptr: not one of ours).
unsigned remoteSlotOf(const void* p) { for (unsigned s = 1; s <= MaxRemote; ++s) if (p == g.remotes[s - 1].storage.bytes) return s; return 0; }
unsigned remoteSlotOfActor(const void* a) { if (!a) return 0; for (unsigned s = 1; s <= MaxRemote; ++s) if (g.remotes[s - 1].exists && a == g.remotes[s - 1].p2.actor) return s; return 0; }
Byte* primary() { return g.base + 0xaf33e0; }
Byte* instance() { return g.base + 0xaf33a0; }
bool thread() { return GetCurrentThreadId() == g.expectedTid; }
bool gameplay() { return read<int>(instance(), 0x24) == 3 && read<int>(instance(), 0x28) == 0; }

pse::journal::Throttle logThrottle; // guarded by g.logLock
std::uint64_t lastLogFlushMs{};
pse::journal::AsyncWriter journalWriter; // the file is written by its own thread (async_journal.hpp)
void journalSink(const char* data, std::size_t size, bool hard) {
    DWORD written{};
    if (size) WriteFile(g.log, data, static_cast<DWORD>(size), &written, nullptr);
    if (hard) FlushFileBuffers(g.log);
}
void log(const char* event, const char* fmt = "", ...) {
    if (g.log == INVALID_HANDLE_VALUE) return;
    {   // per-tick events are cut down before any formatting work (see log_throttle.hpp)
        AcquireSRWLockExclusive(&g.logLock);
        const bool write = logThrottle.allow(event, GetTickCount64());
        ReleaseSRWLockExclusive(&g.logLock);
        if (!write) return;
    }
    char detail[6500]{}; va_list args; va_start(args, fmt);
    vsnprintf_s(detail, sizeof(detail), _TRUNCATE, fmt, args); va_end(args);
    SYSTEMTIME utc{}; GetSystemTime(&utc);
    AcquireSRWLockExclusive(&g.logLock);
    char line[7500]{};
    const int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
        "{\"event\":\"%s\",\"seq\":%llu,\"utc\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\",\"tid\":%lu%s%s}\n",
        event, ++g.seq, utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond, utc.wMilliseconds,
        GetCurrentThreadId(), detail[0] ? "," : "", detail);
    if (n > 0) journalWriter.append(line, static_cast<std::size_t>(n));
    (void)lastLogFlushMs;
    ReleaseSRWLockExclusive(&g.logLock);
    // The game's thread never waits for the disk (micro freezes): only a fault, the shutdown and the final exit write and force the file here.
    if (std::strncmp(event, "blocked", 7) == 0 || std::strncmp(event, "native_fault", 12) == 0 || std::strncmp(event, "shutdown", 8) == 0 ||
        std::strncmp(event, "native_final_exit", 17) == 0) journalWriter.flush(true);
}
char lastBlock[64]{}; unsigned blockRecoveries{};
// A process-level fault (hook install/restore, wrong thread, exact-byte profile): never recovered, the game must restart. Anything else is a
// fault of one world (one level's bodies, snapshots, map identity, respawn) and ends co-op only until that world is left (recoverAfterBlock).
bool processFault(const char* r) {
    for (const char* k : {"install", "hook", "minhook", "thread", "affinity", "prefix", "profile", "restore", "bootstrap", "nested", "vtable"})
        if (std::strstr(r, k)) return true;
    return false;
}
void block(const char* reason) {
    if (!g.failed) strncpy_s(lastBlock, reason, _TRUNCATE);
    g.failed = true; g.schedule = false; g.phase = Phase::Blocked;
    log("blocked", "\"reason\":\"%s\",\"native_retry\":false,\"storage_pinned\":true", reason);
}
bool guardsOf(const Remote& r) {
    return std::all_of(std::begin(r.storage.before), std::end(r.storage.before), [](Byte b) { return b == 0xa5; }) &&
           std::all_of(std::begin(r.storage.after), std::end(r.storage.after), [](Byte b) { return b == 0xa5; });
}
bool guards() { return guardsOf(R()); }
bool live(const Identity& id, Byte* p) {
    return !id.destroyed && id.actor && id.node && read<Byte*>(p, 0xd8) == id.node &&
        read<Byte*>(id.node, 0x28) == id.actor && read<Byte*>(id.actor, 0x10) == id.node &&
        read<Byte*>(id.actor, 0x1b8) == p && read<Byte*>(id.actor) == g.base + 0x52d198;
}
Vec3 position(Byte* a) { return {read<float>(a, 0x18), read<float>(a, 0x1c), read<float>(a, 0x20)}; }
bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool p16Pending(unsigned slot);
void debugKillP2(std::uint64_t worldTicks); // smoke only: PSE_DEBUG_KILL_P2=<world tick> kills the remote player natively
bool p16Restore();
// ---- Milestone 4 (campaign). The engine keeps only P1's inventory across maps (kexPlayer lives inside the GameInstance and is never
// rebuilt); P2 is our own Player block, so the fields SavePersistentData (0x1b5b80) copies travel here: weapons, ammo, ankhs,
// artifacts, abilities, quest items, team dolls, health. p2Entry = what P2 had when this world started (a Restart Level gives that back).
std::map<std::string, P2Persist> travelBy; // carried inventory per client identity (see Remote::key)
bool mapAnnounced{};
bool worldBound{}; // this world's map identity is bound to the host session (first world: configureNativeMap; later: rebindNativeMap)
void rearmWorld();
void recoverAfterBlock();
void tick16();
int p16DtorEntry(void* actor);
void p16DtorReturn(int index,void* actor);
#ifdef PSE_FEATURE_SNAPSHOT
#ifndef PSE_FEATURE_MAPREADY
bool firstMapWitness() {
    Identity id{};id.node=read<Byte*>(primary(),0xd8);id.actor=id.node ? read<Byte*>(id.node,0x28) : nullptr;
    if(!live(id,primary()))return false;
    const auto v=position(id.actor);
    return finite(v) && v.x==-7420.0f && v.y==-6775.0f && v.z==1152.0f && read<int>(id.actor,0x3c)==259;
}
#endif
// ---- Host-side visual proxy of the remote player (slot 1) -----------------------------------------------------------
// The stock Puppet of P2 is never drawn by the world renderer, so the host shows P2 the way the client shows the host:
// a passive stock actor with the player sprite (native_proxy.cpp in hostMode) that follows P2's snapshots.
pse::seam::Seam proxySeam;
pse::proxy::SnapshotRouter<MaxRemote>* proxies{}; // process lifetime, never freed; one proxy per remote slot
bool proxyInstalled{},proxyLocalBound{},proxyMapReady{},proxyExited{},proxySafe{};
std::uint32_t proxyGeneration[8]{}; // per slot: the connection generation its proxy is configured for (0 = not configured yet)
std::uint32_t proxyEntity[8]{};bool proxyRebuild[8]{}; // per slot: body generation the proxy shows; a new one (respawn) needs a fresh proxy
std::uint64_t proxyNotSafeLogged{},proxyDeferredLogged{};
void proxyJournal(const char* event,const char* detail) { log(event,"%s",detail); }
std::uint64_t proxyMapToken(const map56::Identity& id,std::uint32_t epoch) {
    std::uint64_t h=1469598103934665603ULL;auto mix=[&](std::uint64_t v) { for(int i=0;i<8;++i) { h^=(v>>(i*8))&0xff;h*=1099511628211ULL; } };
    for(char ch:id.name) mix(static_cast<unsigned char>(ch));mix(id.countA);mix(id.countB);mix(id.version);mix(epoch);return h ? h : 1;
}
void proxyRoute(const pse::BridgePlayerSnapshot& s) {
    if(!proxyInstalled || proxyExited || !proxies || !g.network || s.slotId<1 || s.slotId>MaxRemote) return;
    const unsigned slot=s.slotId;const auto gen=g.network->generation(slot),epoch=g.network->mapEpoch();
    if(!gen) return;
    if(proxyGeneration[slot] && proxyGeneration[slot]!=gen) {
        // This slot's player rejoined (new connection generation, e.g. after its level restart): its previous proxy must be fully
        // retired in this world before a fresh incarnation (new automaton + adapter slot rearm) may spawn. The other proxies keep running.
        if(auto* old=proxies->proxy(static_cast<std::uint8_t>(slot))) old->onDisconnect();
        if(!proxies->slotQuiescent(static_cast<std::uint8_t>(slot)) || !pse::native_proxy::rearmSlot(slot)) return;
        proxies->resetSlot(static_cast<std::uint8_t>(slot));
        log("proxy_rejoin_rearmed","\"slot\":%u,\"old_generation\":%u,\"generation\":%u",slot,proxyGeneration[slot],gen);
        proxyGeneration[slot]=0;proxyEntity[slot]=0;proxyRebuild[slot]=false;
    }
    // The player respawned (new body generation in its snapshots): the proxy removes the old body's Actor by itself, but its snapshot guard keeps the old
    // generation and its one spawn is used, so no new Actor ever came (the player stayed invisible after the first death). Once the old Actor is retired,
    // the slot gets a fresh machine and adapter instance; the next snapshot spawns the new body. Same as the client does on a respawn confirm.
    if(proxyEntity[slot] && proxyEntity[slot]!=s.entityGeneration) proxyRebuild[slot]=true;
    if(proxyRebuild[slot]) {
        if(auto* old=proxies->proxy(static_cast<std::uint8_t>(slot))) old->onDisconnect();
        if(!proxies->slotQuiescent(static_cast<std::uint8_t>(slot)) || !pse::native_proxy::rearmSlot(slot)) return; // proxyPhase still retires the old Actor
        proxies->resetSlot(static_cast<std::uint8_t>(slot));
        log("proxy_respawn_rearmed","\"slot\":%u,\"old_body\":%u,\"body\":%u",slot,proxyEntity[slot],s.entityGeneration);
        proxyGeneration[slot]=0;proxyRebuild[slot]=false;
    }
    proxyEntity[slot]=s.entityGeneration;
    if(!proxyLocalBound) { proxies->configureSession(0,1,epoch);proxyLocalBound=true; } // local slot 0; per-slot generations follow
    if(!proxyGeneration[slot]) {
        proxies->configureSessionSlot(static_cast<std::uint8_t>(slot),gen,epoch);proxyGeneration[slot]=gen;
        log("proxy_session_configured","\"local_slot\":0,\"remote_slot\":%u,\"generation\":%u,\"epoch\":%u",slot,gen,epoch);
    }
    if(!proxyMapReady) {
        map56::Identity id;const auto fault=map56::derive(map56::observe(g.base),id);
        if(fault==map56::Fault::None) { proxies->onNativeMapReady(epoch,proxyMapToken(id,epoch));proxyMapReady=true;log("proxy_native_map_ready","\"epoch\":%u,\"map\":\"%s\"",epoch,id.name.c_str()); }
    }
    pse::proxy::Rejection rj{};const auto rr=proxies->route(s,gameplay(),rj);
    if(rr==pse::proxy::RouteResult::Routed) pse::native_proxy::stageAnimation(s);
}
void proxyPhase() {
    if(!proxyInstalled || proxyExited || !proxies || !proxySeam.attached()) return;
    using namespace pse::proxy;
    for(unsigned slot=1;slot<=MaxRemote;++slot) if(proxyGeneration[slot] && !(g.network && g.network->wantsPlayer(slot))) { if(auto* p=proxies->proxy(static_cast<std::uint8_t>(slot))) p->onDisconnect(); }
    const auto ready=proxySeam.worldReady(g.ticks);
    const bool step=gameplay() && g.localThisTick==1 && !g.failed && (ready&pse::seam::LocalTicked) && (ready&pse::seam::AdapterHealthy);
    const TickContext ctx{step,(ready&pse::seam::StableWorld)!=0};
    if(step!=proxySafe) { proxySafe=step;log("proxy_safe_phase","\"safe\":%s,\"ready_flags\":%d,\"tick\":%llu",step?"true":"false",ready,g.ticks); }
    proxies->forEach([&](VisualProxyStateMachine& p) {
        const auto before=p.state();
        const auto a=p.tick(&proxySeam,ctx);
        if(a==TickAction::None || (a==TickAction::NotSafe && ++proxyNotSafeLogged%600!=1) || (a==TickAction::SpawnDeferred && ++proxyDeferredLogged%120!=1)) return;
        log("proxy_tick_action","\"slot\":%u,\"action\":\"%s\",\"state_before\":\"%s\",\"state_after\":\"%s\",\"quarantine\":\"%s\",\"handle\":%llu,\"tick\":%llu",
            p.targetSlot(),toString(a),toString(before),toString(p.state()),toString(p.quarantineReason()),p.handle() ? p.handle() : p.orphanedHandle(),g.ticks);
    });
}
void proxyTickEnter() { if(proxyInstalled && !proxyExited && proxySeam.attached()) proxySeam.tickEnter(g.ticks); }
void proxyTickLeave() { if(proxyInstalled && !proxyExited && proxySeam.attached()) proxySeam.tickLeave(g.ticks,g.localThisTick); }
// World exit / shutdown: automaton latch first, then the adapter, never a native proxy call afterwards.
void proxyExit(const char* reason,std::uint32_t code) {
    if(!proxyInstalled || proxyExited || !thread()) return;
    proxyExited=true;
    if(proxies) proxies->forEach([](pse::proxy::VisualProxyStateMachine& p) { p.onPreUnload(); });
    const bool was=proxySeam.attached();
    if(was) proxySeam.worldExit(code);
    pse::native_proxy::exitBarrier(code);
    const bool restored=pse::native_proxy::detach();
    proxySeam.detach(restored && !proxySeam.faults() ? pse::seam::ExitDetached : pse::seam::Faulted);
    log("proxy_world_exit","\"reason\":\"%s\",\"was_attached\":%s,\"restored\":%s",reason,was?"true":"false",restored?"true":"false");
}
bool installHostProxy() {
    proxies=new pse::proxy::SnapshotRouter<MaxRemote>();proxies->bind(g.expectedTid);
    if(!pse::native_proxy::install(g.base,g.expectedTid,proxyJournal,1,true) || proxySeam.registerTable(&pse::native_proxy::table())!=ERROR_SUCCESS) return false;
    pse::native_proxy::permitSpawn();proxyInstalled=true;return true;
}
// Every remote that exists in this world: constructed, live Player/Puppet, intact guards.
bool remotesLive() {
    for (unsigned s = 1; s <= MaxRemote; ++s) if (R(s).exists && (!live(R(s).p2, R(s).storage.bytes) || !guardsOf(R(s)))) return false;
    return true;
}
unsigned firstRemote() { for (unsigned s = 1; s <= MaxRemote; ++s) if (R(s).exists) return s; return 0; }
void captureNativeSnapshots() {
    if(!thread() || !g.inTick) { block("snapshot_capture_affinity");return; }
    if(g.failed || g.shutdown || g.worldExit || g.backgroundStopping || !gameplay() ||
        !g.network || !g.network->anyWantsPlayer() || g.localThisTick!=1 || (g.ticks%2)!=0)return;
#ifdef PSE_FEATURE_MAPREADY
    const bool eligible=g.network->anySnapshotEligible();
    if(eligible!=g.snapshotEligibleLogged) {
        g.snapshotEligibleLogged=eligible;
        log("snapshot_eligibility","\"eligible\":%s,\"tick\":%llu,\"epoch\":%u,\"connected\":%u,\"world\":%u,\"peer\":%u,\"map\":\"%s\"",eligible ? "true" : "false",g.ticks,g.network->mapEpoch(),g.network->connected(),static_cast<unsigned>(g.network->mapGate(1).world()),static_cast<unsigned>(g.network->mapGate(1).peer()),g.network->mapGate(1).bound().name.c_str());
    }
    if(!eligible)return;
#endif
    // The host's own identity is bound when the first body of this world is built; a client can be ready a few ticks before that (no free
    // spot beside the host yet: construct() retries every tick). Nothing to send yet — not a fault (it once ended the whole session in SANCTUAR).
    if(!g.p1.actor || !g.p1.node) return;
    if(!live(g.p1,primary()) || !remotesLive()) { block("snapshot_capture_ownership");return; }
    if(g.ticks>UINT32_MAX || g.snapshotSequence>UINT32_MAX-16) { block("snapshot_counter_exhaustion");return; }
    // One snapshot per player in the game; each goes, unicast with that client's own connection generation, to every connected client.
    // A client uses its own slot's snapshot for self state and shows the others as proxies (the router excludes the local slot).
    for(unsigned slot=0;slot<=MaxRemote;++slot) {
        if(slot && (!R(slot).exists || R(slot).p2Tick!=g.ticks))continue; // a remote that did not update this tick has nothing new to say
        if(p16Pending(slot))continue;
        std::optional<Ctx> ctx;if(slot)ctx.emplace(slot);
        const auto& id=slot ? R().p2 : g.p1;
        const auto v=position(id.actor);const float yaw=read<float>(id.actor,0x38),pitch=read<float>(id.actor,0x30);
        if(!finite(v) || !std::isfinite(yaw) || !std::isfinite(pitch) || !g.entityGeneration[slot]
#if !defined(PSE_FEATURE_P8) && !defined(PSE_FEATURE_P7)
           || read<std::int16_t>(id.actor,0x17a)<=0
#endif
        ) { block("snapshot_capture_transform_or_lifetime");return; }
        pse::BridgePlayerSnapshot s;s.slotId=static_cast<std::uint8_t>(slot);s.entityId=0x5500+slot;s.entityGeneration=g.entityGeneration[slot];
        s.sequence=++g.snapshotSequence;s.serverTick=static_cast<std::uint32_t>(g.ticks);s.mapEpoch=g.network->mapEpoch();
        s.posX=v.x;s.posY=v.y;s.posZ=v.z;s.yaw=yaw;s.pitch=pitch;s.health=read<std::int16_t>(id.actor,0x17a);
        // Actual post-world native scalars, not transport defaults. Every slot uses its own validated Full Player; no addresses enter the DTO.
        Byte* const nativePlayer=slot?player():primary();
        s.velX=read<float>(id.actor,0xe0);s.velY=read<float>(id.actor,0xe4);s.velZ=read<float>(id.actor,0xe8);
        s.buttons=read<std::uint32_t>(nativePlayer);
        s.lifeState=s.health>0 && !(read<unsigned>(id.actor,0x1c0)&0x200u)?pse::LIFE_ALIVE:pse::LIFE_DEAD;
        if(!std::isfinite(s.velX)||!std::isfinite(s.velY)||!std::isfinite(s.velZ)) { block("p8_velocity_nonfinite");return; }
        const auto active=read<int>(nativePlayer,0x88),pending=read<int>(nativePlayer,0x8c);
        if(active<0||active>=8||pending<0||pending>=8){block("p7_native_weapon_range");return;}
        s.activeWeapon=static_cast<std::uint8_t>(active);s.weaponMask=0;
        for(unsigned index=0;index<8;++index) {
            const auto owned=read<Byte>(nativePlayer,0xcc+index);
            if(owned>1){block("p7_native_owned_nonbool");return;}
            if(owned)s.weaponMask|=1u<<index;
            s.ammoByWeapon[index]=read<std::uint16_t>(nativePlayer,0xbc+index*2);
        }
        s.ammo=s.ammoByWeapon[s.activeWeapon];
        s.pendingWeapon=pending==active?-1:pending;
        s.keys=read<std::uint16_t>(nativePlayer,0x98);
        s.artifacts=read<std::uint32_t>(nativePlayer,0x90);s.abilities=read<std::uint32_t>(nativePlayer,0x94);
        s.questItems=read<std::uint16_t>(nativePlayer,0x9a);
        s.ankhCount=read<std::uint16_t>(nativePlayer,0x84);s.ankhMask=read<std::uint16_t>(nativePlayer,0x86);
        s.teamDolls=read<std::uint32_t>(nativePlayer,0xa4);
        const bool dead=s.health<=0||(read<unsigned>(id.actor,0x1c0)&0x200u)!=0;
        if(g.p7Dead[slot]&&!dead){block("p7_unadmitted_native_revival");return;}
        if(dead&&!g.p7Dead[slot]){
            if(g.p7DeathSequence[slot]==UINT32_MAX){block("p7_death_sequence_exhausted");return;}
            ++g.p7DeathSequence[slot];g.p7Dead[slot]=true;
        }
        s.lifeState=dead?pse::LIFE_DEAD:pse::LIFE_ALIVE;s.deathSequence=g.p7DeathSequence[slot];
        s.stateTimeNs=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        s.resetToken=s.discontinuitySequence=s.entityGeneration;
        // The stock Puppet is not drawn by the world renderer: a remote player is shown by a passive proxy actor (see proxyRoute above).
        if(slot) { auto routed=s;routed.connectionGeneration=g.network->generation(slot);proxyRoute(routed); }
        const bool chatty=slot<=1 || g.ticks%60==0; // 8 players: keep the journal readable
        if(chatty) log("p7_native_scalars","\"slot\":%u,\"snapshot_seq\":%u,\"server_tick\":%u,\"state_time_ns\":%llu,\"health\":%d,\"life_state\":%u,\"death_sequence\":%u,\"active_weapon\":%u,\"pending_weapon\":%d,\"weapon_mask\":%u,\"ammo\":[%u,%u,%u,%u,%u,%u,%u,%u],\"keys\":%u,\"artifacts\":%u,\"abilities\":%u,\"quest_items\":%u,\"ankh_count\":%u,\"ankh_mask\":%u,\"team_dolls\":%u,\"reset_token\":%u,\"discontinuity\":%u,\"post_world_tick\":true",
            slot,s.sequence,s.serverTick,s.stateTimeNs,s.health,s.lifeState,s.deathSequence,s.activeWeapon,s.pendingWeapon,s.weaponMask,s.ammoByWeapon[0],s.ammoByWeapon[1],s.ammoByWeapon[2],s.ammoByWeapon[3],s.ammoByWeapon[4],s.ammoByWeapon[5],s.ammoByWeapon[6],s.ammoByWeapon[7],s.keys,s.artifacts,s.abilities,s.questItems,s.ankhCount,s.ankhMask,s.teamDolls,s.resetToken,s.discontinuitySequence);
        unsigned sent=0;
        for(unsigned to=1;to<=MaxRemote;++to) if(g.network->snapshotEligible(to) && g.network->publishNativeSnapshot(to,s)) ++sent;
        if(chatty) log("native_snapshot_captured", "\"slot\":%u,\"snapshot_seq\":%u,\"server_tick\":%u,\"map_epoch\":%u,\"entity_id\":%u,\"entity_generation\":%u,\"pos\":[%.9g,%.9g,%.9g],\"yaw\":%.9g,\"pitch\":%.9g,\"queued\":%s,\"recipients\":%u,\"actor\":\"%p\",\"node\":\"%p\",\"owner\":\"%p\",\"post_world_tick\":true",
            slot,s.sequence,s.serverTick,s.mapEpoch,s.entityId,s.entityGeneration,v.x,v.y,v.z,yaw,pitch,sent ? "true" : "false",sent,id.actor,id.node,read<Byte*>(id.actor,0x1b8));
    }
}
// ---- Milestone 2: doors / lifts / shared keys (see world_sync.hpp) ---------------------------------------------------
// The client's own actions arrive as requests (mover spawned on the client, key picked up on the client) and are replayed
// here; whatever the host or P2 does locally is sent to the client. Keys are shared: P1, P2 and the client always hold the union.
std::deque<pse::BridgeInteractionRequest> worldRequests;
// Native overworld map object (global 0xa45e20, vtable 0x14052c330): the location the map screen is "at" is a 16-bit field (+0x10); the exit trigger
// sets it (setter = vtable slot 0x30 = 0x1d5920, `mov [rcx+0x10],dx`) to the next location BEFORE the game asks state 2, and the map screen opens its cursor
// (and its "Will you travel to X?" box) on it. A player who is only LED to the map screen must get the same value, or his map opens on a stale location.
short overworldCurrent() { return read<short>(g.base+0xa45e20,0x10); }
bool setOverworldCurrent(int id) {
    static constexpr Byte setter[5]{0x66,0x89,0x51,0x10,0xc3};
    if(id<0||id>99||!std::equal(std::begin(setter),std::end(setter),g.base+0x1d5920))return false;
    reinterpret_cast<void(__fastcall*)(void*,short)>(g.base+0x1d5920)(g.base+0xa45e20,static_cast<short>(id));return true;
}
// The game's own "Travel" answer of the exit box (0x1b0e00 -> GameInstance vtable+0xe0 = 0x1b1a50): map name of the location, state 2 with the fade,
// current level AND that location unlocked on the map screen (GameInstance+0x10), autosave. Same function in client.cpp.
bool travelToOverworld(int location) {
    static constexpr Byte travel[28]{0x48,0x8b,0x05,0x99,0x25,0x94,0x00,0x48,0x8d,0x0d,0x92,0x25,0x94,0x00,0xc6,0x05,0x7f,0xb5,0x93,0x00,0x00,0x48,0xff,0xa0,0xe0,0x00,0x00,0x00};
    static constexpr Byte enter[16]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x05,0xbf,0x43,0x89};
    if(!std::equal(std::begin(travel),std::end(travel),g.base+0x1b0e00) || !std::equal(std::begin(enter),std::end(enter),g.base+0x1b1a50) ||
       read<Byte*>(instance())!=g.base+0x52ad08 || read<Byte*>(g.base+0x52ad08,0xe0)!=g.base+0x1b1a50 || !setOverworldCurrent(location)) return false;
    reinterpret_cast<void(__fastcall*)()>(g.base+0x1b0e00)();
    return read<int>(instance(),0x28)==2;
}
// A client's own game started a map change / ending (its player reached an exit): the host does the same native call, then the usual order
// (MapChange) leads every other client after it. Refused when a change is already under way (the host or another player got there first).
void followClientExit(std::uint32_t lo,std::uint32_t hi) {
    char stem[9]{};std::memcpy(stem,&lo,4);std::memcpy(stem+4,&hi,4);
    const std::string s(stem);
    if(s.empty() || !std::all_of(s.begin(),s.end(),[](char ch){return (ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='_';})) { log("host_client_exit_refused","\"reason\":\"name\""); return; }
    const auto o=map56::observe(g.base);
    if(o.state!=3 || o.pending!=0 || !gameplay()) { log("host_client_exit_refused","\"reason\":\"busy\",\"stem\":\"%s\"",s.c_str()); return; }
    if(s.size()==8 && s.rfind("STATE2",0)==0 && std::isdigit(static_cast<unsigned char>(s[6])) && std::isdigit(static_cast<unsigned char>(s[7]))) {   // the client's player reached the exit: map screen at the location it was sent to
        const int at=(s[6]-'0')*10+(s[7]-'0');
        if(travelToOverworld(at)) { log("host_follow_client_exit","\"state\":2,\"location\":%d,\"native_travel\":true",at);return; }
        static constexpr Byte stateBytes[9]{0x89,0x51,0x28,0x0f,0x57,0xd2,0x48,0x8b,0x0d};
        if(!std::equal(std::begin(stateBytes),std::end(stateBytes),g.base+0x1ae650) || !setOverworldCurrent(at)) return;
        log("host_follow_client_exit","\"state\":2,\"location\":%d",at);
        reinterpret_cast<void(__fastcall*)(void*,int)>(g.base+0x1ae650)(instance(),2);
        return;
    }
    if(s.size()==6 && s.rfind("STATE",0)==0 && (s[5]=='2' || s[5]=='6' || s[5]=='7')) {
        static constexpr Byte stateBytes[9]{0x89,0x51,0x28,0x0f,0x57,0xd2,0x48,0x8b,0x0d};
        if(!std::equal(std::begin(stateBytes),std::end(stateBytes),g.base+0x1ae650)) return;
        log("host_follow_client_exit","\"state\":%d",s[5]-'0');
        reinterpret_cast<void(__fastcall*)(void*,int)>(g.base+0x1ae650)(instance(),s[5]-'0');
        return;
    }
    static constexpr Byte changeBytes[16]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0xc7,0x41,0x28,0x04,0x00,0x00};
    if(!std::equal(std::begin(changeBytes),std::end(changeBytes),g.base+0x1b54d0)) return;
    const std::string map="maps/"+s+".MAP";
    log("host_follow_client_exit","\"map\":\"%s\"",map.c_str());
    reinterpret_cast<void(__fastcall*)(void*,const char*)>(g.base+0x1b54d0)(instance(),map.c_str());
}
std::uint16_t pendingClientKeys{},sharedKeysSent{};std::uint32_t sharedKeysGen{};std::uint64_t worldOnTicks{};
std::vector<int> enemiesGone,pickupsTaken;std::uint32_t goneSentSerial{}; // this world: map enemies gone and map spheres taken (also before any client was here)
std::vector<std::pair<int,int>> switchesPressed; // this world: (face, event) of every switch pressed here, for clients that join later
#ifdef PSE_FEATURE_P14
bool p14Eligible();
#endif
void markCheckpoint16(unsigned slot,int id);
void worldSyncHost() {
    namespace w=pse::world;
    // Health never above the native cap of the host's difficulty: bodies made by the mod (P16 respawn, a client's body, travel) came with 200.
    if(gameplay() && !g.failed && !g.worldExit) {
        if(live(g.p1,primary()) && !p16Pending(0)) w::clampHealth(g.p1.actor,primary());
        for(unsigned s=1;s<=MaxRemote;++s) if(R(s).exists && live(R(s).p2,R(s).storage.bytes) && !p16Pending(s)) w::clampHealth(R(s).p2.actor,R(s).storage.bytes);
    }
    const bool on=gameplay() && !g.failed && !g.shutdown && !g.worldExit && g.network && g.network->running() && g.network->anySnapshotEligible() &&
        g.phase==Phase::Live && live(g.p1,primary()) && remotesLive();
    w::setActive(on);
    if(!on) {
        worldRequests.clear();pendingClientKeys=0;sharedKeysGen=0;sharedKeysSent=0;worldOnTicks=0;goneSentSerial=0;
        // Alone in the level (no client ready yet): still note the map enemies that vanish and the spheres taken, for whoever joins later.
        if(gameplay() && !g.failed && !g.shutdown && !g.worldExit && live(g.p1,primary())) {
            w::prepareHud();
            if(g.ticks%3==0) { static w::Enemy scratch[128];w::collectEnemies(scratch,128); }
            for(int id;(id=w::popEnemyGone())>=0;) enemiesGone.push_back(id);
            for(int id;(id=w::popPickup())>=0;) pickupsTaken.push_back(id);
            for(int f,e;w::popSwitch(f,e);) switchesPressed.push_back({f,e});
        }
        return;
    }
    ++worldOnTicks;
    while(!worldRequests.empty()) {
        const auto r=worldRequests.front();worldRequests.pop_front();
        if(r.interactionType==w::ReqMover) w::applyMover(w::moverFromWire(r.targetGeneration,r.targetEntityId));
        else if(r.interactionType==w::ReqKeys) pendingClientKeys|=static_cast<std::uint16_t>(r.targetEntityId&0xfu);
        else if(r.interactionType==w::ReqAchievement) w::applyAchievement(r.targetEntityId); // the client earned it: the host's Steam account gets it too
        else if(r.interactionType==w::ReqBreak) { if(w::applyBreak(static_cast<int>(r.targetEntityId))) g.network->publishWorldEvent(w::EvBreak,0,0,static_cast<std::int32_t>(r.targetEntityId)); }
        else if(r.interactionType==w::ReqMap) followClientExit(r.targetEntityId,r.targetGeneration);
        else if(r.interactionType==w::ReqSound && r.slotId>=1 && r.slotId<=MaxRemote) {   // a sound of that client's own player: the host hears it at its proxy, every other client too
            w::playPlayerSound(pse::native_proxy::actorOf(r.slotId),r.targetEntityId);
            g.network->publishWorldEvent(w::EvSound,r.slotId,0,static_cast<std::int32_t>(r.targetEntityId));
        }
        else if(r.interactionType==w::ReqPickup && r.slotId>=1 && r.slotId<=MaxRemote && R(r.slotId).exists && live(R(r.slotId).p2,R(r.slotId).storage.bytes)) {
#ifdef PSE_FEATURE_P14
            // That client's player touched a health/ammo sphere in its own game: its body here takes it (P14 gives it to that player only). A map
            // sphere then goes to everyone through popPickup below; a dynamic one (vase, enemy drop) is announced now by its mirror tag.
            if(p14Eligible()) {
                pse::p14::allowRemoteTouch(true);
                const int taken=w::takePickup(static_cast<int>(r.targetEntityId),R(r.slotId).p2.actor);
                pse::p14::allowRemoteTouch(false);
                if(taken==2) g.network->publishWorldEvent(w::EvPickup,0,0,static_cast<std::int32_t>(r.targetEntityId));
            }
#endif
        }
        else if(r.interactionType==w::ReqSelfDamage && r.slotId>=1 && r.slotId<=MaxRemote && R(r.slotId).exists && live(R(r.slotId).p2,R(r.slotId).storage.bytes)) // drowning, lava, fall
            w::applySelfDamage(R(r.slotId).p2.actor,static_cast<int>(r.targetEntityId),static_cast<int>(r.targetGeneration&0xffu));
        else if(r.interactionType==w::ReqCheckpoint && r.slotId>=1 && r.slotId<=MaxRemote) markCheckpoint16(r.slotId,static_cast<int>(r.targetEntityId)); // that player's scarab
        else if(r.interactionType==w::ReqTouch && r.slotId>=1 && r.slotId<=MaxRemote && R(r.slotId).exists && live(R(r.slotId).p2,R(r.slotId).storage.bytes)) {
            // One channel for everything the client's own player touched in its game (sync rework stage 2): its body here touches the same
            // actor natively (spheres through P14 to that player, weapons through P16, keys into the shared set); a scarab is that player's.
            const w::Touch t{static_cast<int>(r.targetEntityId),static_cast<std::uint8_t>(r.targetGeneration&0xffu)};
            if(t.kind==w::TouchScarab) markCheckpoint16(r.slotId,t.id);
#ifdef PSE_FEATURE_P14
            else if((t.kind==w::TouchHealth || t.kind==w::TouchAmmo) && p14Eligible()) {
                pse::p14::allowRemoteTouch(true);
                const int taken=w::applyTouch(t,R(r.slotId).p2.actor);
                pse::p14::allowRemoteTouch(false);
                if(taken==2) g.network->publishWorldEvent(w::EvPickup,0,0,t.id);
            }
#endif
            else if(t.kind==w::TouchKey || t.kind==w::TouchWeapon) w::applyTouch(t,R(r.slotId).p2.actor);
        }
        else if(r.interactionType==w::ReqHit && r.slotId>=1 && r.slotId<=MaxRemote && R(r.slotId).exists) // that client's player hit an enemy in its game
            w::applyHit(static_cast<int>(r.targetEntityId),static_cast<int>(r.targetGeneration&0xffffu),static_cast<int>((r.targetGeneration>>16)&0xffu),R(r.slotId).p2.actor);
        else if(r.interactionType==w::ReqScript) { const auto c=w::unpackScript(static_cast<int>(r.targetEntityId&0xffff),r.targetGeneration,static_cast<std::uint16_t>(r.targetEntityId>>16));w::applyScript(c.num,g.p1.actor,&c); } // the client crossed a story trigger: the host's player plays the scene too
    }
    for(int id;(id=w::popBreak())>=0;) g.network->publishWorldEvent(w::EvBreak,0,0,id); // a vase/urn broke here (host or its P2 body): everyone breaks it
    for(int id;(id=w::popCheckpoint())>=0;) markCheckpoint16(0,id); // the host's own scarab
    for(auto snd=w::popSound();snd!=0xffffffffu;snd=w::popSound()) g.network->publishWorldEvent(w::EvSound,0,0,static_cast<std::int32_t>(snd)); // the host's own player
    // Enemies of the map gone here and spheres of the map taken here: once to everyone now, and again to a client that became ready later
    // (it loaded the map with every enemy and sphere still in place).
    for(int id;(id=w::popEnemyGone())>=0;) { enemiesGone.push_back(id);g.network->publishWorldEvent(w::EvEnemyGone,0,0,id); }
    for(int id;(id=w::popPickup())>=0;) { pickupsTaken.push_back(id);g.network->publishWorldEvent(w::EvPickup,0,0,id); }
    for(int f,e;w::popSwitch(f,e);) { switchesPressed.push_back({f,e});g.network->publishWorldEvent(w::EvSwitch,0,static_cast<std::uint32_t>(e),f); } // a switch pressed here: its texture for everyone
    if(g.network->joinSerial()!=goneSentSerial) {
        goneSentSerial=g.network->joinSerial();
        for(const int id:enemiesGone) g.network->publishWorldEvent(w::EvEnemyGone,0,0,id);
        for(const int id:pickupsTaken) g.network->publishWorldEvent(w::EvPickup,0,0,id);
        for(const auto& [f,e]:switchesPressed) g.network->publishWorldEvent(w::EvSwitch,0,static_cast<std::uint32_t>(e),f);
        if(const int d=w::difficulty();d>=0) { g.network->publishWorldEvent(w::EvDifficulty,0,0,d);log("world_difficulty_sent","\"difficulty\":%d,\"join_serial\":%u",d,goneSentSerial); } // the host's rules (health bar, sphere amounts) for every player
        if(!enemiesGone.empty() || !pickupsTaken.empty() || !switchesPressed.empty())
            log("world_removals_resent","\"enemies\":%zu,\"pickups\":%zu,\"switches\":%zu,\"join_serial\":%u",enemiesGone.size(),pickupsTaken.size(),switchesPressed.size(),goneSentSerial);
    }
    for(auto c=w::popScript();c.num>=0;c=w::popScript()) g.network->publishWorldEvent(w::EvScript,c.pos?w::packXY(c.x,c.y):0,c.pos?w::packZ(c.z):0,c.num); // the host (or P2) crossed one: the client's player plays it too
    if(worldOnTicks==320) w::debugScript(g.p1.actor);
    if(const auto ach=w::popAchievements()) // earned here (host or P2): every other player gets it too
        for(unsigned i=0;i<20;++i) if(ach>>i&1) g.network->publishWorldEvent(w::EvAchievement,0,0,static_cast<std::int32_t>(i));
    w::debugAchievement();
    if(worldOnTicks>=240 && worldOnTicks%10==0) {   // smoke only: PSE_DEBUG_CHANGE_MAP="maps/KARNAK.MAP" + PSE_DEBUG_MAP_FILE=<flag file> = what the exit trigger does
        static bool fired{};char name[64]{},file[MAX_PATH]{};
        if(!fired && GetEnvironmentVariableA("PSE_DEBUG_CHANGE_MAP",name,sizeof(name)) && GetEnvironmentVariableA("PSE_DEBUG_MAP_FILE",file,sizeof(file)) && GetFileAttributesA(file)!=INVALID_FILE_ATTRIBUTES) {
            fired=true;log("debug_change_map","\"map\":\"%s\"",name);
            reinterpret_cast<void(__fastcall*)(void*,const char*)>(g.base+0x1b54d0)(instance(),name);
        }
        char st[8]{};   // smoke only: PSE_DEBUG_END=6|7 + PSE_DEBUG_MAP_FILE = the game's own EndGame(bool) request (GameInstance vtable+0x98)
        if(!fired && GetEnvironmentVariableA("PSE_DEBUG_END",st,sizeof(st)) && GetEnvironmentVariableA("PSE_DEBUG_MAP_FILE",file,sizeof(file)) && GetFileAttributesA(file)!=INVALID_FILE_ATTRIBUTES) {
            fired=true;const int state=std::atoi(st);log("debug_end_game","\"state\":%d",state);
            if(state==6||state==7) reinterpret_cast<void(__fastcall*)(void*,int)>(g.base+0x1ae650)(instance(),state);
            if(state==2) { const int at=overworldCurrent()+1;log("debug_overworld","\"location\":%d,\"travel\":%s",at,travelToOverworld(at)?"true":"false"); } // the level exit: map screen at the next location
        }
    }
    w::popKeysGained(); // P1/P2 pickups are caught by the union below
    const auto k1=read<std::uint16_t>(primary(),0x98);
    std::uint16_t merged=static_cast<std::uint16_t>((k1|pendingClientKeys)&0xfu);pendingClientKeys=0;
    for(unsigned s=1;s<=MaxRemote;++s) if(R(s).exists) merged|=static_cast<std::uint16_t>(read<std::uint16_t>(R(s).storage.bytes,0x98)&0xfu);
    if(merged!=(k1&0xfu)) w::applyKeys(primary(),g.p1.actor,merged);
    for(unsigned s=1;s<=MaxRemote;++s) if(R(s).exists) {
        auto* kp=reinterpret_cast<std::uint16_t*>(R(s).storage.bytes+0x98);
        if(merged!=(*kp&0xfu)) *kp=static_cast<std::uint16_t>((*kp&~0xfu)|merged);
    }
    // joinSerial() moves whenever any client became ready: a late joiner gets the shared keys too
    if(merged!=sharedKeysSent || g.network->joinSerial()!=sharedKeysGen)
        if(g.network->publishWorldEvent(w::EvKeys,0,0,merged)) { sharedKeysSent=merged;sharedKeysGen=g.network->joinSerial();log("world_keys_sent","\"keys\":%u",merged); }
    {
        // Milestone 3: the host owns AI and damage; the client only mirrors. Enemies go out at 20 Hz; enemy projectiles are fast, so while
        // any exist they go out EVERY tick (first in the snapshot = first batch) - a fireball must be seen in flight to be dodged.
        static w::Projectile projectiles[16];
        const int pc=w::collectProjectiles(projectiles,16);
        const bool enemyTick=worldOnTicks%3==0,platformTick=worldOnTicks%15==0;
        if(enemyTick || pc>0) {
            static w::Enemy enemies[128];static w::Dynamic dynamics[64];
            const int n=enemyTick ? w::collectEnemies(enemies,128) : 0;
            const int dn=enemyTick ? w::collectDynamic(dynamics,64) : 0;
            std::vector<pse::EntitySnapshot> out;out.reserve(static_cast<std::size_t>(n)+static_cast<std::size_t>(pc)+static_cast<std::size_t>(dn));
            static w::Platform platforms[128];
            const int pn=platformTick ? w::collectPlatforms(platforms,128) : 0;
            static w::SectorState sectors[160];   // stage 4: sector heights - every sector that ever moved at 4 Hz, the moving ones at 20 Hz
            const int sn=enemyTick ? w::collectSectors(sectors,160,!platformTick) : 0;
            for(int i=0;i<sn;++i) {
                pse::EntitySnapshot s;s.entityId=static_cast<std::uint32_t>(static_cast<std::uint16_t>(sectors[i].sector));s.generation=1;s.type=5;s.posX=sectors[i].floor;s.posY=sectors[i].ceiling;
                out.push_back(s);
            }
            for(int i=0;i<pn;++i) {   // floating platform tick / drop pad state, type 4 (worldOnTicks%15 is also an enemy tick)
                pse::EntitySnapshot s;s.entityId=static_cast<std::uint32_t>(platforms[i].sector);s.generation=1;s.type=4;s.subType=platforms[i].kind;s.meta=platforms[i].value;
                out.push_back(s);
            }
            for(int i=0;i<dn;++i) {   // spawned during play (vase scorpion, dropped sphere ...): type 3, the client mirrors it by its actor type
                const auto& d=dynamics[i];pse::EntitySnapshot s;
                s.entityId=d.id;s.generation=1;s.type=3;s.subType=d.type;s.posX=d.x;s.posY=d.y;s.posZ=d.z;s.velX=d.vx;s.velY=d.vy;s.velZ=d.vz;s.yaw=d.yaw;s.pitch=d.scale;
                s.health=d.hp;s.stateFlags=static_cast<std::uint32_t>(d.aiState)|(static_cast<std::uint32_t>(static_cast<std::uint16_t>(d.sector))<<16);
                s.meta=(static_cast<std::uint32_t>(d.anim)<<16)|d.frame;
                out.push_back(s);
            }
            for(int i=0;i<pc;++i) {
                const auto& p=projectiles[i];pse::EntitySnapshot s;
                s.entityId=p.id;s.generation=1;s.type=2;s.subType=p.type;s.posX=p.x;s.posY=p.y;s.posZ=p.z;s.velX=p.vx;s.velY=p.vy;s.velZ=p.vz;s.yaw=p.yaw;
                s.stateFlags=static_cast<std::uint32_t>(static_cast<std::uint16_t>(p.sector));s.meta=p.anim;
                out.push_back(s);
            }
            for(int i=0;i<n;++i) {
                const auto& e=enemies[i];pse::EntitySnapshot s;
                s.entityId=e.id;s.generation=1;s.type=1;s.subType=e.anim;s.posX=e.x;s.posY=e.y;s.posZ=e.z;s.velX=e.vx;s.velY=e.vy;s.velZ=e.vz;s.yaw=e.yaw;
                s.health=e.hp;s.stateFlags=static_cast<std::uint32_t>(e.aiState)|(static_cast<std::uint32_t>(static_cast<std::uint16_t>(e.sector))<<16);s.meta=e.frame;
                out.push_back(s);
            }
            g.network->publishWorldSnapshot(std::move(out));
        }
    }
    w::Mover m;
    while(w::popMover(m)) g.network->publishWorldEvent(w::EvMover,m.cls,static_cast<std::uint32_t>(m.sector),static_cast<std::int32_t>(w::moverToWire(m)));
    if(worldOnTicks==240) w::census("host_world_live");
    if(worldOnTicks==200) w::debugTeleport(g.p1.actor);
    if(worldOnTicks==300) w::debugSpawn();
    if(worldOnTicks==400) w::debugKeys(primary());
    if(worldOnTicks==500) w::debugKillEnemy();
    const unsigned dbg=firstRemote();Byte* const dbgActor=dbg?R(dbg).p2.actor:nullptr;
    if(dbgActor && worldOnTicks>=230 && worldOnTicks%10==0) {w::debugPullEnemy(dbgActor);w::debugCensusOnFile();w::debugRemove();}
    if(dbgActor && worldOnTicks>=230) w::debugFireball(dbgActor);
    static bool grantedOnce{};
    if(worldOnTicks==300 && !grantedOnce && dbg) { Ctx dctx(dbg);   // smoke only: PSE_DEBUG_P2_GRANT="weapon,ammo" gives host P2 a weapon and its ammo (once per process: the next map must carry it)
        grantedOnce=true;char v[32]{};int wp=-1,am=0;
        if(GetEnvironmentVariableA("PSE_DEBUG_P2_GRANT",v,sizeof(v)) && sscanf_s(v,"%d,%d",&wp,&am)>=1 && wp>=0 && wp<8) {
            *(player()+0xcc+wp)=1;*reinterpret_cast<std::uint16_t*>(player()+0xbc+2*wp)=static_cast<std::uint16_t>(am);
            log("debug_p2_grant","\"weapon\":%d,\"ammo\":%d",wp,am);
        }
    }
    debugKillP2(worldOnTicks);
}
#endif
LocalState localState() {
    LocalState s;
    for (std::size_t i = 0; i < s.input.size(); ++i) s.input[i] = read<std::uint32_t>(primary(), i * 4);
    for (std::size_t i = 0; i < 8; ++i) { s.owned[i] = read<Byte>(primary(), 0xcc + i); s.ammo[i] = read<std::uint16_t>(primary(), 0xbc + i * 2); }
    s.node = read<Byte*>(primary(), 0xd8); s.actor = g.p1.actor; s.owner = read<Byte*>(s.actor, 0x1b8);
    s.health = read<std::int16_t>(s.actor, 0x17a); s.animation = read<void*>(primary(), 0xe8);
    s.weapon = read<int>(primary(), 0x88); s.pending = read<int>(primary(), 0x8c);
    return s;
}

void snapshot(const char* name) {
    if (!live(g.p1, primary()) || !live(R().p2, player())) { block("snapshot_identity"); return; }
    const Vec3 a = position(g.p1.actor), b = position(R().p2.actor);
    log(name,
        "\"p1_player\":\"%p\",\"p1_actor\":\"%p\",\"p1_node\":\"%p\",\"p1_owner\":\"%p\",\"p1_health\":%d,\"p1_pos\":[%.6f,%.6f,%.6f],"
        "\"p2_player\":\"%p\",\"p2_actor\":\"%p\",\"p2_node\":\"%p\",\"p2_owner\":\"%p\",\"p2_health\":%d,\"p2_pos\":[%.6f,%.6f,%.6f],"
        "\"p2_refs\":%d,\"p2_commands\":%u,\"p2_updates\":%llu,\"p2_puppet_ticks\":%llu,\"guards\":%s,\"generation\":1",
        primary(), g.p1.actor, g.p1.node, read<Byte*>(g.p1.actor, 0x1b8), read<std::int16_t>(g.p1.actor, 0x17a), a.x,a.y,a.z,
        player(), R().p2.actor, R().p2.node, read<Byte*>(R().p2.actor, 0x1b8), read<std::int16_t>(R().p2.actor, 0x17a), b.x,b.y,b.z,
        read<int>(R().p2.node, 0x40), R().commandCalls, R().secondaryUpdates, R().secondaryPuppetTicks, guards() ? "true" : "false");
}
bool prefix(profile::Index i) { return std::equal(std::begin(profile::Sites[i].prefix), std::end(profile::Sites[i].prefix), static_cast<Byte*>(at(i))); }
bool setHook(profile::Index i, bool enable) {
    if (!thread()) { block("hook_mutation_thread"); return false; }
    if (g.enabled[i] == enable) return true;
    const MH_STATUS status = enable ? MH_EnableHook(at(i)) : MH_DisableHook(at(i));
    if (status != MH_OK) { log("hook_error", "\"site\":\"%s\",\"status\":%d", profile::Sites[i].name, status); block("hook_change"); return false; }
    g.enabled[i] = enable;
    return true;
}
#ifdef PSE_FEATURE_P14
bool p14Eligible() {
    return thread() && g.inTick && g.schedule && g.phase==Phase::Live &&
        !g.failed && !g.worldExit && !g.shutdown && !g.backgroundStopping && gameplay() &&
        g.network && g.network->anyWantsPlayer();
}
pse::p14::Roster p14Roster() {
    pse::p14::Roster result{};
    if (!p14Eligible()) return result;
    if (live(g.p1,primary())) result[0]={primary(),g.p1.actor,0};
    for (unsigned s = 1; s <= MaxRemote; ++s) if (R(s).exists && live(R(s).p2, R(s).storage.bytes)) result[s] = {R(s).storage.bytes, R(s).p2.actor, s};
    return result;
}
// The Player() getter hook stays on once a P14 scope needed it: outside a scope hookGetter only forwards (scopedPlayer() is null). It used to be
// switched on and off around every scope, and MinHook's enable/disable suspends every thread of the game: a player body standing on a sphere it
// cannot take (full health, or its weapon in hand has no ammo type) touched it every tick and the host stuttered there. restoreControls() and the
// spawn below still switch it off exactly (prefix check) at world exit.
bool p14Getter(bool enable) {
    if (!thread() || g.scope) return false;
    return !enable || g.enabled[profile::Getter] || setHook(profile::Getter,true);
}
int p14HeldWeapon(unsigned slot) {
    if (!g.network || slot<1 || slot>MaxRemote) return -1;
    const auto want=g.network->selectedWeapon(slot);
    return want<8 ? static_cast<int>(want) : -1;
}
void p14Event(const char* name,unsigned slot,unsigned value) {
    if (strcmp(name,"p14_no_visible_player")==0 && ++g.p14QuietEvents>4 && g.p14QuietEvents%600) return;
    if (strncmp(name,"p14_touch_",10)==0) { static std::uint64_t touches=0;if (++touches>30 && touches%600) return; } // a body standing on a sphere touches it every tick
    if (slot==0 && strcmp(name,"p14_ankh_granted")==0) {
        ++g.ankhSerial; g.ankhPrimaryHealth=read<std::int16_t>(g.p1.actor,0x17a);
    }
    log(name,"\"slot\":%u,\"value\":%u,\"tick\":%llu",slot,value,g.ticks);
}
#endif
bool restoreControls() {
    g.schedule = false; g.scope = false;
    bool ok = true;
    for (const auto i : {profile::Update, profile::Turning, profile::Commands, profile::Getter}) {
        ok = setHook(i, false) && ok;
        const bool exact = prefix(i); ok = exact && ok;
        log("control_restore", "\"site\":\"%s\",\"exact16\":%s", profile::Sites[i].name, exact ? "true" : "false");
    }
    if (!ok) block("control_restore");
    return ok;
}

#ifdef PSE_ROLE_HOST
using Respond = std::uint64_t(__fastcall*)(void*, void*);
void clearPrimaryCommands() {
    // Proven native command block only: buttons, 13 held counters, turn/axes/deltas.
    // No Player copy and no actor/physics/health/inventory/weapon writes.
    for (std::size_t offset=0; offset<=0x7c; offset+=4)
        *reinterpret_cast<std::uint32_t*>(primary()+offset)=0;
}
bool primaryNeutral(void* p) {
    return p==primary() && thread() && g.inTick && g.neutralPrimary && !g.backgroundStopping;
}
void endFocusLease() {
    if (!g.focusLease.applied) return;
    if (!thread()) { block("focus_restore_thread"); return; }
    auto& value=*reinterpret_cast<Byte*>(instance()+background::FocusByte);
    g.focusLease.release(value,g.focusSerial);
    if (g.focusKnown && value != (g.focusLost ? 1 : 0)) block("focus_restore_contract");
}
std::uint64_t __fastcall hookFocusResponder(void* self, void* event) {
    // Observe the original decision; never rewrite/drop an SDL/KEX event.
    if (!thread() || self!=instance()) {
        ++g.foreignCalls; block("focus_responder_identity_or_thread");
        return reinterpret_cast<Respond>(g.focusOriginal)(self,event);
    }
    if (g.backgroundStopping || !event) return reinterpret_cast<Respond>(g.focusOriginal)(self,event);
    const auto type=read<std::uint32_t>(event), reason=type==13 ? read<std::uint32_t>(event,16) : 0;
    const bool focus=type==13 && (reason==1 || reason==2);
    const auto result=reinterpret_cast<Respond>(g.focusOriginal)(self,event);
    if (focus && !g.backgroundStopping) {
        ++g.focusSerial;
        const auto value=read<Byte>(instance(),background::FocusByte);
        g.focusKnown=value==(reason==2 ? 1 : 0); g.focusLost=g.focusKnown && reason==2;
        log("native_focus_decision", "\"type\":%u,\"reason\":%u,\"focus_byte\":%u,\"known\":%s,\"serial\":%llu,\"menu\":%u,\"held_forward\":%u,\"held_back\":%u,\"buttons\":%u,\"held_attack\":%u,\"yaw_delta\":%.9g,\"pitch_delta\":%.9g",
            type,reason,value,g.focusKnown ? "true" : "false",g.focusSerial,read<Byte>(instance(),background::MenuByte),read<unsigned>(primary(),12),read<unsigned>(primary(),16),read<unsigned>(primary()),read<unsigned>(primary(),4),read<float>(primary(),0x38),read<float>(primary(),0x3c));
    }
    return result;
}
#endif

void __fastcall hookCommands(void* p) {
#ifdef PSE_ROLE_HOST
    if (primaryNeutral(p)) {
        clearPrimaryCommands();
        // Keep native consumption of local queues; discard their local result.
        reinterpret_cast<Unary>(g.original[profile::Commands])(p);
        clearPrimaryCommands(); return;
    }
#endif
    if (!(thread() && g.inTick && g.p2Update && p == player())) {
        reinterpret_cast<Unary>(g.original[profile::Commands])(p); return;
    }
    ++R().commandCalls;
#ifdef PSE_ROLE_HOST
    const std::uint32_t buttons = R().appliedButtons;
#else
    const auto n = R().secondaryUpdates;
    const std::uint32_t buttons = n <= 45 ? 8u : n > 60 && n <= 105 ? 4u : 0u;
#endif
    own<std::uint32_t>(0, buttons);
    for (unsigned i = 0; i < 13; ++i) { R().held[i] = buttons & (1u << i) ? R().held[i] + 1 : 0; own<std::uint32_t>(4 + 4 * i, R().held[i]); }
    for (std::size_t offset = 0x38; offset <= 0x7c; offset += 4) own<std::uint32_t>(offset, 0);
}
void __fastcall hookTurning(void* p) {
    if (thread() && g.inTick && g.p2Update && p == player()) return;
    reinterpret_cast<Unary>(g.original[profile::Turning])(p);
#ifdef PSE_ROLE_HOST
    if (primaryNeutral(p)) clearPrimaryCommands();
#endif
}
void* __fastcall hookGetter(void* p) {
#ifdef PSE_FEATURE_P14
    if (thread() && g.inTick && p==instance())
        if (auto* selected=pse::p14::scopedPlayer(GetCurrentThreadId(),_ReturnAddress())) return selected;
#endif
    if (thread() && g.inTick && g.scope && p == instance()) { ++g.resolverHits; return player(); }
    return reinterpret_cast<Getter>(g.original[profile::Getter])(p);
}
#ifdef PSE_ROLE_HOST
// v1.14: the client simulates itself; P2 mirrors its view and position so the host's P2 uses doors, shoots and is seen by AI
// exactly where and how the client really stands. Called right before P2's native Update.
void applyRemotePose() {
    {   // P2's weapon in hand follows the client's (the engine switches through its own lower/raise animation via the pending field)
        const auto want=g.network->selectedWeapon(g.cur);Byte* pl=player();
        if(want<8) {
            const int active=read<int>(pl,0x88),pending=read<int>(pl,0x8c);
            if(want!=active && pending==active && read<Byte>(pl,0xcc+want)==1) *reinterpret_cast<int*>(pl+0x8c)=want;
        }
    }
    const auto& pose=g.network->pose(g.cur);
    if(!pose.valid || !live(R().p2,player())) return;
    Byte* a=R().p2.actor;
    if(std::isfinite(pose.yaw) && std::isfinite(pose.pitch)) {
        *reinterpret_cast<float*>(a+0x38)=pose.yaw;*reinterpret_cast<float*>(a+0x30)=pose.pitch;
    }
    const Vec3 v=position(a);
    const float dx=pose.x-v.x,dy=pose.y-v.y,dz=pose.z-v.z;
    if(dx*dx+dy*dy>64.0f || std::fabs(dz)>8.0f) {
        const bool ok=pse::world::teleport(a,pose.x,pose.y,pose.z,pose.sector);
        if(ok) ++R().p2Teleports; else if(++R().p2TeleportRefused%120==1) log("p2_pose_refused","\"target\":[%.1f,%.1f,%.1f],\"sector\":%d,\"p2\":[%.1f,%.1f,%.1f],\"refused\":%llu",pose.x,pose.y,pose.z,pose.sector,v.x,v.y,v.z,R().p2TeleportRefused);
    }
    if(g.ticks%60==0) {
        const Vec3 w=position(a);
        log("p2_pose_applied","\"tick\":%llu,\"target\":[%.1f,%.1f,%.1f],\"p2\":[%.1f,%.1f,%.1f],\"yaw\":%.4f,\"p2_yaw\":%.4f,\"sector\":%d,\"p2_sector\":%d,\"teleports\":%llu",g.ticks,pose.x,pose.y,pose.z,w.x,w.y,w.z,pose.yaw,read<float>(a,0x38),pose.sector,read<int>(a,0x3c),R().p2Teleports);
    }
}
#endif
// One remote player's Player Update, run right after the host's own (the engine only ticks P1 itself): its input and pose come from its
// client, the native Update runs on its own Player block, and the host player must come out of it unchanged.
// Sounds of every player (weapon, jump, pain ...) are relayed by world_sync (EvSound/ReqSound): each machine sends its own player's.
void updateRemote(unsigned slot) {
    Ctx ctx(slot);
    if (!live(R().p2, player()) || !guards()) { block("controller_identity_or_guards"); return; }
    if (p16Pending(slot) || read<std::int16_t>(R().p2.actor, 0x17a) <= 0 || (read<unsigned>(R().p2.actor, 0x1c0) & 0x200u)) {
        // M0 C13/C19: a dead remote keeps native Puppet ticks; skip its Player Update. The owned identity stays for a death
        // presentation snapshot; the respawn state machine (tick16) brings it back.
        R().p2Tick=g.ticks;R().motionBefore=position(R().p2.actor);return;
    }
    const auto before = localState();
#ifdef PSE_FEATURE_P14
    const auto ankhSerial=g.ankhSerial;
#endif
#ifdef PSE_ROLE_HOST
    if (R().p2Tick == g.ticks) { block("duplicate_secondary_update"); return; }
    R().p2Tick = g.ticks;
    R().appliedSequence = g.network->sequence(g.cur);
    R().appliedGeneration = g.network->generation(g.cur);
    applyRemotePose();
    R().appliedButtons = g.network->nativeButtons(g.cur) | g.network->takeNativeEdges(g.cur);
    R().motionBefore = position(R().p2.actor);
    const auto commandsBefore = R().commandCalls;
#endif
    ++R().secondaryUpdates; g.p2Update = true;
    const int hudBefore = pse::world::setHudKind(3); // what this body does (door sealed, pickup ...) is shown to its own player only
    reinterpret_cast<Unary>(g.original[profile::Update])(player());
    pse::world::setHudKind(hudBefore);
    g.p2Update = false;
    ++g.inputComparisons;
    const auto after = localState();
#ifdef PSE_FEATURE_P14
    auto expected=before;
    if (g.ankhSerial!=ankhSerial) {
        // Shared native grant is the sole permitted P1 field change during P2 Update.
        // Keep comparison/log source honest: record actual pre-grant HP separately.
        log("p14_shared_ankh_during_update","\"health_before\":%d,\"expected_after\":%d,\"observed_after\":%d",before.health,g.ankhPrimaryHealth,after.health);
        expected.health=g.ankhPrimaryHealth;
    }
    const bool unchanged=expected==after;
#else
    const bool unchanged=before==after;
#endif
    if (!unchanged) block("p2_update_changed_local_input_or_identity");
#ifdef PSE_ROLE_HOST
    // Concrete fields for every application, not two isolated snapshots.
    auto fields = [](const LocalState& s) {
        std::array<char, 1800> out{};
        int n = sprintf_s(out.data(), out.size(), "{\"node\":\"%p\",\"actor\":\"%p\",\"owner\":\"%p\",\"health\":%d,\"weapon\":%d,\"pending\":%d,\"animation\":\"%p\",\"input\":[", s.node,s.actor,s.owner,s.health,s.weapon,s.pending,s.animation);
        for (std::size_t i = 0; i < s.input.size(); ++i) n += sprintf_s(out.data()+n, out.size()-n, "%s%u", i ? "," : "", s.input[i]);
        n += sprintf_s(out.data()+n, out.size()-n, "],\"owned\":[");
        for (std::size_t i = 0; i < s.owned.size(); ++i) n += sprintf_s(out.data()+n, out.size()-n, "%s%u", i ? "," : "", s.owned[i]);
        n += sprintf_s(out.data()+n, out.size()-n, "],\"ammo\":[");
        for (std::size_t i = 0; i < s.ammo.size(); ++i) n += sprintf_s(out.data()+n, out.size()-n, "%s%u", i ? "," : "", s.ammo[i]);
        sprintf_s(out.data()+n, out.size()-n, "]}");
        return out;
    };
        const auto a = fields(before), b = fields(after);
    if(g.cur<=1 || g.ticks%60==0) log("network_native_update", "\"tick\":%llu,\"slot\":%u,\"generation\":%u,\"input_seq\":%u,\"accepted_tick\":%llu,\"native_buttons\":%u,\"commands_this_update\":%u,\"p2_player\":\"%p\",\"p2_actor\":\"%p\",\"p1_before\":%s,\"p1_after\":%s,\"unchanged\":%s,\"focus_override\":%s,\"p1_neutral\":%s,\"foreground_pid\":%lu,\"primary_caller_rva\":%u", g.ticks,g.cur,R().appliedGeneration,R().appliedSequence,g.network->acceptedAt(g.cur),R().appliedButtons,R().commandCalls-commandsBefore,player(),R().p2.actor,a.data(),b.data(),unchanged ? "true" : "false",g.focusLease.applied ? "true" : "false",g.neutralPrimary ? "true" : "false",g.foregroundPid,background::PrimaryReturn);
#endif
}
void __fastcall hookUpdate(void* p) {
    // Prevent a duplicate/foreign primary call BEFORE executing native Update.
    if (p==primary()) {
        if (!thread() || !g.inTick) { ++g.foreignCalls; block("local_update_outside_attested_tick"); return; }
        if (g.localThisTick!=0) { block("duplicate_local_update"); return; }
        ++g.localThisTick;
#ifdef PSE_ROLE_HOST
        if (reinterpret_cast<Byte*>(_ReturnAddress())!=g.base+background::PrimaryReturn) { block("primary_update_not_native_state_tick"); return; }
        if (g.neutralPrimary) {
            if (g.bindingsResetPending) {
                reinterpret_cast<Final>(g.base+background::ResetBindings)();
                g.bindingsResetPending=false;
                log("local_bindings_reset", "\"tick\":%llu,\"native_rva\":%u,\"focus_lost\":%s",g.ticks,background::ResetBindings,g.focusLost ? "true" : "false");
            }
            clearPrimaryCommands();
        }
#endif
    }
    // The native StateTick remains the sole caller of P1 Update.
    reinterpret_cast<Unary>(g.original[profile::Update])(p);
    if (p != primary()) return;
    if (!thread() || !g.inTick) { ++g.foreignCalls; block("local_update_outside_attested_tick"); return; }
    ++g.localUpdates;
    if (g.localThisTick != 1) { block("duplicate_local_update"); return; }
#ifdef PSE_ROLE_HOST
    if (g.neutralPrimary) {
        for (std::size_t offset=0; offset<=0x7c; offset+=4)
            if (read<unsigned>(primary(),offset)!=0) { block("background_primary_input_not_neutral"); return; }
        ++g.neutralUpdates; g.resumeNeutral=false;
    }
    if (gameplay() && !g.worldExit && !g.shutdown) {
        log("native_primary_input", "\"tick\":%llu,\"buttons\":%u,\"held_attack\":%u,\"yaw_delta\":%.9g,\"pitch_delta\":%.9g,\"mouse_x\":%.9g,\"mouse_y\":%.9g,\"neutral\":%s,\"focus_lost\":%s,\"foreground_pid\":%lu",
            g.ticks,read<unsigned>(primary()),read<unsigned>(primary(),4),read<float>(primary(),0x38),read<float>(primary(),0x3c),read<float>(primary(),0x40),read<float>(primary(),0x44),g.neutralPrimary ? "true" : "false",g.focusLost ? "true" : "false",g.foregroundPid);
    }
#endif
    if (g.phase == Phase::Complete && g.inputSamples < 12 && read<std::uint32_t>(primary()) != 0 && live(g.p1, primary())) {
        const Vec3 a = position(g.p1.actor); ++g.inputSamples;
        log("local_input_after_sequence", "\"buttons\":%u,\"held_forward\":%u,\"held_back\":%u,\"pos\":[%.6f,%.6f,%.6f],\"p2_scheduled\":false",
            read<std::uint32_t>(primary()), read<std::uint32_t>(primary(), 12), read<std::uint32_t>(primary(), 16), a.x,a.y,a.z);
    }
    if (!g.schedule || !gameplay()) return;
    if (!live(g.p1, primary())) { block("controller_identity_or_guards"); return; }
    // M0 C19 seam: after P1 Update, before the remote Updates and this tick's
    // ActorTick/Sweep/Drain. Native Remove must not be issued at tick end.
    tick16();
    if(g.failed)return;
    for (unsigned slot = 1; slot <= MaxRemote && !g.failed; ++slot) if (R(slot).exists) updateRemote(slot);
}
void __fastcall hookPuppetTick(void* p) {
    if (thread() && !g.shutdown) {
        if (proxyInstalled && !proxyExited) pse::native_proxy::notePuppetTick(p);
        if (p == g.p1.actor && !g.p1.destroyed) ++g.primaryPuppetTicks;
        if (const unsigned rs = remoteSlotOfActor(p)) if (!R(rs).p2.destroyed) ++R(rs).secondaryPuppetTicks;
    }
    const bool remoteBody = remoteSlotOfActor(p) != 0;
    const int hudBefore = remoteBody ? pse::world::setHudKind(3) : 0;
    reinterpret_cast<Unary>(g.original[profile::PuppetTick])(p);
    if (remoteBody) pse::world::setHudKind(hudBefore);
}
void __fastcall hookDtor(void* p) {
    const int retiredIndex16=p16DtorEntry(p);
    const unsigned rs = remoteSlotOfActor(p);
    const bool owned = rs && !R(rs).p2.destroyed;
    if (owned && !thread()) { ++g.foreignCalls; block("owned_destructor_thread"); }
    if (owned) log("owned_actor_dtor_entry", "\"slot\":%u,\"actor\":\"%p\",\"external_handle_null\":%s", rs, p, read<Byte*>(R(rs).storage.bytes, 0xd8) ? "false" : "true");
    reinterpret_cast<Unary>(g.original[profile::ActorDtor])(p);
    p16DtorReturn(retiredIndex16,p);
    if (owned) {
        R(rs).p2.destroyed = true; ++g.dtorReturns;
        log("owned_actor_dtor_return", "\"slot\":%u,\"actor\":\"%p\",\"actor_reads_after_return\":false,\"guards\":%s", rs, p, guardsOf(R(rs)) ? "true" : "false");
    }
}
// pl = the Player block to read (P2's own by default; P1's kexPlayer for a late joiner); hp = health to carry (0: P2's actor health)
P2Persist captureP2(Byte* pl=player(), std::int16_t hp=0) {
    P2Persist c;
    for(unsigned i=0;i<8;++i) { c.weapons[i]=read<Byte>(pl,0xcc+i)?1:0; c.ammo[i]=read<std::uint16_t>(pl,0xbc+2*i); }
    c.ankhs=read<std::uint16_t>(pl,0x84);c.ankhMask=read<std::uint16_t>(pl,0x86);c.artifacts=read<std::uint16_t>(pl,0x90);
    c.abilities=read<std::uint16_t>(pl,0x94);c.quest=read<std::uint16_t>(pl,0x9a);c.dolls=read<std::uint32_t>(pl,0xa4);c.extra=read<std::uint32_t>(pl,0x194);
    c.weapon=read<int>(pl,0x88);
    if(!hp&&R().p2.actor) hp=read<std::int16_t>(R().p2.actor,0x17a);
    c.health=hp>0?hp:std::int16_t{200}; // a body that is dead at the exit does not carry its death over
    c.valid=true;return c;
}
// Same writes as RestorePersistentData (0x1b5ce0), into P2's own block; keys are per level (Ready() clears them) and are not carried.
void restoreP2(const P2Persist& c) {
    for(unsigned i=0;i<8;++i) { own<Byte>(0xcc+i,c.weapons[i]); own<std::uint16_t>(0xbc+2*i,c.ammo[i]); }
    own<std::uint16_t>(0x84,c.ankhs);own<std::uint16_t>(0x86,c.ankhMask);own<std::uint32_t>(0x90,c.artifacts);
    own<std::uint32_t>(0x94,c.abilities);own<std::uint16_t>(0x9a,c.quest);own<std::uint32_t>(0xa4,c.dolls);
    own<std::uint16_t>(0x190,static_cast<std::uint16_t>(c.health));own<std::uint32_t>(0x194,c.extra);
    const int w=c.weapon>=0&&c.weapon<8&&c.weapons[c.weapon]?c.weapon:0;
    own<int>(0x88,w);own<int>(0x8c,w);
    log("p2_travel_restored","\"weapons\":[%u,%u,%u,%u,%u,%u,%u,%u],\"ammo\":[%u,%u,%u,%u,%u,%u,%u,%u],\"weapon\":%d,\"health\":%d,\"artifacts\":%u,\"ankhs\":%u,\"dolls\":%u",
        c.weapons[0],c.weapons[1],c.weapons[2],c.weapons[3],c.weapons[4],c.weapons[5],c.weapons[6],c.weapons[7],c.ammo[0],c.ammo[1],c.ammo[2],c.ammo[3],c.ammo[4],c.ammo[5],c.ammo[6],c.ammo[7],w,c.health,c.artifacts,c.ankhs,c.dolls);
}
// Leaving a gameplay world for another one: take what P2 carries before its Player is reset.
void captureTravel() {
    const bool restart=read<Byte>(instance(),0x278)!=0; // Restart Level / checkpoint reload: the engine gives P1 its level-start inventory back
    for(unsigned slot=1;slot<=MaxRemote;++slot) {
        if(!R(slot).exists) continue; // never joined this world: whatever was carried in stays
        Ctx ctx(slot);
        if(!live(R().p2,player()) || R().key.empty()) continue;
        auto& carried=travelBy[R().key];
        carried=restart&&R().entry.valid?R().entry:captureP2();
        log("p2_travel_captured","\"slot\":%u,\"restart\":%s,\"weapon\":%d,\"weapons\":[%u,%u,%u,%u,%u,%u,%u,%u],\"ammo\":[%u,%u,%u,%u,%u,%u,%u,%u],\"health\":%d",slot,restart?"true":"false",carried.weapon,
            carried.weapons[0],carried.weapons[1],carried.weapons[2],carried.weapons[3],carried.weapons[4],carried.weapons[5],carried.weapons[6],carried.weapons[7],
            carried.ammo[0],carried.ammo[1],carried.ammo[2],carried.ammo[3],carried.ammo[4],carried.ammo[5],carried.ammo[6],carried.ammo[7],carried.health);
    }
}
// World change (next level, restart, back to title): the engine purges the proxy Actor itself. Latch first, then never call it again;
// the adapter hooks and the seam stay installed for the next world (same short path the client takes in worldTransition()).
void proxySoftExit() {
    if(!proxyInstalled || !thread()) return;
    if(proxies) proxies->forEach([](pse::proxy::VisualProxyStateMachine& p) { p.onPreUnload(); });
    pse::native_proxy::exitBarrier(1);
    log("proxy_world_transition","\"kept_hooks\":true");
}
void unbind(const char* reason) {
    const bool next=std::strcmp(reason,"StateExit")==0; // a world change is not the end of the process: hooks, UI request and permit stay
    if(next) proxySoftExit(); else proxyExit(reason,2u);
    if(!p16Restore())block("p16_restart_restore");
    if(!next) {
        if(!pse::weapon_p16::stop())block("p16_weapon_restore");
        pse::native_ui::stop();
    }
    if (!thread()) { ++g.foreignCalls; block("unbind_thread"); return; }
#ifdef PSE_FEATURE_P14
    if(!next) pse::p14::stop();
#endif
#ifdef PSE_FEATURE_P4
    g.p7KeysPermitted=false;g.p7KeyProbeSerial=g.p7KeyReviewSerial=0;
    g.anchorValid=false; // No spawn spot survives this world's barrier.
#endif
#ifdef PSE_ROLE_HOST
    g.backgroundStopping=true;
    endFocusLease(); g.neutralPrimary=false;
    // One session for the whole game: a world change keeps every client connected (only their readiness ends with this world).
    // DirectQuit ends it: the join completes while engine services and our callbacks/storage still live.
    if (g.network) { if (next) g.network->leaveWorld(reason); else g.network->stop(reason); }
    if (g.schedule) for (unsigned slot=1; slot<=MaxRemote; ++slot) if (R(slot).exists) {
        Ctx ctx(slot);
        own<std::uint32_t>(0, 0);
        for (unsigned i=0; i<13; ++i) { R().held[i]=0; own<std::uint32_t>(4+4*i, 0); }
    }
#endif
    restoreControls();
    unsigned open=0;
    for (unsigned slot=1; slot<=MaxRemote; ++slot) if (R(slot).exists && read<Byte*>(R(slot).storage.bytes, 0xd8)) ++open;
    if (!open) { log("unbind_already_empty", "\"reason\":\"%s\"", reason); return; }
    if (g.resetAttempted || !gameplay() || !live(g.p1, primary())) { block("unbind_abi_lifetime_gate"); return; }
    const auto before = localState(); g.resetAttempted = true;
    // Every remote's Player block is released exactly like the single P2 was: native Reset, handle must come back null.
    for (unsigned slot=1; slot<=MaxRemote; ++slot) {
        if (!R(slot).exists || !read<Byte*>(R(slot).storage.bytes, 0xd8)) continue;
        Ctx ctx(slot);
        if (!live(R().p2, player())) { block("unbind_abi_lifetime_gate"); return; }
        snapshot("before_unbind");
        log("native_call_attempt", "\"operation\":\"exit_reset\",\"slot\":%u,\"reason\":\"%s\"", slot, reason);
        reinterpret_cast<Unary>(at(profile::Reset))(player());
        const bool empty = read<Byte*>(player(), 0xd8) == nullptr;
        const bool unchanged = before == localState();
        log("native_call_return", "\"operation\":\"exit_reset\",\"slot\":%u,\"handle_null\":%s,\"p1_unchanged\":%s,\"guards\":%s", slot, empty ? "true" : "false", unchanged ? "true" : "false", guards() ? "true" : "false");
        if (!empty || !unchanged || !guards()) { block("unbind_postcondition"); return; }
    }
}
void __fastcall hookStateExit(void* self) {
    const bool teardown = thread() && self == instance() && read<int>(instance(), 0x24) == 3 && read<int>(instance(), 0x20) != 3;
    if (teardown && !g.worldExit) {
        g.worldExit = true;
        log("state_exit_entry", "\"next_state\":%d", read<int>(instance(), 0x20));
        captureTravel();
        unbind("StateExit"); g.phase = Phase::WorldExit;
    }
    reinterpret_cast<Unary>(g.original[profile::StateExit])(self);
    if (teardown) {
        ++g.stateExitReturns; g.phase = Phase::WorldExited;
        log("state_exit_return", "\"external_handle_null\":%s,\"owned_dtor_returns\":%llu,\"guards\":%s,\"actor_reads\":false,\"module_and_storage_pinned\":true",
            read<Byte*>(player(), 0xd8) ? "false" : "true", g.dtorReturns, guards() ? "true" : "false");
        recoverAfterBlock();
        // A fault that is not recoverable (process level) ends co-op for this run: the clients are told by the session's end.
        if (g.failed && g.network && g.network->running()) g.network->stop("blocked_permanently");
        rearmWorld();
    }
}
void __fastcall hookShutdown(void* self) {
    if (thread()) {
        log("shutdown_entry", "\"state_exit_preceded\":%s,\"live_external_handle\":%s", g.worldExit ? "true" : "false", read<Byte*>(player(), 0xd8) ? "true" : "false");
        unbind("DirectQuit"); g.shutdown = true; g.phase = Phase::Shutdown;
        log("shutdown_quiesced", "\"external_handle_null\":%s,\"guards\":%s,\"pin_until_process_exit\":true,\"forced_teardown\":false",
            read<Byte*>(player(), 0xd8) ? "false" : "true", guards() ? "true" : "false");
    } else { ++g.foreignCalls; block("shutdown_thread"); }
    reinterpret_cast<Unary>(g.original[profile::Shutdown])(self);
}
void __fastcall hookEngineShutdown(void* self) {
    log("engine_shutdown_entry", "\"adapter_quiesced\":%s", g.shutdown ? "true" : "false");
    reinterpret_cast<Unary>(g.original[profile::EngineShutdown])(self);
    log("engine_shutdown_return", "\"actor_reads\":false,\"module_pinned\":true");
}
void __fastcall hookFinal() {
    log("native_final_exit", "\"external_handle_null\":%s,\"guards\":%s,\"module_pinned\":true,\"storage_pinned\":true,\"ticks_after_shutdown\":%llu,\"foreign_calls\":%llu,\"failed\":%s,\"actor_reads\":false",
        read<Byte*>(player(), 0xd8) ? "false" : "true", guards() ? "true" : "false", g.shutdownTicks, g.foreignCalls, g.failed ? "true" : "false");
    reinterpret_cast<Final>(g.original[profile::FinalExit])();
}

#ifdef PSE_FEATURE_P4
bool spawnAnchorClear(bool quiet=false) {
    namespace pp=pse::native_proxy::profile;
    if(!g.anchorValid || !std::isfinite(g.anchorYaw)) return false;
    const auto count=read<std::uint64_t>(g.base,0xa45970);auto* sectors=read<Byte*>(g.base,0xa45968);
    if(!sectors || count>4096 || g.anchorSector<0 || static_cast<std::uint64_t>(g.anchorSector)>=count) return false;
    // Revalidate the native location; a cached coordinate alone is not proof that geometry is unchanged.
    for(const auto i:{pp::point_in_sector,pp::floor_height,pp::ceiling_height}) {
        const auto& site=pp::Sites[i];
        if(!std::equal(std::begin(site.prefix),std::end(site.prefix),g.base+site.rva)) return false;
    }
    auto* sector=sectors+static_cast<std::size_t>(g.anchorSector)*0x88;
    using Point=std::uint8_t(__fastcall*)(void*,const float*,float);
    using Height=float(__fastcall*)(const float*,void*,std::uint8_t);
    if(reinterpret_cast<Point>(g.base+pp::Sites[pp::point_in_sector].rva)(sector,g.anchor.position.data(),0)!=1) return false;
    const float floor=reinterpret_cast<Height>(g.base+pp::Sites[pp::floor_height].rva)(g.anchor.position.data(),sector,1);
    const float ceiling=reinterpret_cast<Height>(g.base+pp::Sites[pp::ceiling_height].rva)(g.anchor.position.data(),sector,1);
    if(!pse::placement::sameSupport(g.anchorFloor,g.anchorCeiling,floor,ceiling)) return false;
    // Include actors in adjoining sectors as their bounds can overlap the anchor. Read-only census.
    for(std::size_t i=0;i<count;++i) {
        auto* head=read<Byte*>(sectors+i*0x88,0x78);if(!head || read<Byte*>(head)) return false;
        auto* previous=head;auto* node=read<Byte*>(head,8);unsigned walked=0;
        while(node && read<Byte*>(node,8)) {
            if(++walked>4096 || read<Byte*>(node)!=previous || read<Byte*>(previous,8)!=node) return false;
            auto* actor=read<Byte*>(node,0x28);if(!actor || read<Byte*>(actor,0x10)!=node) return false;
            const pse::placement::Box other{read<pse::bounded::Vec3>(actor,0x18),read<pse::bounded::Vec3>(actor,0xec),read<pse::bounded::Vec3>(actor,0xf8)};
            if(!pse::placement::separate(g.anchor,other)) {
                if(!quiet && g.ticks%120==0)log("p4_anchor_occupied","\"sector\":%zu,\"actor\":\"%p\",\"position\":[%.9g,%.9g,%.9g],\"bounds_min\":[%.9g,%.9g,%.9g],\"bounds_max\":[%.9g,%.9g,%.9g],\"flags\":%u",i,actor,other.position[0],other.position[1],other.position[2],other.lo[0],other.lo[1],other.lo[2],other.hi[0],other.hi[1],other.hi[2],read<unsigned>(actor,0x16c));
                return false;
            }
            previous=node;node=read<Byte*>(node,8);
        }
        if(!node || read<Byte*>(node)!=previous) return false;
    }
    return true;
}
// Pick a clear spot beside the host's CURRENT position (right, left, behind, then farther out) in the host's own sector:
// native point-in-sector, identical floor/ceiling support and a read-only census of every actor. No fixed anchor, no timeout:
// it is retried every tick until a spot is free.
bool chooseSpawn() {
    auto* n=read<Byte*>(primary(),0xd8);auto* a=n?read<Byte*>(n,0x28):nullptr;
    if(!a || read<Byte*>(a,0x10)!=n || read<Byte*>(a,0x1b8)!=primary()) return false;
    const pse::placement::Box base{read<pse::bounded::Vec3>(a,0x18),read<pse::bounded::Vec3>(a,0xec),read<pse::bounded::Vec3>(a,0xf8)};
    const float yaw=read<float>(a,0x38);
    if(!pse::placement::valid(base) || !std::isfinite(yaw)) return false;
    const float fx=std::sin(yaw),fy=std::cos(yaw),rx=fy,ry=-fx; // forward = (sin,cos); right = (cos,-sin)
    struct Step{float right,forward,distance;};
    // Up to seven remote bodies stand around the host: more rings farther out, every spot is still checked against all actors.
    static constexpr Step steps[]={{1,0,96},{-1,0,96},{0,-1,96},{1,0,140},{-1,0,140},{0,-1,140},{0,1,96},{1,-1,120},{-1,-1,120},
        {1,0,190},{-1,0,190},{0,-1,190},{0,1,190},{1,-1,170},{-1,-1,170},{1,1,170},{-1,1,170},
        {1,0,240},{-1,0,240},{0,-1,240},{0,1,240},{1,-1,220},{-1,-1,220},{1,1,220},{-1,1,220}};
    for(const auto& s:steps) {
        const float len=std::sqrt(s.right*s.right+s.forward*s.forward);
        pse::placement::Box c=base;
        c.position[0]=base.position[0]+s.distance*(rx*s.right+fx*s.forward)/len;
        c.position[1]=base.position[1]+s.distance*(ry*s.right+fy*s.forward)/len;
        g.anchor=c;g.anchorYaw=yaw;g.anchorSector=read<int>(a,0x3c);
        g.anchorFloor=read<float>(a,0x11c);g.anchorCeiling=read<float>(a,0x120);
        g.anchorValid=true;
        if(spawnAnchorClear(true)) {
            log("p4_spawn_chosen","\"sector\":%d,\"host\":[%.9g,%.9g,%.9g],\"spawn\":[%.9g,%.9g,%.9g],\"right\":%.0f,\"forward\":%.0f,\"distance\":%.0f",
                g.anchorSector,base.position[0],base.position[1],base.position[2],c.position[0],c.position[1],c.position[2],s.right,s.forward,s.distance);
            return true;
        }
    }
    g.anchorValid=false;
    return false;
}
#endif

// Primary-player scalars compared around a respawn: the host player must be untouched.
struct P7PrimaryScalars {
    LocalState state;std::array<std::uint32_t,9> scalar{};
    bool operator==(const P7PrimaryScalars&)const=default;
};
P7PrimaryScalars p7PrimaryScalars(){
    P7PrimaryScalars s;s.state=localState();
    const std::size_t offsets[]={0x84,0x88,0x8c,0x90,0x94,0x98,0x9c,0xa0,0xa4};
    for(unsigned i=0;i<9;++i)s.scalar[i]=read<std::uint32_t>(primary(),offsets[i]);return s;
}
bool p16Eligible(){
    return thread()&&g.inTick&&g.schedule&&g.phase==Phase::Live&&!g.failed&&!g.worldExit&&!g.shutdown&&
        gameplay()&&g.network&&g.network->anyWantsPlayer();
}
bool p16WeaponEligible(){
    return thread()&&g.inTick&&!g.failed&&!g.worldExit&&!g.shutdown&&gameplay()&&g.network&&g.network->running();
}
pse::weapon_p16::Roster p16Roster(){
    pse::weapon_p16::Roster r{};
    if(p16WeaponEligible()){
        pse::life_p16::Graph p1{primary()};
        if(pse::life_p16::graph(g.base,p1))r[0]={primary(),p1.actor,0,!p16Pending(0)};
        for(unsigned s=1;s<=MaxRemote;++s)if(R(s).exists){
            pse::life_p16::Graph p2{R(s).storage.bytes};
            if(pse::life_p16::graph(g.base,p2))r[s]={R(s).storage.bytes,p2.actor,static_cast<std::uint8_t>(s),!p16Pending(s)};
        }
    }
    return r;
}
void p16Event(const char* name,unsigned slot,unsigned value){
    log(name,"\"slot\":%u,\"value\":%u",slot,value);
}
void p16Grant(const pse::weapon_p16::Grant& r){
    log("p16_weapon_grant_receipt","\"slot\":%u,\"weapon\":%u,\"generation\":%u,\"epoch\":%u,\"player\":\"%p\",\"puppet\":\"%p\",\"pickup\":\"%p\",\"node\":\"%p\",\"persistent\":%s,\"ammo_before\":[%u,%u,%u,%u,%u,%u,%u,%u],\"ammo_after_equal\":true",r.slot,r.weapon,g.entityGeneration[r.slot],g.network->mapEpoch(),r.player,r.puppet,r.pickup,r.node,r.persistent?"true":"false",r.ammo[0],r.ammo[1],r.ammo[2],r.ammo[3],r.ammo[4],r.ammo[5],r.ammo[6],r.ammo[7]);
}
#include "host_respawn_p16.inc"
void debugKillP2(std::uint64_t worldTicks){
    static const int at=[]{char v[16]{};return GetEnvironmentVariableA("PSE_DEBUG_KILL_P2",v,sizeof(v))?std::atoi(v):0;}();
    if(at>0&&worldTicks==static_cast<std::uint64_t>(at)){unsigned empty=0;fixture16.compare_exchange_strong(empty,2);}
    static bool fileFired=false;char file[MAX_PATH]{}; // or when PSE_DEBUG_KILL_P2_FILE appears (once)
    if(!fileFired&&worldTicks%10==0&&GetEnvironmentVariableA("PSE_DEBUG_KILL_P2_FILE",file,sizeof(file))&&GetFileAttributesA(file)!=INVALID_FILE_ATTRIBUTES){fileFired=true;unsigned empty=0;fixture16.compare_exchange_strong(empty,2);log("debug_kill_p2_file","\"world_ticks\":%llu",worldTicks);}
}
// Called right after the engine's own StateExit returned: everything per-world is dropped (bodies, proxies, respawn team, world sync).
// The network Session is NOT: it lives until the host quits, the next world re-binds it to its map with a new epoch (rebindNativeMap).
// The world that had the fault is gone (the engine's StateExit returned): every body and proxy of it is destroyed. The next world gets a fresh
// session as usual, so a world-level fault no longer needs a game restart (it used to: the session stayed dead and clients could not rejoin).
void recoverAfterBlock() {
    if(!g.failed || g.shutdown || !thread()) return;
    bool clean=!processFault(lastBlock) && blockRecoveries<16;
    for(unsigned s=1;s<=MaxRemote && clean;++s) if(R(s).exists && (read<Byte*>(R(s).storage.bytes,0xd8) || !guardsOf(R(s)))) clean=false; // bodies of that world
    if(!clean) { log("host_block_kept","\"reason\":\"%s\",\"recoveries\":%u",lastBlock,blockRecoveries);return; }
    ++blockRecoveries;g.failed=false;
    log("host_recovered_after_block","\"reason\":\"%s\",\"recoveries\":%u",lastBlock,blockRecoveries);
    lastBlock[0]=0;
}
void rearmWorld() {
    if(g.failed || g.shutdown || !thread()) return;
    g.worldExit=false;g.backgroundStopping=false;g.resetAttempted=false;g.schedule=false;g.scope=false;g.p2Update=false;
    g.phase=Phase::Waiting;g.p1={};g.attestSamples=0;g.anchorValid=false;
    for(auto& r:g.remotes){r.p2={};r.exists=false;r.p2Tick=r.motionTick=0;r.appliedSequence=r.appliedGeneration=r.appliedButtons=0;r.maxP2Distance=0;r.key.clear();r.entry={};}
    g.cur=1;
    for(unsigned i=0;i<8;++i){g.entityGeneration[i]=0;g.p7DeathSequence[i]=0;g.p7Dead[i]=false;}
    connected16={};
    g.p7KeysPermitted=false;g.p7KeyProbeSerial=g.p7KeyReviewSerial=0;
    g.snapshotEligibleLogged=false;g.lastMapFault=map56::Fault::None;
    mapAnnounced=false;worldBound=false;
    enemiesGone.clear();pickupsTaken.clear();switchesPressed.clear();goneSentSerial=0;pse::world::resetWorld();
    for(unsigned i=0;i<8;++i){proxyEntity[i]=0;proxyRebuild[i]=false;}
    // respawn / death bookkeeping of the old world
    team16=rp16::Team{};hostInput16=rp16::DeadInput{};bound16=false;checkpoints16={};
    requests16.clear();confirms16.clear();receipts16={};plans16={};deferred16={};deferredSince16={};retired16={};retiredCount16=0;retiredAllocations16={};
    // proxy of the remote player: fresh router + adapter incarnation (rearm is allowed after the barrier)
    proxies=new pse::proxy::SnapshotRouter<MaxRemote>();proxies->bind(g.expectedTid);
    proxyLocalBound=false;proxyMapReady=false;proxyExited=false;for(auto& pg:proxyGeneration)pg=0;proxySafe=false;
    if(!pse::native_proxy::rearm()) block("host_proxy_rearm");
    // restoreControls() switched the P1/P2 control hooks off for the engine's teardown; they are on again for the next world
    for(const auto i:{profile::Update,profile::Turning,profile::Commands}) if(!setHook(i,true)) return;
    log("host_world_rearmed","\"state\":%d,\"next\":%d,\"p2_travel\":%s,\"carried_players\":%zu",read<int>(instance(),0x24),read<int>(instance(),0x20),travelBy.empty()?"false":"true",travelBy.size());
}
bool installWeapon16(){
    static bool installed{};if(installed)return true;
    installed=pse::weapon_p16::install(g.base,g.expectedTid,{p16WeaponEligible,p16Roster,p16Event,p16Grant});return installed;
}
// Build the Player block + Puppet of the remote player in `slot` (a client became ready). Success = R(slot).exists. The first one takes the
// world from Armed to Live; later joiners are built the same way while the world keeps running (Live -> Constructing -> Live inside one tick).
void construct(unsigned slot) {
    Ctx ctx(slot);
    const auto phaseBefore = g.phase;
#ifdef PSE_FEATURE_P4
    if(!chooseSpawn()) {
        if(g.ticks%120==0) log("p4_spawn_deferred","\"slot\":%u,\"reason\":\"no clear native spot beside the host yet\",\"factory_calls\":0",slot);
        return; // Stay as we are and retry next tick. Never allocate/reset a remote or fall back to P1's occupied position.
    }
#endif
    R().key = g.network->participantKey(slot);
    if (R().key.empty()) R().key = "slot" + std::to_string(slot);
    g.phase = Phase::Constructing;
    g.p1.node = read<Byte*>(primary(), 0xd8);
    if (!g.p1.node) { block("primary_node_before_spawn"); return; }
    g.p1.actor = read<Byte*>(g.p1.node, 0x28);
    if (!live(g.p1, primary())) { block("primary_identity_before_spawn"); return; }
    if (read<void*>(read<void*>(g.base + 0x5a10a0), 0x18) != at(profile::Spawn)) { block("factory_vtable"); return; }
    static bool p1GrantDone{};
    {   // smoke only: PSE_DEBUG_P1_GRANT="weapon,ammo" gives the host a weapon before the client joins (a late joiner must get it too)
        char v[32]{};int wp=-1,am=0;
        if(GetEnvironmentVariableA("PSE_DEBUG_P1_GRANT",v,sizeof(v)) && sscanf_s(v,"%d,%d",&wp,&am)>=1 && wp>=0 && wp<8 && !p1GrantDone) {
            p1GrantDone=true;
            *(primary()+0xcc+wp)=1;*reinterpret_cast<std::uint16_t*>(primary()+0xbc+2*wp)=static_cast<std::uint16_t>(am);
            log("debug_p1_grant","\"weapon\":%d,\"ammo\":%d",wp,am);
        }
    }
    const auto before = localState();
    std::fill(std::begin(R().storage.before), std::end(R().storage.before), Byte{0xa5});
    std::fill(std::begin(R().storage.after), std::end(R().storage.after), Byte{0xa5});
    // Zero/default + ready references only in OUR new allocation, from C01 witness.
    std::fill(std::begin(R().storage.bytes), std::end(R().storage.bytes), Byte{0});
    own<std::uint32_t>(0x80, 0xffffffffu); own<void*>(0xe0, player());
    own<void*>(0x160, g.base + 0x5137c8); own<void*>(0x178, g.base + 0x5137c8);
    own<void*>(0x168, player() + 0x13c); own<void*>(0x180, player() + 0x134);
    log("native_call_attempt", "\"operation\":\"initial_reset\",\"alignment16\":%s", reinterpret_cast<std::uintptr_t>(player()) % 16 == 0 ? "true" : "false");
    reinterpret_cast<Unary>(at(profile::Reset))(player());
    log("native_call_return", "\"operation\":\"initial_reset\",\"guards\":%s", guards() ? "true" : "false");
    // The next map of the campaign: P2 arrives with what it collected. Nothing carried yet (the client joined mid-campaign, or the host
    // loaded a save): P2 gets a copy of the host's live inventory, full health, so a late joiner is not left with the starting kit.
    P2Persist start;
    if (const auto it=travelBy.find(R().key); it!=travelBy.end() && it->second.valid) start=it->second;
    else { start=captureP2(primary(),200); log("p2_seeded_from_host","\"slot\":%u,\"weapon\":%d",slot,start.weapon); }
    restoreP2(start);
    const bool getterWasOn = g.enabled[profile::Getter]; // a P14 scope may have left it on (see p14Getter): then it stays on
    if (!setHook(profile::Getter, true)) return;
    g.scope = true;
    const bool resolver = reinterpret_cast<Getter>(at(profile::Getter))(instance()) == player();
    void* actor{};
    if (resolver) {
#ifdef PSE_FEATURE_P4
        const Vec3 v{g.anchor.position[0],g.anchor.position[1],g.anchor.position[2]};
        const float yaw=g.anchorYaw;const int sector=g.anchorSector;
#else
        const auto v = position(g.p1.actor);
        const float yaw = read<float>(g.p1.actor, 0x38); const int sector = read<int>(g.p1.actor, 0x3c);
#endif
        log("native_call_attempt", "\"operation\":\"spawn\",\"type\":0,\"sector\":%d,\"yaw\":%.6f,\"pos\":[%.6f,%.6f,%.6f]", sector, yaw, v.x,v.y,v.z);
        actor = reinterpret_cast<Spawn>(at(profile::Spawn))(g.base + 0x5a10a0, 0, &v, yaw, sector);
        log("native_call_return", "\"operation\":\"spawn\",\"actor\":\"%p\"", actor);
    }
    g.scope = false;
    if (!getterWasOn && (!setHook(profile::Getter, false) || !prefix(profile::Getter))) { block("scoped_getter_restore"); return; }
    log("getter_scope_restored", "\"exact16\":%s,\"resolver_hits\":%u,\"left_on\":%s", getterWasOn ? "false" : "true", g.resolverHits, getterWasOn ? "true" : "false");
    R().p2.actor = static_cast<Byte*>(actor); R().p2.node = read<Byte*>(player(), 0xd8);
    if (!resolver || !live(R().p2, player()) || !live(g.p1, primary()) || R().p2.actor == g.p1.actor || R().p2.node == g.p1.node || !(before == localState()) || !guards()) { block("independent_spawn_postcondition"); return; }
    log("native_call_attempt", "\"operation\":\"weapon_ready\",\"mode\":0");
    reinterpret_cast<Ready>(at(profile::WeaponReady))(player() + 0xe0, 0);
    log("native_call_return", "\"operation\":\"weapon_ready\"");
    if (!read<void*>(player(), 0xe8) || !guards()) { block("weapon_ready_postcondition"); return; }
#ifdef PSE_FEATURE_SNAPSHOT
    if(g.entityGeneration[slot]) { block("unexpected_snapshot_entity_rebind");return; }
    // The first observed native Player/Puppet binding of this slot, independent of peer generation.
    if(!g.entityGeneration[0]) g.entityGeneration[0]=1;
    g.entityGeneration[slot]=1;
    log("snapshot_entity_bindings", "\"slot\":%u,\"p1_entity\":21760,\"p2_entity\":%u,\"p1_generation\":%u,\"p2_generation\":%u,\"connection_generation\":%u,\"map_epoch\":%u",slot,0x5500+slot,g.entityGeneration[0],g.entityGeneration[slot],g.network->generation(slot),g.network->mapEpoch());
#endif
    if (start.health>0 && start.health<=400) *reinterpret_cast<std::int16_t*>(R().p2.actor+0x17a)=static_cast<std::int16_t>((std::min)(static_cast<int>(start.health),pse::world::healthCap(player())));
    R().entry=captureP2();
    R().p2Start = position(R().p2.actor); R().exists = true; g.schedule = true; g.phase = Phase::Live;
    (void)phaseBefore;
    snapshot("independent_spawn");
}
// Clients that are ready but have no body yet (first player of the world, or a late joiner): one per tick.
void constructWaiting() {
    if (!g.network) return;
    for (unsigned slot = 1; slot <= MaxRemote; ++slot) {
        if (R(slot).exists || !g.network->wantsPlayer(slot)) continue;
        construct(slot);
        if (!R(slot).exists || g.failed) return; // deferred (no free spot yet) or fatal: try again next tick
    }
}
void __fastcall hookTick(void* self) {
    if (self != instance() || !thread()) {
        ++g.foreignCalls; block("tick_identity_or_thread");
        reinterpret_cast<Unary>(g.original[profile::Tick])(self); return;
    }
    if (!g.observedTid) { g.observedTid = GetCurrentThreadId(); log("simulation_attested", "\"expected_tid\":%lu,\"observed_tid\":%lu,\"game_instance\":\"%p\"", g.expectedTid, g.observedTid, self); }
    if (g.inTick) { block("nested_tick"); return; }
    g.inTick = true; g.localThisTick = 0; ++g.ticks;
    if (g.ticks == 120 || g.ticks == 1800) {   // diagnostics: is the game connected to the Steam client?
        const HMODULE api = GetModuleHandleW(L"steam_api64.dll");
        using IntFn = int(__cdecl*)();
        const auto user = api ? reinterpret_cast<IntFn>(GetProcAddress(api, "SteamAPI_GetHSteamUser")) : nullptr;
        const auto running = api ? reinterpret_cast<IntFn>(GetProcAddress(api, "SteamAPI_IsSteamRunning")) : nullptr;
        char appid[24]{};GetEnvironmentVariableA("SteamAppId", appid, sizeof(appid));
        log("steam_diag", "\"tick\":%llu,\"api_loaded\":%s,\"steamclient_loaded\":%s,\"hsteamuser\":%d,\"steam_running\":%d,\"gameoverlay_loaded\":%s,\"appid_env\":\"%s\"", g.ticks, api ? "true" : "false",
            GetModuleHandleW(L"steamclient64.dll") ? "true" : "false", user ? user() : -1, running ? running() : -1, GetModuleHandleW(L"GameOverlayRenderer64.dll") ? "true" : "false", appid);
    }
    {   // smoke only: PSE_DEBUG_SOLO_MAP="maps/KARNAK.MAP" — the host, still alone, goes to another level after 300 gameplay ticks (as after loading
        // a save there): a client that joins from the first level must follow and rejoin the host's session on that level.
        static std::uint64_t soloTicks=0;static bool soloFired=false;
        if (!soloFired && gameplay() && read<int>(instance(),0x20)==3 && ++soloTicks>=300) {
            soloFired=true;char solo[64]{};
            static constexpr Byte changeBytes[16]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0xc7,0x41,0x28,0x04,0x00,0x00};
            if (GetEnvironmentVariableA("PSE_DEBUG_SOLO_MAP",solo,sizeof(solo)) && std::equal(std::begin(changeBytes),std::end(changeBytes),g.base+0x1b54d0)) {
                log("debug_solo_map","\"map\":\"%s\"",solo);reinterpret_cast<void(__fastcall*)(void*,const char*)>(g.base+0x1b54d0)(instance(),solo);
            }
        }
    }
    {   // smoke only: PSE_DEBUG_BLOCK_FILE — when the file appears, a world-level fault (block); 2 s later the host goes on to
        // PSE_DEBUG_BLOCK_NEXT_MAP. The next world must bring co-op back by itself (host_recovered_after_block, the client rejoins).
        static bool fired=false;static std::uint64_t at=0;char file[MAX_PATH]{};
        if (!fired && g.ticks%30==0 && gameplay() && g.network && GetEnvironmentVariableA("PSE_DEBUG_BLOCK_FILE",file,sizeof(file)) && GetFileAttributesA(file)!=INVALID_FILE_ATTRIBUTES) {
            fired=true;at=g.ticks;block("debug_world_fault");
        }
        char next[64]{};
        if (fired && at && g.ticks==at+120 && gameplay() && GetEnvironmentVariableA("PSE_DEBUG_BLOCK_NEXT_MAP",next,sizeof(next))) {
            static constexpr Byte changeBytes[16]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0xc7,0x41,0x28,0x04,0x00,0x00};
            if (std::equal(std::begin(changeBytes),std::end(changeBytes),g.base+0x1b54d0)) { log("debug_block_next_map","\"map\":\"%s\"",next);reinterpret_cast<void(__fastcall*)(void*,const char*)>(g.base+0x1b54d0)(instance(),next); }
        }
    }
    proxyTickEnter();
    if (g.shutdown) ++g.shutdownTicks;
#ifdef PSE_ROLE_HOST
#ifdef PSE_FEATURE_MAPREADY
    if (g.network && g.network->running()) {
        map56::Identity current;
        const auto fault=map56::derive(map56::observe(g.base),current);
        if (fault!=g.lastMapFault || g.ticks%600==0)
            log("map_identity_status", "\"tick\":%llu,\"fault\":\"%s\",\"map\":\"%s\",\"count_a\":%llu,\"count_b\":%llu,\"world\":%u,\"peer\":%u,\"epoch\":%u",g.ticks,map56::text(fault),current.name.c_str(),current.countA,current.countB,static_cast<unsigned>(g.network->mapGate(1).world()),static_cast<unsigned>(g.network->mapGate(1).peer()),g.network->mapEpoch());
        g.lastMapFault=fault;
        g.network->verifyNativeMap(fault,current);
    }
#endif
    if (g.network) g.network->tick(g.ticks, gameplay() && !g.failed && !g.worldExit && !g.shutdown);
    {   // Host leads, client follows: ChangeMap (0x1b54d0) put the destination in GameInstance+0x1e0 and started the 1 s fade (pending=4);
        // the client gets the same map name now and calls ChangeMap itself, so both StateExits come within the same second.
        // The story endings are the same kind of order: EndGame(bool) asks the game state 6 (bad) / 7 (good) with the same fade; both
        // players then play their own (localized) ending movie. "@STATE:n" tells the client to request the same state.
        // The level exit itself asks state 2 = the game's own overworld map screen (the cursor sits on the next location, Enter loads it): whoever
        // reaches the exit, "@STATE:2" takes every player to their own map screen, each picks a location there; the host's level is the common one
        // afterwards (a client that loaded another level follows the host's level when it joins).
        const auto o=map56::observe(g.base);
        std::string order;
        if(o.state==3 && o.pending==4 && o.nameReadable && o.nameLength>0 && o.nameLength<=map56::NameReadMax) order.assign(o.name.data(),static_cast<std::size_t>(o.nameLength));
        else if(o.state==3 && o.pending==2) order="@STATE:2:"+std::to_string(overworldCurrent()); // the map screen, at the location the exit set
        else if(o.state==3 && (o.pending==6 || o.pending==7)) order="@STATE:"+std::to_string(o.pending);
        if(!order.empty()) {
            if(!mapAnnounced && g.network && g.network->running() && !g.failed && g.network->announceNextMap(order)) { mapAnnounced=true;log("host_map_change_announced","\"order\":\"%s\",\"tick\":%llu",order.c_str(),g.ticks); }
        } else mapAnnounced=false;
    }
    g.foregroundPid=0; GetWindowThreadProcessId(GetForegroundWindow(),&g.foregroundPid);
    if (g.backgroundPeriod && g.focusKnown && !g.focusLost) {
        g.backgroundPeriod=false; g.resumeNeutral=true; g.bindingsResetPending=true;
        log("background_focus_return", "\"tick\":%llu,\"neutral_updates\":%llu,\"first_update_drain\":true",g.ticks,g.neutralUpdates);
    }
    const background::Conditions c{thread(), g.network && g.network->running(), g.network && g.network->anyWantsPlayer(),
        gameplay(), read<int>(instance(),0x20)==3, read<Byte>(instance(),background::MenuByte)!=0,
        g.backgroundStopping || g.worldExit || g.shutdown, g.failed, g.focusKnown, g.focusLost};
    const bool allowed=background::eligible(c) && g.foregroundPid!=GetCurrentProcessId();
    auto& focusByte=*reinterpret_cast<Byte*>(instance()+background::FocusByte);
    const bool applied=g.focusLease.acquire(focusByte,g.focusSerial,allowed);
    if (applied) {
        ++g.backgroundTicks;
        if (!g.backgroundPeriod) {
            g.backgroundPeriod=true; g.bindingsResetPending=true;
            log("background_session_begin", "\"tick\":%llu,\"generation\":%u,\"p2_exists\":%s,\"foreground_pid\":%lu,\"native_focus_before\":1",g.ticks,g.network->connected(),g.schedule ? "true" : "false",g.foregroundPid);
        }
    }
    const bool uiCapture=pse::ui::nativeCapture(g.permit.load(std::memory_order_acquire)!=0,gameplay(),pse::ui::shared().capture.load());
    if(uiCapture!=g.uiCaptureWas) {g.bindingsResetPending=true;log("p23_ui_input_gate","\"capture\":%s,\"scoped_native_neutral\":true",uiCapture?"true":"false");}
    g.neutralPrimary=applied || g.resumeNeutral || uiCapture || g.uiCaptureWas;g.uiCaptureWas=uiCapture;
#endif
#ifdef PSE_FEATURE_P14
    pse::p14::sync();
#endif
    reinterpret_cast<Unary>(g.original[profile::Tick])(self);
#ifdef PSE_ROLE_HOST
    endFocusLease();
    if (applied && (g.backgroundTicks<=3 || g.backgroundTicks%120==0))
        log("background_tick", "\"tick\":%llu,\"background_ticks\":%llu,\"local_this_tick\":%u,\"focus_restored\":%u,\"foreground_pid\":%lu,\"neutral_updates\":%llu,\"menu\":%u,\"inventory\":%u",g.ticks,g.backgroundTicks,g.localThisTick,focusByte,g.foregroundPid,g.neutralUpdates,read<Byte>(instance(),background::MenuByte),read<Byte>(g.base+0xa43f30,0x1b0));
    if (!g.backgroundStopping && g.ticks%120==0)
        log("background_policy_status", "\"tick\":%llu,\"state\":%d,\"next\":%d,\"pending\":%d,\"focus_byte_after\":%u,\"focus_known\":%s,\"focus_lost\":%s,\"menu\":%u,\"inventory\":%u,\"network_running\":%s,\"session_ready\":%s,\"override_this_tick\":%s,\"local_this_tick\":%u,\"local_updates\":%llu,\"foreground_pid\":%lu,\"buttons\":%u,\"held_forward\":%u,\"held_back\":%u",g.ticks,read<int>(instance(),0x24),read<int>(instance(),0x20),read<int>(instance(),0x28),focusByte,g.focusKnown ? "true" : "false",g.focusLost ? "true" : "false",read<Byte>(instance(),background::MenuByte),read<Byte>(g.base+0xa43f30,0x1b0),g.network && g.network->running() ? "true" : "false",g.network && g.network->anyWantsPlayer() ? "true" : "false",applied ? "true" : "false",g.localThisTick,g.localUpdates,g.foregroundPid,read<unsigned>(primary()),read<unsigned>(primary(),12),read<unsigned>(primary(),16));
    g.neutralPrimary=false;
#endif
    if (!g.failed && !g.shutdown && !g.worldExit && gameplay()) {
        if (g.phase == Phase::Waiting && g.permit.load(std::memory_order_acquire) && pse::ui::shared().requested.load(std::memory_order_acquire)) { g.phase = Phase::Armed; log("test_permit_consumed", "\"mailbox_only\":true"); }
        if (g.localThisTick == 1) {
            ++g.activeTicks;
#ifdef PSE_FEATURE_P4
#endif
            if (g.phase == Phase::Armed && ++g.attestSamples >= 60) {
#ifdef PSE_ROLE_HOST
                if (!worldBound && g.network && g.network->running()) {
                    // The same session, the next world: every connected client is moved to it (new epoch; slots, identities and
                    // connections stay). Its bodies are built again below as each client becomes ready in this world.
                    const auto observed=map56::observe(g.base);map56::Identity id;
                    const auto fault=map56::derive(observed,id);
                    const auto epoch=fault==map56::Fault::None ? map56::randomEpoch(g.network->mapEpoch()) : 0;
                    if(fault!=map56::Fault::None || !epoch || !g.network->rebindNativeMap(id,epoch)) { block("native_map_rebind");g.inTick=false;return; }
                    log("map_identity_bound", "\"map\":\"%s\",\"map_epoch\":%u,\"epoch_source\":\"BCryptGenRandom per world of the host session\",\"tick\":%llu,\"rebind\":true,\"bindings\":%u,\"clients\":%u",
                        id.name.c_str(),epoch,g.ticks,g.network->worldBindings(),g.network->activePeers());
                    worldBound=true;
                }
                if (!g.network || !g.network->running()) {
                    g.network = new m12a::Session(log); // the first world (or after a permanent stop): a new listening session
#if defined(PSE_FEATURE_MAPREADY)
                    {
                        const auto observed=map56::observe(g.base);map56::Identity id;
                        const auto fault=map56::derive(observed,id);
                        const auto epoch=fault==map56::Fault::None ? map56::randomEpoch(0) : 0;
                        log("map_identity_observed", "\"fault\":\"%s\",\"state\":%d,\"next\":%d,\"pending\":%d,\"name_length\":%llu,\"name_capacity\":%llu,\"map\":\"%s\",\"mapinfo\":\"%p\",\"mapinfo_key\":\"%s\",\"level_vtable\":%s,\"psm_version\":%u,\"count_a\":%llu,\"count_b\":%llu,\"source\":\"GameInstance+1e0 kexStr / +1f8 mapInfo / level a45940\"",
                            map56::text(fault),observed.state,observed.next,observed.pending,observed.nameLength,observed.nameCapacity,id.name.c_str(),reinterpret_cast<void*>(observed.mapInfo),fault==map56::Fault::None ? observed.key.data() : "",observed.levelVtable ? "true" : "false",observed.version,observed.countA,observed.countB);
                        if(fault!=map56::Fault::None || !epoch || !g.network->configureNativeMap(id,epoch)) { block("native_map_identity_bind");g.inTick=false;return; }
                        log("map_identity_bound", "\"map\":\"%s\",\"map_epoch\":%u,\"epoch_source\":\"BCryptGenRandom per host session binding\",\"tick\":%llu,\"world_synchronized\":false",id.name.c_str(),epoch,g.ticks);
                    }
#elif defined(PSE_FEATURE_SNAPSHOT)
                    if(!firstMapWitness() || g.ticks>UINT32_MAX || !g.network->configureNativeSnapshots(static_cast<std::uint32_t>(g.ticks))) { block("snapshot_first_level_epoch_witness");g.inTick=false;return; }
                    log("snapshot_epoch_bound", "\"map_epoch\":%u,\"source\":\"host_attested_first_level_session_tick\",\"tick\":%llu,\"domain\":\"%s\",\"native_mapready_general_contract\":false,\"initial_sector\":259",g.network->mapEpoch(),g.ticks,snapshot55::Domain);
#endif
                    if (!g.network->start()) block("network_start");
                    if(!g.failed&&!installWeapon16())block("p16_weapon_hook_install");
                    worldBound=true;
                }
                if (!g.failed) constructWaiting();
#else
                construct(1);
#endif
            }
            else if (g.phase == Phase::Live) {
                constructWaiting(); // a client that became ready after the world started gets its body now
                if (!live(g.p1, primary()) || !remotesLive()) block("tick_identity_or_guards");
                else for (unsigned slot = 1; slot <= MaxRemote; ++slot) {
                    if (!R(slot).exists) continue;
                    Ctx ctx(slot);
                    const Vec3 v = position(R().p2.actor);
                    if (!finite(v)) { block("nonfinite_native_movement"); break; }
                    const float dx=v.x-R().p2Start.x, dy=v.y-R().p2Start.y;
                    R().maxP2Distance = std::max(R().maxP2Distance, std::sqrt(dx*dx+dy*dy));
#ifndef PSE_ROLE_HOST
                    if (R().secondaryUpdates == 45 || R().secondaryUpdates == 60 || R().secondaryUpdates == 105) snapshot("movement_checkpoint");
                    if (R().secondaryUpdates == 120) {
                        g.schedule = false; g.phase = Phase::Complete; snapshot("sequence_complete");
                        log("sequence_metrics", "\"native_p2_updates\":%llu,\"native_p2_puppet_ticks\":%llu,\"native_p2_commands\":%u,\"input_comparisons\":%llu,\"max_native_xy_displacement\":%.6f,\"foreign_calls\":%llu,\"neutral_final_buttons\":%u,\"failed\":%s",
                            R().secondaryUpdates, R().secondaryPuppetTicks, R().commandCalls, g.inputComparisons, R().maxP2Distance, g.foreignCalls, read<unsigned>(player()), g.failed ? "true" : "false");
                    }
#else
                    if (R().p2Tick == g.ticks && R().motionTick != g.ticks && (slot <= 1 || g.ticks % 60 == 0)) {
                        R().motionTick = g.ticks;
                        log("network_native_motion", "\"tick\":%llu,\"slot\":%u,\"generation\":%u,\"input_seq\":%u,\"native_buttons\":%u,\"p2_actor\":\"%p\",\"before\":[%.6f,%.6f,%.6f],\"after\":[%.6f,%.6f,%.6f],\"p2_health\":%d,\"p1_health\":%d,\"puppet_ticks\":%llu,\"updates\":%llu,\"guards\":%s", g.ticks,slot,R().appliedGeneration,R().appliedSequence,R().appliedButtons,R().p2.actor,R().motionBefore.x,R().motionBefore.y,R().motionBefore.z,v.x,v.y,v.z,read<std::int16_t>(R().p2.actor,0x17a),read<std::int16_t>(g.p1.actor,0x17a),R().secondaryPuppetTicks,R().secondaryUpdates,guards() ? "true" : "false");
                    }
#endif
                }
            }
        }
    }

#ifdef PSE_FEATURE_SNAPSHOT
    captureNativeSnapshots();
    worldSyncHost();
#endif
    proxyPhase();proxyTickLeave();
    // Always retain the first real P1 update, even after a long menu/startup.
    // The periodic diagnostic horizon must not suppress the isolation/readiness gate.
    if ((g.localUpdates == 1 && g.localThisTick == 1) || g.ticks <= 3 || (g.ticks % 120 == 0 && g.ticks <= 2400)) {
        log("tick_status", "\"ticks\":%llu,\"state\":%d,\"pending\":%d,\"local_updates\":%llu,\"local_this_tick\":%u,\"phase\":%d,\"p2_updates\":%llu",
            g.ticks, read<int>(instance(),0x24), read<int>(instance(),0x28), g.localUpdates, g.localThisTick, static_cast<int>(g.phase), R().secondaryUpdates);
    }
    {   // Esc menu: gameplay state, but the local Player Update is skipped. Debounced over a few ticks.
        static unsigned idleTicks=0;idleTicks=(gameplay() && g.localThisTick==0) ? idleTicks+1 : 0;
        pse::ui::shared().paused.store(idleTicks>=3,std::memory_order_release);
    }
    pse::native_ui::tick(!g.failed && gameplay() && g.network && g.network->anySnapshotEligible(),g.network?g.network->mapEpoch():0,0,1);
    {   // lobby beacon: the map this host plays, who is in, and whether a client can connect right now (a Session is listening)
        const bool open=g.network && g.network->running() && !g.failed;
        const auto o=map56::observe(g.base);
        pse::native_ui::lobbyInfo(o.nameReadable && o.state==3 ? o.name.data() : "",1u+(g.network?g.network->connected():0u),open);
    }
    g.inTick = false;
}

LONG CALLBACK exceptions(EXCEPTION_POINTERS* e) {
    const auto code = e->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_STACK_OVERFLOW)
        log("native_fault", "\"code\":%lu,\"address\":\"%p\",\"phase\":%d", code, e->ExceptionRecord->ExceptionAddress, static_cast<int>(g.phase));
    return EXCEPTION_CONTINUE_SEARCH;
}
void CALLBACK bootstrapApc(ULONG_PTR) {
    if (!thread()) { block("bootstrap_thread"); return; }
    log("bootstrap_apc", "\"expected_primary_tid\":%lu,\"game_calls\":0", g.expectedTid);
    for (std::size_t n=0; n < profile::Count; ++n) {
        const auto i=static_cast<profile::Index>(n);
        if (!prefix(i)) { block("exact_prefix_before_hooks"); return; }
    }
#ifdef PSE_ROLE_HOST
    const auto exact=[](const Byte* address, const auto& bytes) { return std::equal(std::begin(bytes),std::end(bytes),address); };
    if (!exact(g.base+background::FocusResponder,background::ResponderPrefix) ||
        !exact(g.base+background::ResetBindings,background::ResetPrefix) ||
        !exact(g.base+background::FocusGate,background::GatePrefix)) { block("background_exact_profile"); return; }
#endif

    if (MH_Initialize() != MH_OK) { block("minhook_initialize"); return; }
#ifdef PSE_FEATURE_P14
    if (!pse::p14::install(g.base,g.expectedTid,{p14Eligible,p14Roster,p14Getter,p14Event,block,p14HeldWeapon})) {
        block("p14_hook_install"); return;
    }
#endif
    void* hooks[] = {reinterpret_cast<void*>(&hookTick), reinterpret_cast<void*>(&hookUpdate), reinterpret_cast<void*>(&hookCommands), reinterpret_cast<void*>(&hookTurning), reinterpret_cast<void*>(&hookGetter),
        reinterpret_cast<void*>(&hookStateExit), reinterpret_cast<void*>(&hookShutdown), reinterpret_cast<void*>(&hookDtor), reinterpret_cast<void*>(&hookPuppetTick), reinterpret_cast<void*>(&hookEngineShutdown), reinterpret_cast<void*>(&hookFinal)};
    for (std::size_t n=0; n < std::size(hooks); ++n) {
        const auto i=static_cast<profile::Index>(n);
        const auto status = MH_CreateHook(at(i), hooks[n], &g.original[n]);
        if (status != MH_OK) { log("hook_create_error", "\"site\":\"%s\",\"status\":%d", profile::Sites[n].name, status); block("minhook_create"); return; }
        g.created[n] = true;
    }
#ifdef PSE_ROLE_HOST
    if (MH_CreateHook(g.base+background::FocusResponder,reinterpret_cast<void*>(&hookFocusResponder),&g.focusOriginal)!=MH_OK ||
        MH_EnableHook(g.base+background::FocusResponder)!=MH_OK) { block("background_focus_hook"); return; }
    log("background_hook_ready", "\"responder_rva\":%u,\"focus_byte_offset\":%u,\"scope\":\"READY host gameplay StateTick only\",\"lifetime\":\"pinned until process exit\"",background::FocusResponder,background::FocusByte);
#endif
    for (std::size_t n=0; n < std::size(hooks); ++n) {
        const auto i=static_cast<profile::Index>(n);
        if (i != profile::Getter && !setHook(i, true)) return;
    }
    if(!pse::world::install(g.base,g.expectedTid,log,true)) {block("world_hook_install");return;}
    pse::world::setLocalActorGetter([]() -> unsigned char* { return g.p1.actor; });
    pse::world::setRemoteBodyTest([](const void* a) { return remoteSlotOfActor(a) != 0; }); // damage frame / pickup messages of other players' bodies stay off the host's HUD
    if(!pse::native_ui::install(g.base,g.expectedTid,true,log)) {block("p23_ui_install");return;}
    if(!installHostProxy()) { block("host_proxy_install"); return; }
    AddVectoredExceptionHandler(0, exceptions);
    log("bootstrap_ready", "\"game_calls\":0,\"hooks_installed_on_primary\":true,\"spawn_requires_tick_and_isolation_permit\":true,\"module_pinned\":true");
}
bool within(const wchar_t* path, const wchar_t* root) {
    const auto n = wcslen(root);
    return _wcsnicmp(path, root, n) == 0 && (path[n] == L'\\' || path[n] == L'\0');
}
bool hashMatches(const wchar_t* exe) {
    const HANDLE f=CreateFileW(exe, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE alg{}; BCRYPT_HASH_HANDLE hash{}; Byte result[32]{}; Byte buffer[65536]{}; DWORD n{};
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 && BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    while (ok) { if (!ReadFile(f, buffer, sizeof(buffer), &n, nullptr)) { ok=false; break; } if (!n) break; ok=BCryptHashData(hash, buffer, n, 0) >= 0; }
    if (ok) ok=BCryptFinishHash(hash, result, sizeof(result), 0) >= 0;
    if (hash) BCryptDestroyHash(hash); if (alg) BCryptCloseAlgorithmProvider(alg,0); CloseHandle(f);
    char hex[65]{};
    for (unsigned i=0; i < 32; ++i) sprintf_s(hex+2*i, 3, "%02x", result[i]);
    return ok && strcmp(hex, profile::Sha256) == 0;
}
} // namespace

// Called by the diagnostic loader before ResumeThread. Only adapter-owned memory,
// documented OS APIs, hashing and a same-process primary-thread APC are used here.
namespace m12a { static void p16Receive(const pse::BridgeRespawnRequest& request); static void worldReceive(const pse::BridgeInteractionRequest& request); }
extern "C" DWORD WINAPI host_M11Prepare(const wchar_t* logPath, DWORD primaryTid) {
    if (g.prepared.exchange(1) != 0) return ERROR_ALREADY_INITIALIZED;
    m12a::respawnHandler = &m12a::p16Receive;
    m12a::worldRequestHandler = &m12a::worldReceive;
    wchar_t exe[32768]{}, dll[32768]{}, fullLog[32768]{}, installRoot[32768]{};
    // P27: pe-play (PSE_GUARD_ROOT) selects the portable install-root policy; otherwise the unchanged lab policy.
    const bool portable = pse::portable::root(installRoot, 32768);
    const wchar_t* allowed = portable ? installRoot : profile::Workspace;
    if (!GetModuleFileNameW(nullptr, exe, 32768) || !(portable ? pse::portable::exeMatches(exe, installRoot) : _wcsicmp(exe, profile::Executable) == 0) || !within(exe, allowed) || !hashMatches(exe)) return ERROR_BAD_EXE_FORMAT;
    if (!GetModuleFileNameW(g.module, dll, 32768) || !within(dll, allowed)) return ERROR_BAD_PATHNAME;
    const DWORD length=GetFullPathNameW(logPath, 32768, fullLog, nullptr);
    if (!length || length >= 32768 || !within(fullLog, allowed)) return ERROR_BAD_PATHNAME;
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&host_M11Prepare), &pinned)) return GetLastError();
    g.expectedTid=primaryTid; g.base=reinterpret_cast<Byte*>(GetModuleHandleW(nullptr));
    g.log=CreateFileW(fullLog, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    journalWriter.bind(&journalSink);
    if (g.log == INVALID_HANDLE_VALUE) return GetLastError();
    log("dll_prepared", "\"pid\":%lu,\"loader_tid\":%lu,\"expected_primary_tid\":%lu,\"binary_sha256\":\"%s\",\"module_pinned\":true,\"kex_calls\":0", GetCurrentProcessId(), GetCurrentThreadId(), primaryTid, profile::Sha256);
    const HANDLE th=OpenThread(THREAD_SET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE, primaryTid);
    if (!th) return GetLastError();
    if (GetProcessIdOfThread(th) != GetCurrentProcessId()) { CloseHandle(th); return ERROR_INVALID_PARAMETER; }
    const DWORD queued=QueueUserAPC(bootstrapApc, th, 0); const DWORD error=queued ? ERROR_SUCCESS : GetLastError(); CloseHandle(th);
    log("bootstrap_queued", "\"queued\":%s,\"win32_error\":%lu", queued ? "true" : "false", error);
    return error;
}
// Mailbox only; no KEX call or game-memory access on the loader/diagnostic thread.
extern "C" DWORD WINAPI host_M11PermitTest() { pse::native_ui::permit();g.permit.store(1, std::memory_order_release); return ERROR_SUCCESS; }
namespace m12a {static void p16Receive(const pse::BridgeRespawnRequest& request){
    if(!thread()||!g.inTick){block("p16_request_affinity");return;}
    if(requests16.size()>=128){block("p16_request_capacity");return;}requests16.push_back(request);
}}
namespace m12a {static void worldReceive(const pse::BridgeInteractionRequest& request){
    if(!thread()||!g.inTick){block("world_request_affinity");return;}
    if(worldRequests.size()<256) worldRequests.push_back(request);
}}
extern "C" DWORD WINAPI host_PSE_P16_PermitRestore(std::uint64_t probe,std::uint64_t review){
    if(!probe||!review)return ERROR_INVALID_PARAMETER;approvedReview16.store(0,std::memory_order_release);approvedProbe16.store(probe,std::memory_order_release);approvedReview16.store(review,std::memory_order_release);return ERROR_SUCCESS;
}
extern "C" DWORD WINAPI host_PSE_P16_HostFixture(unsigned op){
    if(op<1||op>3)return ERROR_INVALID_PARAMETER;unsigned empty=0;
    return fixture16.compare_exchange_strong(empty,op)?ERROR_SUCCESS:ERROR_BUSY;
}
extern "C" void host_attach(HINSTANCE module) {
    g.module=module;
}
