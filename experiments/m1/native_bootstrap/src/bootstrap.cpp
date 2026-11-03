// M1.1 exact-build adapter. Opaque KEX objects; this is not reconstructed KEX source.
// All KEX calls and all hook changes execute on the launch primary / attested tick thread.
#include <windows.h>
#include <bcrypt.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <intrin.h>
#include "build_profile.hpp"
#ifdef PSE_M12A
#include "network_input.hpp"
#include "background_policy.hpp"
#include "background_profile.hpp"
#endif
#ifdef PSE_DOC55_SNAPSHOT
#include "../../native_snapshot/src/snapshot_contract.hpp"
#endif
#ifdef PSE_DOC56_MAPREADY
#include "../../native_map_ready/src/map_reader.hpp"
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
    GuardedPlayer storage{}; Identity p1{}, p2{};
    std::uint32_t held[13]{};
    std::uint64_t seq{}, ticks{}, activeTicks{}, localUpdates{}, secondaryUpdates{}, primaryPuppetTicks{}, secondaryPuppetTicks{};
    std::uint64_t attestSamples{}, inputComparisons{}, foreignCalls{}, dtorReturns{}, stateExitReturns{}, shutdownTicks{};
    unsigned localThisTick{}, resolverHits{}, commandCalls{}, inputSamples{};
    Vec3 p2Start{}; float maxP2Distance{};
#ifdef PSE_M12A
    m12a::Session* network{}; // retained, never a CRT global destructor
    std::uint64_t p2Tick{}, motionTick{};
    std::uint32_t appliedSequence{}, appliedGeneration{}, appliedButtons{};
    Vec3 motionBefore{};
    void* focusOriginal{}; // one additional process-lifetime trampoline, never freed
    background::FocusLease focusLease{};
    std::uint64_t focusSerial{}, backgroundTicks{}, neutralUpdates{};
    bool focusKnown{}, focusLost{}, backgroundStopping{}, backgroundPeriod{};
    bool neutralPrimary{}, resumeNeutral{}, bindingsResetPending{};
    DWORD foregroundPid{};
#ifdef PSE_DOC55_SNAPSHOT
    std::uint32_t snapshotSequence{}, entityGeneration[2]{};
#endif
#ifdef PSE_DOC56_MAPREADY
    bool snapshotEligibleLogged{};
    map56::Fault lastMapFault{map56::Fault::None};
#endif
#endif
} g;

template<class T> T read(const void* p, std::size_t offset = 0) { return *reinterpret_cast<const T*>(static_cast<const Byte*>(p) + offset); }
template<class T> void own(std::size_t offset, T value) { *reinterpret_cast<T*>(g.storage.bytes + offset) = value; }
void* at(profile::Index i) { return g.base + profile::Sites[i].rva; }
Byte* player() { return g.storage.bytes; }
Byte* primary() { return g.base + 0xaf33e0; }
Byte* instance() { return g.base + 0xaf33a0; }
bool thread() { return GetCurrentThreadId() == g.expectedTid; }
bool gameplay() { return read<int>(instance(), 0x24) == 3 && read<int>(instance(), 0x28) == 0; }

void log(const char* event, const char* fmt = "", ...) {
    if (g.log == INVALID_HANDLE_VALUE) return;
    char detail[6500]{}; va_list args; va_start(args, fmt);
    vsnprintf_s(detail, sizeof(detail), _TRUNCATE, fmt, args); va_end(args);
    SYSTEMTIME utc{}; GetSystemTime(&utc);
    AcquireSRWLockExclusive(&g.logLock);
    char line[7500]{};
    const int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
        "{\"event\":\"%s\",\"seq\":%llu,\"utc\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\",\"tid\":%lu%s%s}\n",
        event, ++g.seq, utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond, utc.wMilliseconds,
        GetCurrentThreadId(), detail[0] ? "," : "", detail);
    DWORD written{};
    if (n > 0) WriteFile(g.log, line, static_cast<DWORD>(n), &written, nullptr);
    FlushFileBuffers(g.log);
    ReleaseSRWLockExclusive(&g.logLock);
}
void block(const char* reason) {
    g.failed = true; g.schedule = false; g.phase = Phase::Blocked;
    log("blocked", "\"reason\":\"%s\",\"native_retry\":false,\"storage_pinned\":true", reason);
}
bool guards() {
    return std::all_of(std::begin(g.storage.before), std::end(g.storage.before), [](Byte b) { return b == 0xa5; }) &&
           std::all_of(std::begin(g.storage.after), std::end(g.storage.after), [](Byte b) { return b == 0xa5; });
}
bool live(const Identity& id, Byte* p) {
    return !id.destroyed && id.actor && id.node && read<Byte*>(p, 0xd8) == id.node &&
        read<Byte*>(id.node, 0x28) == id.actor && read<Byte*>(id.actor, 0x10) == id.node &&
        read<Byte*>(id.actor, 0x1b8) == p && read<Byte*>(id.actor) == g.base + 0x52d198;
}
Vec3 position(Byte* a) { return {read<float>(a, 0x18), read<float>(a, 0x1c), read<float>(a, 0x20)}; }
bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
#ifdef PSE_DOC55_SNAPSHOT
#ifndef PSE_DOC56_MAPREADY
bool firstMapWitness() {
    Identity id{};id.node=read<Byte*>(primary(),0xd8);id.actor=id.node ? read<Byte*>(id.node,0x28) : nullptr;
    if(!live(id,primary()))return false;
    const auto v=position(id.actor);
    return finite(v) && v.x==-7420.0f && v.y==-6775.0f && v.z==1152.0f && read<int>(id.actor,0x3c)==259;
}
#endif
void captureNativeSnapshots() {
    if(!thread() || !g.inTick) { block("snapshot_capture_affinity");return; }
    if(g.failed || g.shutdown || g.worldExit || g.backgroundStopping || !gameplay() ||
        !g.network || !g.network->wantsPlayer() || g.localThisTick!=1 || g.p2Tick!=g.ticks || (g.ticks%2)!=0)return;
#ifdef PSE_DOC56_MAPREADY
    const bool eligible=g.network->snapshotEligible();
    if(eligible!=g.snapshotEligibleLogged) {
        g.snapshotEligibleLogged=eligible;
        log("snapshot_eligibility","\"eligible\":%s,\"tick\":%llu,\"epoch\":%u,\"generation\":%u,\"world\":%u,\"peer\":%u,\"map\":\"%s\"",eligible ? "true" : "false",g.ticks,g.network->mapEpoch(),g.network->generation(),static_cast<unsigned>(g.network->mapGate().world()),static_cast<unsigned>(g.network->mapGate().peer()),g.network->mapGate().bound().name.c_str());
    }
    if(!eligible)return;
#endif
    if(!live(g.p1,primary()) || !live(g.p2,player()) || !guards()) { block("snapshot_capture_ownership");return; }
    if(g.ticks>UINT32_MAX || g.snapshotSequence>UINT32_MAX-2) { block("snapshot_counter_exhaustion");return; }
    for(unsigned slot=0;slot<2;++slot) {
        const auto& id=slot ? g.p2 : g.p1;
        const auto v=position(id.actor);const float yaw=read<float>(id.actor,0x38),pitch=read<float>(id.actor,0x30);
        if(!finite(v) || !std::isfinite(yaw) || !std::isfinite(pitch) || !g.entityGeneration[slot] || read<std::int16_t>(id.actor,0x17a)<=0) { block("snapshot_capture_transform_or_lifetime");return; }
        pse::BridgePlayerSnapshot s;s.slotId=static_cast<std::uint8_t>(slot);s.entityId=0x5500+slot;s.entityGeneration=g.entityGeneration[slot];
        s.sequence=++g.snapshotSequence;s.serverTick=static_cast<std::uint32_t>(g.ticks);s.mapEpoch=g.network->mapEpoch();s.connectionGeneration=g.network->generation();
        s.posX=v.x;s.posY=v.y;s.posZ=v.z;s.yaw=yaw;s.pitch=pitch;s.health=read<std::int16_t>(id.actor,0x17a);
        const bool queued=g.network->publishNativeSnapshot(s);
        log("native_snapshot_captured", "\"slot\":%u,\"snapshot_seq\":%u,\"server_tick\":%u,\"map_epoch\":%u,\"connection_generation\":%u,\"entity_id\":%u,\"entity_generation\":%u,\"pos\":[%.9g,%.9g,%.9g],\"yaw\":%.9g,\"pitch\":%.9g,\"bits\":[%u,%u,%u,%u,%u],\"queued\":%s,\"actor\":\"%p\",\"node\":\"%p\",\"owner\":\"%p\",\"p2_updates\":%llu,\"p2_puppet_ticks\":%llu,\"post_world_tick\":true",
            slot,s.sequence,s.serverTick,s.mapEpoch,s.connectionGeneration,s.entityId,s.entityGeneration,v.x,v.y,v.z,yaw,pitch,snapshot55::bits(v.x),snapshot55::bits(v.y),snapshot55::bits(v.z),snapshot55::bits(yaw),snapshot55::bits(pitch),queued ? "true" : "false",id.actor,id.node,read<Byte*>(id.actor,0x1b8),g.secondaryUpdates,g.secondaryPuppetTicks);
    }
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
    if (!live(g.p1, primary()) || !live(g.p2, player())) { block("snapshot_identity"); return; }
    const Vec3 a = position(g.p1.actor), b = position(g.p2.actor);
    log(name,
        "\"p1_player\":\"%p\",\"p1_actor\":\"%p\",\"p1_node\":\"%p\",\"p1_owner\":\"%p\",\"p1_health\":%d,\"p1_pos\":[%.6f,%.6f,%.6f],"
        "\"p2_player\":\"%p\",\"p2_actor\":\"%p\",\"p2_node\":\"%p\",\"p2_owner\":\"%p\",\"p2_health\":%d,\"p2_pos\":[%.6f,%.6f,%.6f],"
        "\"p2_refs\":%d,\"p2_commands\":%u,\"p2_updates\":%llu,\"p2_puppet_ticks\":%llu,\"guards\":%s,\"generation\":1",
        primary(), g.p1.actor, g.p1.node, read<Byte*>(g.p1.actor, 0x1b8), read<std::int16_t>(g.p1.actor, 0x17a), a.x,a.y,a.z,
        player(), g.p2.actor, g.p2.node, read<Byte*>(g.p2.actor, 0x1b8), read<std::int16_t>(g.p2.actor, 0x17a), b.x,b.y,b.z,
        read<int>(g.p2.node, 0x40), g.commandCalls, g.secondaryUpdates, g.secondaryPuppetTicks, guards() ? "true" : "false");
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

#ifdef PSE_M12A
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
#ifdef PSE_M12A
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
    ++g.commandCalls;
#ifdef PSE_M12A
    const std::uint32_t buttons = g.appliedButtons;
#else
    const auto n = g.secondaryUpdates;
    const std::uint32_t buttons = n <= 45 ? 8u : n > 60 && n <= 105 ? 4u : 0u;
#endif
    own<std::uint32_t>(0, buttons);
    for (unsigned i = 0; i < 13; ++i) { g.held[i] = buttons & (1u << i) ? g.held[i] + 1 : 0; own<std::uint32_t>(4 + 4 * i, g.held[i]); }
    for (std::size_t offset = 0x38; offset <= 0x7c; offset += 4) own<std::uint32_t>(offset, 0);
}
void __fastcall hookTurning(void* p) {
    if (thread() && g.inTick && g.p2Update && p == player()) return;
    reinterpret_cast<Unary>(g.original[profile::Turning])(p);
#ifdef PSE_M12A
    if (primaryNeutral(p)) clearPrimaryCommands();
#endif
}
void* __fastcall hookGetter(void* p) {
    if (thread() && g.inTick && g.scope && p == instance()) { ++g.resolverHits; return player(); }
    return reinterpret_cast<Getter>(g.original[profile::Getter])(p);
}
void __fastcall hookUpdate(void* p) {
    // Prevent a duplicate/foreign primary call BEFORE executing native Update.
    if (p==primary()) {
        if (!thread() || !g.inTick) { ++g.foreignCalls; block("local_update_outside_attested_tick"); return; }
        if (g.localThisTick!=0) { block("duplicate_local_update"); return; }
        ++g.localThisTick;
#ifdef PSE_M12A
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
#ifdef PSE_M12A
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
    if (!live(g.p1, primary()) || !live(g.p2, player()) || !guards()) { block("controller_identity_or_guards"); return; }
    if (read<std::int16_t>(g.p2.actor, 0x17a) <= 0 || (read<unsigned>(g.p2.actor, 0x1c0) & 0x200u)) { block("secondary_dead_no_respawn_in_m11"); return; }
    const auto before = localState();
#ifdef PSE_M12A
    if (g.p2Tick == g.ticks) { block("duplicate_secondary_update"); return; }
    g.p2Tick = g.ticks;
    g.appliedSequence = g.network->sequence();
    g.appliedGeneration = g.network->generation();
    g.appliedButtons = g.network->nativeButtons();
    g.motionBefore = position(g.p2.actor);
    const auto commandsBefore = g.commandCalls;
#endif
    ++g.secondaryUpdates; g.p2Update = true;
    reinterpret_cast<Unary>(g.original[profile::Update])(player());
    g.p2Update = false;
    ++g.inputComparisons;
    const auto after = localState();
    if (!(before == after)) block("p2_update_changed_local_input_or_identity");
#ifdef PSE_M12A
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
    log("network_native_update", "\"tick\":%llu,\"slot\":1,\"generation\":%u,\"input_seq\":%u,\"accepted_tick\":%llu,\"native_buttons\":%u,\"commands_this_update\":%u,\"p2_player\":\"%p\",\"p2_actor\":\"%p\",\"p1_before\":%s,\"p1_after\":%s,\"unchanged\":%s,\"focus_override\":%s,\"p1_neutral\":%s,\"foreground_pid\":%lu,\"primary_caller_rva\":%u", g.ticks,g.appliedGeneration,g.appliedSequence,g.network->acceptedAt(),g.appliedButtons,g.commandCalls-commandsBefore,player(),g.p2.actor,a.data(),b.data(),before == after ? "true" : "false",g.focusLease.applied ? "true" : "false",g.neutralPrimary ? "true" : "false",g.foregroundPid,background::PrimaryReturn);
#endif
}
void __fastcall hookPuppetTick(void* p) {
    if (thread() && !g.shutdown) {
        if (p == g.p1.actor && !g.p1.destroyed) ++g.primaryPuppetTicks;
        if (p == g.p2.actor && !g.p2.destroyed) ++g.secondaryPuppetTicks;
    }
    reinterpret_cast<Unary>(g.original[profile::PuppetTick])(p);
}
void __fastcall hookDtor(void* p) {
    const bool owned = p == g.p2.actor && p != nullptr && !g.p2.destroyed;
    if (owned && !thread()) { ++g.foreignCalls; block("owned_destructor_thread"); }
    if (owned) log("owned_actor_dtor_entry", "\"actor\":\"%p\",\"external_handle_null\":%s", p, read<Byte*>(player(), 0xd8) ? "false" : "true");
    reinterpret_cast<Unary>(g.original[profile::ActorDtor])(p);
    if (owned) {
        g.p2.destroyed = true; ++g.dtorReturns;
        log("owned_actor_dtor_return", "\"actor\":\"%p\",\"actor_reads_after_return\":false,\"guards\":%s", p, guards() ? "true" : "false");
    }
}
void unbind(const char* reason) {
    if (!thread()) { ++g.foreignCalls; block("unbind_thread"); return; }
#ifdef PSE_M12A
    g.backgroundStopping=true;
    endFocusLease(); g.neutralPrimary=false;
    // Join completes while engine services and our callbacks/storage still live.
    if (g.network) g.network->stop(reason);
    if (g.schedule) {
        own<std::uint32_t>(0, 0);
        for (unsigned i=0; i<13; ++i) { g.held[i]=0; own<std::uint32_t>(4+4*i, 0); }
    }
#endif
    restoreControls();
    if (!read<Byte*>(player(), 0xd8)) { log("unbind_already_empty", "\"reason\":\"%s\"", reason); return; }
    if (g.resetAttempted || !gameplay() || !live(g.p2, player()) || !live(g.p1, primary())) { block("unbind_abi_lifetime_gate"); return; }
    snapshot("before_unbind");
    const auto before = localState(); g.resetAttempted = true;
    log("native_call_attempt", "\"operation\":\"exit_reset\",\"reason\":\"%s\"", reason);
    reinterpret_cast<Unary>(at(profile::Reset))(player());
    const bool empty = read<Byte*>(player(), 0xd8) == nullptr;
    const bool unchanged = before == localState();
    log("native_call_return", "\"operation\":\"exit_reset\",\"handle_null\":%s,\"p1_unchanged\":%s,\"guards\":%s", empty ? "true" : "false", unchanged ? "true" : "false", guards() ? "true" : "false");
    if (!empty || !unchanged || !guards()) block("unbind_postcondition");
}
void __fastcall hookStateExit(void* self) {
    const bool teardown = thread() && self == instance() && read<int>(instance(), 0x24) == 3 && read<int>(instance(), 0x20) != 3;
    if (teardown && !g.worldExit) {
        g.worldExit = true;
        log("state_exit_entry", "\"next_state\":%d", read<int>(instance(), 0x20));
        unbind("StateExit"); g.phase = Phase::WorldExit;
    }
    reinterpret_cast<Unary>(g.original[profile::StateExit])(self);
    if (teardown) {
        ++g.stateExitReturns; g.phase = Phase::WorldExited;
        log("state_exit_return", "\"external_handle_null\":%s,\"owned_dtor_returns\":%llu,\"guards\":%s,\"actor_reads\":false,\"module_and_storage_pinned\":true",
            read<Byte*>(player(), 0xd8) ? "false" : "true", g.dtorReturns, guards() ? "true" : "false");
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

void construct() {
    g.phase = Phase::Constructing;
    g.p1.node = read<Byte*>(primary(), 0xd8);
    if (!g.p1.node) { block("primary_node_before_spawn"); return; }
    g.p1.actor = read<Byte*>(g.p1.node, 0x28);
    if (!live(g.p1, primary())) { block("primary_identity_before_spawn"); return; }
    if (read<void*>(read<void*>(g.base + 0x5a10a0), 0x18) != at(profile::Spawn)) { block("factory_vtable"); return; }
    const auto before = localState();
    std::fill(std::begin(g.storage.before), std::end(g.storage.before), Byte{0xa5});
    std::fill(std::begin(g.storage.after), std::end(g.storage.after), Byte{0xa5});
    // Zero/default + ready references only in OUR new allocation, from C01 witness.
    std::fill(std::begin(g.storage.bytes), std::end(g.storage.bytes), Byte{0});
    own<std::uint32_t>(0x80, 0xffffffffu); own<void*>(0xe0, player());
    own<void*>(0x160, g.base + 0x5137c8); own<void*>(0x178, g.base + 0x5137c8);
    own<void*>(0x168, player() + 0x13c); own<void*>(0x180, player() + 0x134);
    log("native_call_attempt", "\"operation\":\"initial_reset\",\"alignment16\":%s", reinterpret_cast<std::uintptr_t>(player()) % 16 == 0 ? "true" : "false");
    reinterpret_cast<Unary>(at(profile::Reset))(player());
    log("native_call_return", "\"operation\":\"initial_reset\",\"guards\":%s", guards() ? "true" : "false");
    if (!setHook(profile::Getter, true)) return;
    g.scope = true;
    const bool resolver = reinterpret_cast<Getter>(at(profile::Getter))(instance()) == player();
    void* actor{};
    if (resolver) {
        const auto v = position(g.p1.actor);
        const float yaw = read<float>(g.p1.actor, 0x38); const int sector = read<int>(g.p1.actor, 0x3c);
        log("native_call_attempt", "\"operation\":\"spawn\",\"type\":0,\"sector\":%d,\"yaw\":%.6f,\"pos\":[%.6f,%.6f,%.6f]", sector, yaw, v.x,v.y,v.z);
        actor = reinterpret_cast<Spawn>(at(profile::Spawn))(g.base + 0x5a10a0, 0, &v, yaw, sector);
        log("native_call_return", "\"operation\":\"spawn\",\"actor\":\"%p\"", actor);
    }
    g.scope = false;
    if (!setHook(profile::Getter, false) || !prefix(profile::Getter)) { block("scoped_getter_restore"); return; }
    log("getter_scope_restored", "\"exact16\":true,\"resolver_hits\":%u", g.resolverHits);
    g.p2.actor = static_cast<Byte*>(actor); g.p2.node = read<Byte*>(player(), 0xd8);
    if (!resolver || !live(g.p2, player()) || !live(g.p1, primary()) || g.p2.actor == g.p1.actor || g.p2.node == g.p1.node || !(before == localState()) || !guards()) { block("independent_spawn_postcondition"); return; }
    log("native_call_attempt", "\"operation\":\"weapon_ready\",\"mode\":0");
    reinterpret_cast<Ready>(at(profile::WeaponReady))(player() + 0xe0, 0);
    log("native_call_return", "\"operation\":\"weapon_ready\"");
    if (!read<void*>(player(), 0xe8) || !guards()) { block("weapon_ready_postcondition"); return; }
#ifdef PSE_DOC55_SNAPSHOT
    if(g.entityGeneration[0] || g.entityGeneration[1]) { block("unexpected_snapshot_entity_rebind");return; }
    // The first observed native Player/Puppet binding, independent of peer generation.
    ++g.entityGeneration[0];++g.entityGeneration[1];
    log("snapshot_entity_bindings", "\"p1_entity\":21760,\"p2_entity\":21761,\"p1_generation\":%u,\"p2_generation\":%u,\"connection_generation\":%u,\"map_epoch\":%u",g.entityGeneration[0],g.entityGeneration[1],g.network->generation(),g.network->mapEpoch());
#endif
    g.p2Start = position(g.p2.actor); g.schedule = true; g.phase = Phase::Live;
    snapshot("independent_spawn");
}
void __fastcall hookTick(void* self) {
    if (self != instance() || !thread()) {
        ++g.foreignCalls; block("tick_identity_or_thread");
        reinterpret_cast<Unary>(g.original[profile::Tick])(self); return;
    }
    if (!g.observedTid) { g.observedTid = GetCurrentThreadId(); log("simulation_attested", "\"expected_tid\":%lu,\"observed_tid\":%lu,\"game_instance\":\"%p\"", g.expectedTid, g.observedTid, self); }
    if (g.inTick) { block("nested_tick"); return; }
    g.inTick = true; g.localThisTick = 0; ++g.ticks;
    if (g.shutdown) ++g.shutdownTicks;
#ifdef PSE_M12A
#ifdef PSE_DOC56_MAPREADY
    if (g.network && g.network->running()) {
        map56::Identity current;
        const auto fault=map56::derive(map56::observe(g.base),current);
        if (fault!=g.lastMapFault || g.ticks%600==0)
            log("map_identity_status", "\"tick\":%llu,\"fault\":\"%s\",\"map\":\"%s\",\"count_a\":%llu,\"count_b\":%llu,\"world\":%u,\"peer\":%u,\"epoch\":%u",g.ticks,map56::text(fault),current.name.c_str(),current.countA,current.countB,static_cast<unsigned>(g.network->mapGate().world()),static_cast<unsigned>(g.network->mapGate().peer()),g.network->mapEpoch());
        g.lastMapFault=fault;
        g.network->verifyNativeMap(fault,current);
    }
#endif
    if (g.network) g.network->tick(g.ticks, gameplay() && !g.failed && !g.worldExit && !g.shutdown);
    g.foregroundPid=0; GetWindowThreadProcessId(GetForegroundWindow(),&g.foregroundPid);
    if (g.backgroundPeriod && g.focusKnown && !g.focusLost) {
        g.backgroundPeriod=false; g.resumeNeutral=true; g.bindingsResetPending=true;
        log("background_focus_return", "\"tick\":%llu,\"neutral_updates\":%llu,\"first_update_drain\":true",g.ticks,g.neutralUpdates);
    }
    const background::Conditions c{thread(), g.network && g.network->running(), g.network && g.network->wantsPlayer(),
        gameplay(), read<int>(instance(),0x20)==3, read<Byte>(instance(),background::MenuByte)!=0,
        g.backgroundStopping || g.worldExit || g.shutdown, g.failed, g.focusKnown, g.focusLost};
    const bool allowed=background::eligible(c) && g.foregroundPid!=GetCurrentProcessId();
    auto& focusByte=*reinterpret_cast<Byte*>(instance()+background::FocusByte);
    const bool applied=g.focusLease.acquire(focusByte,g.focusSerial,allowed);
    if (applied) {
        ++g.backgroundTicks;
        if (!g.backgroundPeriod) {
            g.backgroundPeriod=true; g.bindingsResetPending=true;
            log("background_session_begin", "\"tick\":%llu,\"generation\":%u,\"p2_exists\":%s,\"foreground_pid\":%lu,\"native_focus_before\":1",g.ticks,g.network->generation(),g.schedule ? "true" : "false",g.foregroundPid);
        }
    }
    g.neutralPrimary=applied || g.resumeNeutral;
#endif
    reinterpret_cast<Unary>(g.original[profile::Tick])(self);
#ifdef PSE_M12A
    endFocusLease();
    if (applied && (g.backgroundTicks<=3 || g.backgroundTicks%120==0))
        log("background_tick", "\"tick\":%llu,\"background_ticks\":%llu,\"local_this_tick\":%u,\"focus_restored\":%u,\"foreground_pid\":%lu,\"neutral_updates\":%llu,\"menu\":%u,\"inventory\":%u",g.ticks,g.backgroundTicks,g.localThisTick,focusByte,g.foregroundPid,g.neutralUpdates,read<Byte>(instance(),background::MenuByte),read<Byte>(g.base+0xa43f30,0x1b0));
    if (!g.backgroundStopping && g.ticks%120==0)
        log("background_policy_status", "\"tick\":%llu,\"state\":%d,\"next\":%d,\"pending\":%d,\"focus_byte_after\":%u,\"focus_known\":%s,\"focus_lost\":%s,\"menu\":%u,\"inventory\":%u,\"network_running\":%s,\"session_ready\":%s,\"override_this_tick\":%s,\"local_this_tick\":%u,\"local_updates\":%llu,\"foreground_pid\":%lu,\"buttons\":%u,\"held_forward\":%u,\"held_back\":%u",g.ticks,read<int>(instance(),0x24),read<int>(instance(),0x20),read<int>(instance(),0x28),focusByte,g.focusKnown ? "true" : "false",g.focusLost ? "true" : "false",read<Byte>(instance(),background::MenuByte),read<Byte>(g.base+0xa43f30,0x1b0),g.network && g.network->running() ? "true" : "false",g.network && g.network->wantsPlayer() ? "true" : "false",applied ? "true" : "false",g.localThisTick,g.localUpdates,g.foregroundPid,read<unsigned>(primary()),read<unsigned>(primary(),12),read<unsigned>(primary(),16));
    g.neutralPrimary=false;
#endif
    if (!g.failed && !g.shutdown && !g.worldExit && gameplay()) {
        if (g.phase == Phase::Waiting && g.permit.load(std::memory_order_acquire)) { g.phase = Phase::Armed; log("test_permit_consumed", "\"mailbox_only\":true"); }
        if (g.localThisTick == 1) {
            ++g.activeTicks;
            if (g.phase == Phase::Armed && ++g.attestSamples >= 60) {
#ifdef PSE_M12A
                if (!g.network) {
                    g.network = new m12a::Session(log);
#if defined(PSE_DOC56_MAPREADY)
                    {
                        const auto observed=map56::observe(g.base);map56::Identity id;
                        const auto fault=map56::derive(observed,id);
                        const auto epoch=fault==map56::Fault::None ? map56::randomEpoch(0) : 0;
                        log("map_identity_observed", "\"fault\":\"%s\",\"state\":%d,\"next\":%d,\"pending\":%d,\"name_length\":%llu,\"name_capacity\":%llu,\"map\":\"%s\",\"mapinfo\":\"%p\",\"mapinfo_key\":\"%s\",\"level_vtable\":%s,\"psm_version\":%u,\"count_a\":%llu,\"count_b\":%llu,\"source\":\"GameInstance+1e0 kexStr / +1f8 mapInfo / level a45940\"",
                            map56::text(fault),observed.state,observed.next,observed.pending,observed.nameLength,observed.nameCapacity,id.name.c_str(),reinterpret_cast<void*>(observed.mapInfo),fault==map56::Fault::None ? observed.key.data() : "",observed.levelVtable ? "true" : "false",observed.version,observed.countA,observed.countB);
                        if(fault!=map56::Fault::None || !epoch || !g.network->configureNativeMap(id,epoch)) { block("native_map_identity_bind");g.inTick=false;return; }
                        log("map_identity_bound", "\"map\":\"%s\",\"map_epoch\":%u,\"epoch_source\":\"BCryptGenRandom per host session binding\",\"tick\":%llu,\"world_synchronized\":false",id.name.c_str(),epoch,g.ticks);
                    }
#elif defined(PSE_DOC55_SNAPSHOT)
                    if(!firstMapWitness() || g.ticks>UINT32_MAX || !g.network->configureNativeSnapshots(static_cast<std::uint32_t>(g.ticks))) { block("snapshot_first_level_epoch_witness");g.inTick=false;return; }
                    log("snapshot_epoch_bound", "\"map_epoch\":%u,\"source\":\"host_attested_first_level_session_tick\",\"tick\":%llu,\"domain\":\"%s\",\"native_mapready_general_contract\":false,\"initial_sector\":259",g.network->mapEpoch(),g.ticks,snapshot55::Domain);
#endif
                    if (!g.network->start()) block("network_start");
                }
                if (!g.failed && g.network->wantsPlayer()) construct();
#else
                construct();
#endif
            }
            else if (g.phase == Phase::Live) {
                if (!live(g.p1, primary()) || !live(g.p2, player()) || !guards()) block("tick_identity_or_guards");
                else {
                    const Vec3 v = position(g.p2.actor);
                    if (!finite(v)) block("nonfinite_native_movement");
                    else {
                        const float dx=v.x-g.p2Start.x, dy=v.y-g.p2Start.y;
                        g.maxP2Distance = std::max(g.maxP2Distance, std::sqrt(dx*dx+dy*dy));
#ifndef PSE_M12A
                        if (g.secondaryUpdates == 45 || g.secondaryUpdates == 60 || g.secondaryUpdates == 105) snapshot("movement_checkpoint");
                        if (g.secondaryUpdates == 120) {
                            g.schedule = false; g.phase = Phase::Complete; snapshot("sequence_complete");
                            log("sequence_metrics", "\"native_p2_updates\":%llu,\"native_p2_puppet_ticks\":%llu,\"native_p2_commands\":%u,\"input_comparisons\":%llu,\"max_native_xy_displacement\":%.6f,\"foreign_calls\":%llu,\"neutral_final_buttons\":%u,\"failed\":%s",
                                g.secondaryUpdates, g.secondaryPuppetTicks, g.commandCalls, g.inputComparisons, g.maxP2Distance, g.foreignCalls, read<unsigned>(player()), g.failed ? "true" : "false");
                        }
#else
                        if (g.p2Tick == g.ticks && g.motionTick != g.ticks) {
                            g.motionTick = g.ticks;
                            log("network_native_motion", "\"tick\":%llu,\"slot\":1,\"generation\":%u,\"input_seq\":%u,\"native_buttons\":%u,\"p2_actor\":\"%p\",\"before\":[%.6f,%.6f,%.6f],\"after\":[%.6f,%.6f,%.6f],\"p2_health\":%d,\"p1_health\":%d,\"puppet_ticks\":%llu,\"updates\":%llu,\"guards\":%s", g.ticks,g.appliedGeneration,g.appliedSequence,g.appliedButtons,g.p2.actor,g.motionBefore.x,g.motionBefore.y,g.motionBefore.z,v.x,v.y,v.z,read<std::int16_t>(g.p2.actor,0x17a),read<std::int16_t>(g.p1.actor,0x17a),g.secondaryPuppetTicks,g.secondaryUpdates,guards() ? "true" : "false");
                        }
#endif
                    }
                }
            }
        }
    }
#ifdef PSE_DOC55_SNAPSHOT
    captureNativeSnapshots();
#endif
    // Always retain the first real P1 update, even after a long menu/startup.
    // The periodic diagnostic horizon must not suppress the isolation/readiness gate.
    if ((g.localUpdates == 1 && g.localThisTick == 1) || g.ticks <= 3 || (g.ticks % 120 == 0 && g.ticks <= 2400)) {
        log("tick_status", "\"ticks\":%llu,\"state\":%d,\"pending\":%d,\"local_updates\":%llu,\"local_this_tick\":%u,\"phase\":%d,\"p2_updates\":%llu",
            g.ticks, read<int>(instance(),0x24), read<int>(instance(),0x28), g.localUpdates, g.localThisTick, static_cast<int>(g.phase), g.secondaryUpdates);
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
#ifdef PSE_M12A
    const auto exact=[](const Byte* address, const auto& bytes) { return std::equal(std::begin(bytes),std::end(bytes),address); };
    if (!exact(g.base+background::FocusResponder,background::ResponderPrefix) ||
        !exact(g.base+background::ResetBindings,background::ResetPrefix) ||
        !exact(g.base+background::FocusGate,background::GatePrefix)) { block("background_exact_profile"); return; }
#endif
    if (MH_Initialize() != MH_OK) { block("minhook_initialize"); return; }
    void* hooks[] = {reinterpret_cast<void*>(&hookTick), reinterpret_cast<void*>(&hookUpdate), reinterpret_cast<void*>(&hookCommands), reinterpret_cast<void*>(&hookTurning), reinterpret_cast<void*>(&hookGetter),
        reinterpret_cast<void*>(&hookStateExit), reinterpret_cast<void*>(&hookShutdown), reinterpret_cast<void*>(&hookDtor), reinterpret_cast<void*>(&hookPuppetTick), reinterpret_cast<void*>(&hookEngineShutdown), reinterpret_cast<void*>(&hookFinal)};
    for (std::size_t n=0; n < std::size(hooks); ++n) {
        const auto i=static_cast<profile::Index>(n);
        const auto status = MH_CreateHook(at(i), hooks[n], &g.original[n]);
        if (status != MH_OK) { log("hook_create_error", "\"site\":\"%s\",\"status\":%d", profile::Sites[n].name, status); block("minhook_create"); return; }
        g.created[n] = true;
    }
#ifdef PSE_M12A
    if (MH_CreateHook(g.base+background::FocusResponder,reinterpret_cast<void*>(&hookFocusResponder),&g.focusOriginal)!=MH_OK ||
        MH_EnableHook(g.base+background::FocusResponder)!=MH_OK) { block("background_focus_hook"); return; }
    log("background_hook_ready", "\"responder_rva\":%u,\"focus_byte_offset\":%u,\"scope\":\"READY host gameplay StateTick only\",\"lifetime\":\"pinned until process exit\"",background::FocusResponder,background::FocusByte);
#endif
    for (std::size_t n=0; n < std::size(hooks); ++n) {
        const auto i=static_cast<profile::Index>(n);
        if (i != profile::Getter && !setHook(i, true)) return;
    }
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
extern "C" __declspec(dllexport) DWORD WINAPI M11Prepare(const wchar_t* logPath, DWORD primaryTid) {
    if (g.prepared.exchange(1) != 0) return ERROR_ALREADY_INITIALIZED;
    wchar_t exe[32768]{}, dll[32768]{}, fullLog[32768]{};
    if (!GetModuleFileNameW(nullptr, exe, 32768) || _wcsicmp(exe, profile::Executable) != 0 || !within(exe, profile::Workspace) || !hashMatches(exe)) return ERROR_BAD_EXE_FORMAT;
    if (!GetModuleFileNameW(g.module, dll, 32768) || !within(dll, profile::Workspace)) return ERROR_BAD_PATHNAME;
    const DWORD length=GetFullPathNameW(logPath, 32768, fullLog, nullptr);
    if (!length || length >= 32768 || !within(fullLog, profile::Workspace)) return ERROR_BAD_PATHNAME;
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&M11Prepare), &pinned)) return GetLastError();
    g.expectedTid=primaryTid; g.base=reinterpret_cast<Byte*>(GetModuleHandleW(nullptr));
    g.log=CreateFileW(fullLog, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
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
extern "C" __declspec(dllexport) DWORD WINAPI M11PermitTest() { g.permit.store(1, std::memory_order_release); return ERROR_SUCCESS; }

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) g.module=module;
    // No hooks, threads, KEX calls, file I/O, free, or detach cleanup under loader lock.
    return TRUE;
}
