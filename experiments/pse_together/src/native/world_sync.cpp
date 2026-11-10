// См. world_sync.hpp. Хуки: SpawnMover (все моверы) и kexSlavePickupKey::OnTouch (ключи).
#include "world_sync.hpp"
#include <windows.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace pse::world {
namespace {
using Byte = unsigned char;
using SpawnFn = void*(__fastcall*)(void*, const char*, int, int);
using TouchFn = void(__fastcall*)(void*, void*);

constexpr std::uint32_t SpawnRva = 0x1d4e20, KeyTouchRva = 0x1d8a00, FactoryRva = 0x5a10a0;
constexpr std::uint32_t AiTickRva = 0x194ce0, AiDamageRva = 0x195050, ActorTickRva = 0x19f2d0, ChangeAnimRva = 0x1a1170, AiVtable = 0x527610;
constexpr std::uint32_t SectorsPtrRva = 0xa45968, SectorCountRva = 0xa45970, SectorStride = 0x88;
constexpr std::uint32_t PuppetVtable = 0x52d198, KeyVtable = 0x52c998;
// Имена классов — те самые строки движка (.rdata): фабрика получает указатель на них, свои копии не подставляем.
constexpr std::array<std::uint32_t, 6> ClassRva{0x52aec0, 0x52afd0, 0x52af18, 0x52af80, 0x52b6a8, 0x52b758};
constexpr std::array<const char*, 6> ClassName{"Door", "Lift", "LiftImmediate", "Floor", "DropPad", "FloatingPlatform"};
// Первые 16 байт функций (профиль точной сборки EXE; при несовпадении хуки не ставятся).
constexpr std::array<Byte, 16> SpawnPrefix{0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x57, 0x41, 0x56, 0x48, 0x83, 0xec, 0x30, 0x45, 0x8b, 0xf0};
constexpr std::array<Byte, 16> KeyPrefix{0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x83, 0xb9, 0x70, 0x01, 0x00};

constexpr std::array<Byte, 16> AiTickPrefix{0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x80, 0x79, 0x40, 0x00, 0x48, 0x8b, 0xd9, 0x74, 0x25, 0x48};
constexpr std::array<Byte, 16> AiDamagePrefix{0x48, 0x89, 0x5c, 0x24, 0x20, 0x57, 0x48, 0x83, 0xec, 0x20, 0x66, 0x83, 0xb9, 0x7a, 0x01, 0x00};
// ActorTick (0x19f2d0) не сверяем: прокси хоста-игрока уже держит на нём свой хук и пропускает чужие акторы.
constexpr std::array<Byte, 16> ChangeAnimPrefix{0x48, 0x85, 0xd2, 0x0f, 0x84, 0xc6, 0x00, 0x00, 0x00, 0x56, 0x48, 0x83, 0xec, 0x20, 0x48, 0x89};
// InflictDamage(actor, источник, тип, количество): общая точка урона движка (враги, игроки, ловушки).
constexpr std::uint32_t DamageRva = 0x1a1370;
constexpr std::array<Byte, 16> DamagePrefix{0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
using DamageFn = void(__fastcall*)(void*, void*, int, short);
constexpr std::size_t QueueCap = 64;
struct State {
    Byte* base{};
    std::uint32_t tid{};
    Logger log{};
    SpawnFn spawn{};
    TouchFn keyTouch{};
    bool installed{}, active{}, replaying{}, client{};
    void* aiTick{}; void* aiDamage{}; void* damage{};
    Byte* ownActor{}; bool hostDamage{}, friendlyFire{}; std::uint64_t suppressedDamage{}, hostHits{}, friendlySuppressed{};
    std::uint64_t lastEnemyTick{}, enemyApplies{}, enemyMoves{};
    std::array<Mover, QueueCap> movers{};
    std::size_t head{}, count{};
    std::uint16_t keysGained{};
    std::uint64_t localMovers{}, appliedMovers{}, busySkips{}, ticks{};
    struct Watch { int sector{-1}; unsigned left{}; } watch[4]{};
    int debugCls{-1}, debugType{}, debugSector{-1};
    std::uint16_t debugKeyBits{};
    bool debugTeleportOn{}; float debugTx{}, debugTy{}, debugTyaw{};
    int debugEnemy{-1}, debugKill{-1};
} s;

template <class T> T read(const void* p, std::size_t off = 0) { return *reinterpret_cast<const T*>(static_cast<const Byte*>(p) + off); }
bool thread() { return GetCurrentThreadId() == s.tid; }
void note(const char* event, const char* fmt, ...);
void watchSector(int sector) {
    for (auto& w : s.watch) if (w.left == 0) { w.sector = sector; w.left = 10; return; }
}

int classIndex(const char* name) {
    for (std::size_t i = 0; i < ClassRva.size(); ++i)
        if (name == reinterpret_cast<const char*>(s.base + ClassRva[i])) return static_cast<int>(i);
    for (std::size_t i = 0; i < ClassRva.size(); ++i) // другая копия строки: сравнить по содержимому
        if (name && std::strcmp(name, reinterpret_cast<const char*>(s.base + ClassRva[i])) == 0) return static_cast<int>(i);
    return -1;
}
std::uint64_t sectorCount() { return read<std::uint64_t>(s.base, SectorCountRva); }
Byte* sectorAt(std::int64_t i) {
    auto* all = read<Byte*>(s.base, SectorsPtrRva);
    if (!all || i < 0 || static_cast<std::uint64_t>(i) >= sectorCount() || sectorCount() > 8192) return nullptr;
    return all + static_cast<std::size_t>(i) * SectorStride;
}

void* __fastcall hookSpawn(void* self, const char* name, int type, int sector) {
    void* result = s.spawn(self, name, type, sector);
    if (!thread() || !s.installed) return result;
    const int cls = result && name ? classIndex(name) : -1;
    if (s.active && !s.replaying && cls >= 0 && type >= 0 && type <= 255 && sector >= 0 && sector <= 32767) {
        ++s.localMovers;
        watchSector(sector);
        if (s.count < QueueCap) {
            s.movers[(s.head + s.count++) % QueueCap] = Mover{static_cast<std::uint8_t>(cls), static_cast<std::uint8_t>(type), static_cast<std::int16_t>(sector)};
        }
        note("world_mover_local", "\"class\":\"%s\",\"type\":%d,\"sector\":%d,\"queued\":%zu", ClassName[cls], type, sector, s.count);
    } else if (!s.replaying && result) {
        note("world_mover_ignored", "\"class\":\"%s\",\"type\":%d,\"sector\":%d,\"active\":%s", name ? name : "?", type, sector, s.active ? "true" : "false");
    }
    return result;
}

int animOffset(const Byte* a) { // смещение поля дефиниции, на которое указывает текущая анимация (как в collectEnemies)
    const auto* def = read<Byte*>(a, 0x170); const auto* anim = read<Byte*>(a, 0x108);
    for (unsigned off = 0x80; off <= 0x200 && anim && def; off += 8) if (read<Byte*>(def, off) == anim) return static_cast<int>(off);
    return -1;
}
bool replicaOn() { return s.installed && s.client && s.active && s.ticks - s.lastEnemyTick < 90; }
// Клиент: вместо ИИ - базовый тик актора (анимация, физика), без решений, атак и урона; хост присылает остальное.
void __fastcall hookAiTick(void* actor) {
    if (!thread() || !replicaOn()) { reinterpret_cast<void(__fastcall*)(void*)>(s.aiTick)(actor); return; }
    reinterpret_cast<void(__fastcall*)(void*)>(s.base + ActorTickRva)(actor);
}
// Реакция ИИ на урон (смерть, смена цели): на клиенте решает только хост.
void __fastcall hookAiDamage(void* actor, void* instigator) {
    if (thread() && replicaOn()) return;
    reinterpret_cast<void(__fastcall*)(void*, void*)>(s.aiDamage)(actor, instigator);
}

// Клиент: здоровье своего игрока решает хост (P2 там получает урон от настоящего ИИ). Локальный урон по своему актору — ловушки,
// лишние попадания — глушим, иначе клиент мог бы умереть, пока хост считает его живым. Урон хоста идёт через applyHostHealth.
bool isPuppet(const void* actor) {
    __try { return actor && read<const Byte*>(actor) == s.base + PuppetVtable; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void __fastcall hookDamage(void* actor, void* instigator, int kind, short amount) {
    // Дружественный огонь выключен по умолчанию: игрок (Puppet) не ранит другого игрока. Урон себе (взрыв, ловушка) остаётся.
    if (thread() && s.installed && !s.friendlyFire && instigator && instigator != actor && isPuppet(actor) && isPuppet(instigator)) {
        if (++s.friendlySuppressed % 30 == 1) note("world_friendly_fire_blocked", "\"kind\":%d,\"amount\":%d,\"total\":%llu", kind, static_cast<int>(amount), s.friendlySuppressed);
        return;
    }
    if (thread() && s.client && s.ownActor && actor == s.ownActor && !s.hostDamage) {
        if (++s.suppressedDamage % 60 == 1) note("world_own_damage_suppressed", "\"kind\":%d,\"amount\":%d,\"total\":%llu", kind, static_cast<int>(amount), s.suppressedDamage);
        return;
    }
    reinterpret_cast<DamageFn>(s.damage)(actor, instigator, kind, amount);
}

void __fastcall hookKeyTouch(void* key, void* instigator) {
    Byte* owner = nullptr;
    if (thread() && s.installed && instigator && read<Byte*>(instigator) == s.base + PuppetVtable) owner = read<Byte*>(instigator, 0x1b8);
    const std::uint16_t before = owner ? read<std::uint16_t>(owner, 0x98) : 0;
    s.keyTouch(key, instigator);
    if (!owner) return;
    const std::uint16_t gained = static_cast<std::uint16_t>(read<std::uint16_t>(owner, 0x98) & ~before & 0xf);
    if (gained && s.active && !s.replaying) { s.keysGained |= gained; note("world_key_local", "\"gained\":%u,\"keys\":%u", gained, read<std::uint16_t>(owner, 0x98)); }
}

// Обход списков акторов по секторам (как в read-only переписи спавна host.cpp): sector+0x78 — голова двусвязного списка.
template <class F> void forEachActor(F&& f) {
    const auto count = sectorCount();
    auto* all = read<Byte*>(s.base, SectorsPtrRva);
    if (!all || count > 8192) return;
    for (std::uint64_t i = 0; i < count; ++i) {
        auto* head = read<Byte*>(all + i * SectorStride, 0x78);
        if (!head || read<Byte*>(head)) continue;
        auto* previous = head;
        auto* node = read<Byte*>(head, 8);
        unsigned walked = 0;
        while (node && read<Byte*>(node, 8)) {
            if (++walked > 4096 || read<Byte*>(node) != previous) break;
            auto* actor = read<Byte*>(node, 0x28);
            if (!actor || read<Byte*>(actor, 0x10) != node) break;
            const auto next = read<Byte*>(node, 8); // actor может удалить себя во время f
            if (f(actor, static_cast<int>(i))) return;
            previous = node;
            node = next;
        }
    }
}
Byte* findKeyActor(int keyType) {
    Byte* found = nullptr;
    forEachActor([&](Byte* actor, int) {
        if (read<Byte*>(actor) != s.base + KeyVtable) return false;
        const auto* def = read<Byte*>(actor, 0x170);
        if (!def || read<int>(def, 0x13c) != keyType || !(read<unsigned>(actor, 0x16c) & 0x100u)) return false;
        found = actor;
        return true;
    });
    return found;
}

void note(const char* event, const char* fmt, ...) {
    if (!s.log) return;
    char detail[600]{};
    va_list args; va_start(args, fmt);
    vsnprintf_s(detail, sizeof(detail), _TRUNCATE, fmt, args); va_end(args);
    s.log(event, "%s", detail);
}
}

bool install(Byte* base, std::uint32_t simTid, Logger log, bool host) {
    s.base = base; s.tid = simTid; s.log = log;
    if (GetCurrentThreadId() != simTid) return false;
    if (!std::equal(SpawnPrefix.begin(), SpawnPrefix.end(), base + SpawnRva) ||
        !std::equal(KeyPrefix.begin(), KeyPrefix.end(), base + KeyTouchRva)) { note("world_profile_mismatch", "\"site\":\"spawn_or_key\""); return false; }
    if (MH_CreateHook(base + SpawnRva, reinterpret_cast<void*>(&hookSpawn), reinterpret_cast<void**>(&s.spawn)) != MH_OK ||
        MH_EnableHook(base + SpawnRva) != MH_OK) { note("world_hook_failed", "\"site\":\"SpawnMover\""); return false; }
    if (MH_CreateHook(base + KeyTouchRva, reinterpret_cast<void*>(&hookKeyTouch), reinterpret_cast<void**>(&s.keyTouch)) != MH_OK ||
        MH_EnableHook(base + KeyTouchRva) != MH_OK) { note("world_hook_failed", "\"site\":\"KeyOnTouch\""); return false; }
    s.client = !host;
    {   // Урон: клиент глушит локальный урон своему игроку, обе стороны — дружественный огонь (по умолчанию выключен, PSE_FRIENDLY_FIRE=1 включает).
        char v[8]{};
        s.friendlyFire = GetEnvironmentVariableA("PSE_FRIENDLY_FIRE", v, sizeof(v)) && v[0] == '1';
        if (!std::equal(DamagePrefix.begin(), DamagePrefix.end(), base + DamageRva)) { note("world_profile_mismatch", "\"site\":\"damage\""); return false; }
        if (MH_CreateHook(base + DamageRva, reinterpret_cast<void*>(&hookDamage), &s.damage) != MH_OK || MH_EnableHook(base + DamageRva) != MH_OK) {
            note("world_hook_failed", "\"site\":\"Damage\""); return false;
        }
    }
    if (!host) { // враги хоста: ИИ клиента заменён репликой, пока приходят снимки
        if (!std::equal(AiTickPrefix.begin(), AiTickPrefix.end(), base + AiTickRva) || !std::equal(AiDamagePrefix.begin(), AiDamagePrefix.end(), base + AiDamageRva) ||
            !std::equal(ChangeAnimPrefix.begin(), ChangeAnimPrefix.end(), base + ChangeAnimRva)) {
            note("world_profile_mismatch", "\"site\":\"ai\""); return false;
        }
        if (MH_CreateHook(base + AiTickRva, reinterpret_cast<void*>(&hookAiTick), &s.aiTick) != MH_OK || MH_EnableHook(base + AiTickRva) != MH_OK ||
            MH_CreateHook(base + AiDamageRva, reinterpret_cast<void*>(&hookAiDamage), &s.aiDamage) != MH_OK || MH_EnableHook(base + AiDamageRva) != MH_OK) {
            note("world_hook_failed", "\"site\":\"AI\""); return false;
        }
    }
    s.installed = true;
    {   // Тестовый запуск без игрока у двери: PSE_DEBUG_MOVER="класс,type,сектор" (класс 0=Door). Игрой не используется.
        char v[64]{};
        if (GetEnvironmentVariableA(host ? "PSE_DEBUG_MOVER" : "PSE_DEBUG_MOVER_CLIENT", v, sizeof(v))) {
            if (std::strcmp(v, "auto") == 0) { s.debugCls = 0; s.debugType = 1; s.debugSector = -2; }
            else if (std::strcmp(v, "auto2") == 0) { s.debugCls = 0; s.debugType = 1; s.debugSector = -3; }
            else if (sscanf_s(v, "%d,%d,%d", &s.debugCls, &s.debugType, &s.debugSector) != 3) s.debugCls = -1;
        }
    }
    {
        char v[16]{};
        if (GetEnvironmentVariableA(host ? "PSE_DEBUG_KEY" : "PSE_DEBUG_KEY_CLIENT", v, sizeof(v))) s.debugKeyBits = static_cast<std::uint16_t>(std::atoi(v) & 0xf);
    }
    {
        char v[96]{};
        if (GetEnvironmentVariableA(host ? "PSE_DEBUG_TELEPORT" : "PSE_DEBUG_TELEPORT_CLIENT", v, sizeof(v)))
            s.debugTeleportOn = sscanf_s(v, "%f,%f,%f", &s.debugTx, &s.debugTy, &s.debugTyaw) == 3;
    }
    {
        char v[16]{};
        if (GetEnvironmentVariableA("PSE_DEBUG_ENEMY", v, sizeof(v))) s.debugEnemy = std::atoi(v);
        if (host && GetEnvironmentVariableA("PSE_DEBUG_KILL", v, sizeof(v))) s.debugKill = std::atoi(v);
    }
    note("world_hooks_ready", "\"spawn_rva\":%u,\"key_touch_rva\":%u", SpawnRva, KeyTouchRva);
    return true;
}

void setActive(bool on) {
    if (!thread()) return;
    if (on) {
        ++s.ticks;
        if (s.debugEnemy >= 0 && s.ticks % 30 == 0)
            forEachActor([&](Byte* a, int sector) {
                if (read<Byte*>(a) != s.base + AiVtable || read<int>(a, 0x180) != s.debugEnemy) return false;
                note("world_enemy_probe", "\"id\":%d,\"pos\":[%.1f,%.1f,%.1f],\"yaw\":%.2f,\"hp\":%d,\"ai\":%d,\"anim\":\"%p\",\"frame\":%d,\"sector\":%d,\"replica\":%s,\"f16c\":\"%x\",\"f1c0\":\"%x\",\"scale\":%.2f,\"anim_off\":%d", s.debugEnemy,
                     read<float>(a, 0x18), read<float>(a, 0x1c), read<float>(a, 0x20), read<float>(a, 0x38), read<std::int16_t>(a, 0x17a), read<int>(a, 0x1b8), read<void*>(a, 0x108), read<int>(a, 0x178), sector, replicaOn() ? "true" : "false", read<unsigned>(a, 0x16c), read<unsigned>(a, 0x1c0), read<float>(a, 0x124), animOffset(a));
                return true;
            });
        if (s.ticks % 20 == 0)
            for (auto& w : s.watch) if (w.left) {
                --w.left; const auto p = probe(w.sector);
                note("world_sector_watch", "\"sector\":%d,\"flags\":%u,\"floor_d\":%.3f,\"ceiling_d\":%.3f,\"left\":%u", w.sector, p.flags, p.floorD, p.ceilingD, w.left);
            }
    }
    if (!on) s.ownActor = nullptr;
    if (on == s.active) return;
    s.active = on;
    s.head = s.count = 0; s.keysGained = 0;
    note("world_sync_active", "\"active\":%s", on ? "true" : "false");
}
bool popMover(Mover& out) {
    if (!s.installed || !s.count) return false;
    out = s.movers[s.head]; s.head = (s.head + 1) % QueueCap; --s.count;
    return true;
}
std::uint16_t popKeysGained() { const auto v = s.keysGained; s.keysGained = 0; return v; }

bool applyMover(const Mover& m) {
    if (!thread() || !s.installed || m.cls >= ClassRva.size()) return false;
    auto* sector = sectorAt(m.sector);
    if (!sector) { note("world_mover_invalid", "\"sector\":%d", m.sector); return false; }
    if (read<Byte>(sector, 0x14) & 4) { ++s.busySkips; note("world_mover_busy", "\"class\":\"%s\",\"type\":%u,\"sector\":%d", ClassName[m.cls], m.type, m.sector); return false; }
    s.replaying = true;
    void* r = s.spawn(s.base + FactoryRva, reinterpret_cast<const char*>(s.base + ClassRva[m.cls]), m.type, m.sector);
    s.replaying = false;
    watchSector(m.sector);
    ++s.appliedMovers;
    const auto p = probe(m.sector);
    note("world_mover_applied", "\"class\":\"%s\",\"type\":%u,\"sector\":%d,\"spawned\":%s,\"flags\":%u,\"floor_d\":%.3f,\"ceiling_d\":%.3f", ClassName[m.cls], m.type, m.sector, r ? "true" : "false", p.flags, p.floorD, p.ceilingD);
    return r != nullptr;
}

bool applyKeys(Byte* player, Byte* puppet, std::uint16_t mask) {
    if (!thread() || !s.installed || !player) return false;
    mask &= 0xf;
    for (int bit = 0; bit < 4; ++bit) {
        if (!(mask >> bit & 1) || (read<std::uint16_t>(player, 0x98) >> bit & 1)) continue;
        bool viaTouch = false;
        if (Byte* actor = puppet ? findKeyActor(bit) : nullptr) {
            s.replaying = true;
            s.keyTouch(actor, puppet); // нативно: бит, звук, надпись, вспышка, Remove()
            s.replaying = false;
            viaTouch = (read<std::uint16_t>(player, 0x98) >> bit & 1) != 0;
        }
        if (!viaTouch) *reinterpret_cast<std::uint16_t*>(player + 0x98) = static_cast<std::uint16_t>(read<std::uint16_t>(player, 0x98) | (1u << bit));
        note("world_key_applied", "\"bit\":%d,\"via_touch\":%s,\"keys\":%u", bit, viaTouch ? "true" : "false", read<std::uint16_t>(player, 0x98));
    }
    return true;
}

SectorProbe probe(int sector) {
    SectorProbe p;
    auto* sec = sectorAt(sector);
    if (!sec) return p;
    p.valid = true; p.flags = read<std::uint16_t>(sec, 0x14);
    if (auto* f = read<Byte*>(sec, 0x68)) p.floorD = read<float>(f, 0x18);
    if (auto* c = read<Byte*>(sec, 0x70)) p.ceilingD = read<float>(c, 0x18);
    return p;
}

void debugSpawn() {
    if (!thread() || !s.installed || s.debugCls < 0 || s.debugCls >= static_cast<int>(ClassRva.size())) return;
    const auto cls = s.debugCls; s.debugCls = -1;
    if (s.debugSector < -1) { // auto/auto2: первая/вторая обычная дверь (type 1) карты
        const int want = s.debugSector == -2 ? 0 : 1; int seen = 0;
        const auto nev = read<std::uint64_t>(s.base, 0xa459d0);
        auto* ev = read<Byte*>(s.base, 0xa459c8);
        s.debugSector = -1;
        for (std::uint64_t i = 0; ev && i < nev; ++i) if (read<std::int16_t>(ev + i * 8) == 1 && seen++ == want) { s.debugSector = read<std::int16_t>(ev + i * 8, 2); break; }
    }
    note("world_debug_spawn", "\"class\":%d,\"type\":%d,\"sector\":%d", cls, s.debugType, s.debugSector);
    // Через хук, как вызов из UseWallSpecial: попадёт в очередь и уйдёт второй стороне.
    reinterpret_cast<SpawnFn>(s.base + SpawnRva)(s.base + FactoryRva, reinterpret_cast<const char*>(s.base + ClassRva[cls]), s.debugType, s.debugSector);
}

namespace {
using PointFn = std::uint8_t(__fastcall*)(void*, const float*, float);
using HeightFn = float(__fastcall*)(const float*, void*, std::uint8_t);
using LinkFn = void(__fastcall*)(void*, int);
using ResetFn = void(__fastcall*)(void*);
constexpr std::uint32_t PointRva = 0x1cf540, FloorRva = 0x1a3eb0, CeilingRva = 0x1a4060, LinkRva = 0x1d3d70, ResetRva = 0x1d3e60;
constexpr std::array<Byte, 16> PointPrefix{0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
constexpr std::array<Byte, 16> FloorPrefix{0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x0f};
constexpr std::array<Byte, 16> LinkPrefix{0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48};
constexpr std::array<Byte, 16> ResetPrefix{0x48, 0x8b, 0xd1, 0x48, 0x8b, 0x49, 0x08, 0x48, 0x85, 0xc9, 0x75, 0x19, 0x41, 0xb8, 0x55, 0x00};
bool geometryProfile() { // один раз за процесс: точные байты всех вызываемых функций
    static int ok = -1;
    if (ok < 0) {
        auto same = [&](std::uint32_t rva, const std::array<Byte, 16>& p) { return std::equal(p.begin(), p.end(), s.base + rva); };
        ok = same(PointRva, PointPrefix) && same(FloorRva, FloorPrefix) && same(CeilingRva, FloorPrefix) && same(LinkRva, LinkPrefix) && same(ResetRva, ResetPrefix);
    }
    return ok == 1;
}
}

float floorAt(float x, float y, float z, int sector) {
    auto* sec = sectorAt(sector);
    if (!sec || !geometryProfile()) return NAN;
    const float p[3]{x, y, z};
    return reinterpret_cast<HeightFn>(s.base + FloorRva)(p, sec, 1);
}
int findSector(float x, float y, float z) {
    if (!geometryProfile()) return -1;
    const float p[3]{x, y, z};
    for (std::uint64_t i = 0; i < sectorCount() && i < 8192; ++i)
        if (reinterpret_cast<PointFn>(s.base + PointRva)(sectorAt(static_cast<std::int64_t>(i)), p, 0) == 1) return static_cast<int>(i);
    return -1;
}
bool teleport(Byte* a, float x, float y, float z, int sector) {
    if (!thread() || !s.installed || !a || !geometryProfile() || !std::isfinite(x + y + z)) return false;
    auto* sec = sectorAt(sector);
    if (!sec) return false;
    const float p[3]{x, y, z};
    if (reinterpret_cast<PointFn>(s.base + PointRva)(sec, p, 0) != 1) return false;
    const float floor = reinterpret_cast<HeightFn>(s.base + FloorRva)(p, sec, 1), ceiling = reinterpret_cast<HeightFn>(s.base + CeilingRva)(p, sec, 1);
    if (!std::isfinite(floor) || !std::isfinite(ceiling) || !(floor < ceiling)) return false;
    *reinterpret_cast<float*>(a + 0x18) = x; *reinterpret_cast<float*>(a + 0x1c) = y; *reinterpret_cast<float*>(a + 0x20) = z;
    if (read<int>(a, 0x3c) != sector) {
        reinterpret_cast<LinkFn>(s.base + LinkRva)(a, sector);
        if (read<int>(a, 0x3c) != sector) note("world_teleport_link_mismatch", "\"want\":%d,\"have\":%d", sector, read<int>(a, 0x3c));
    }
    *reinterpret_cast<float*>(a + 0x11c) = floor; *reinterpret_cast<float*>(a + 0x120) = ceiling;
    reinterpret_cast<ResetFn>(s.base + ResetRva)(a + 0x50);
    return true;
}
void debugTeleport(Byte* actor) {
    if (!thread() || !s.installed || !s.debugTeleportOn || !actor) return;
    s.debugTeleportOn = false;
    const int sector = findSector(s.debugTx, s.debugTy, read<float>(actor, 0x20));
    const float floor = sector >= 0 ? floorAt(s.debugTx, s.debugTy, read<float>(actor, 0x20), sector) : NAN;
    const bool ok = sector >= 0 && teleport(actor, s.debugTx, s.debugTy, floor, sector);
    if (ok) { *reinterpret_cast<float*>(actor + 0x38) = s.debugTyaw; reinterpret_cast<ResetFn>(s.base + ResetRva)(actor + 0x78); }
    note("world_debug_teleport", "\"ok\":%s,\"sector\":%d,\"floor\":%.1f,\"x\":%.1f,\"y\":%.1f,\"yaw\":%.3f", ok ? "true" : "false", sector, floor, s.debugTx, s.debugTy, s.debugTyaw);
}

void debugKillEnemy() {
    if (!thread() || !s.installed || s.debugKill < 0) return;
    const int id = s.debugKill; s.debugKill = -1;
    forEachActor([&](Byte* a, int) {
        if (read<Byte*>(a) != s.base + AiVtable || read<int>(a, 0x180) != id) return false;
        // Тот же вызов, что и в P7: generic Damage(actor, source, type, amount); виртуальная реакция ИИ (смерть) идёт из него.
        reinterpret_cast<void(__fastcall*)(void*, void*, int, short)>(s.base + 0x1a1370)(a, nullptr, 0, 100);
        note("world_debug_kill", "\"id\":%d,\"hp\":%d,\"ai\":%d", id, read<std::int16_t>(a, 0x17a), read<int>(a, 0x1b8));
        return true;
    });
}

void debugCensusOnFile() {
    static bool done = false;
    if (done || !thread() || !s.installed) return;
    char file[400]{};
    if (!GetEnvironmentVariableA("PSE_DEBUG_CENSUS_FILE", file, sizeof(file)) || GetFileAttributesA(file) == INVALID_FILE_ATTRIBUTES) return;
    done = true;
    census("fight");
}

void debugPullEnemy(Byte* anchor) {
    static int id = -1; static float dist = 300; static bool read_env = false;
    if (!read_env) { read_env = true; char v[48]{}; if (GetEnvironmentVariableA("PSE_DEBUG_PULL_ENEMY", v, sizeof(v))) { int i = -1; float d = 300; const int n = sscanf_s(v, "%d,%f", &i, &d); if (n >= 1) { id = i; dist = n >= 2 ? d : 300.0f; } } }
    if (!thread() || !s.installed || id < 0 || !anchor) return;
    char file[400]{}; // PSE_DEBUG_PULL_FILE: ждать, пока смоук не создаст файл (иначе враг ставится сразу)
    if (GetEnvironmentVariableA("PSE_DEBUG_PULL_FILE", file, sizeof(file)) && GetFileAttributesA(file) == INVALID_FILE_ATTRIBUTES) return;
    const int want = id; id = -1;
    const float yaw = read<float>(anchor, 0x38);
    const int sector = read<int>(anchor, 0x3c);
    forEachActor([&](Byte* a, int) {
        if (read<Byte*>(a) != s.base + AiVtable || read<int>(a, 0x180) != want) return false;
        // Дальше всего перед игроком, где точка ещё в его секторе (сектор — не коридор до арки, а комната вокруг старта).
        for (float d = dist; d >= 60.0f; d -= 30.0f) {
            const float x = read<float>(anchor, 0x18) + d * std::sin(yaw), y = read<float>(anchor, 0x1c) + d * std::cos(yaw);
            const float floor = floorAt(x, y, read<float>(anchor, 0x20), sector);
            if (!std::isfinite(floor) || !teleport(a, x, y, floor, sector)) continue;
            note("world_debug_pull_enemy", "\"id\":%d,\"ok\":true,\"sector\":%d,\"dist\":%.0f,\"to\":[%.1f,%.1f,%.1f]", want, sector, d, x, y, floor);
            return true;
        }
        note("world_debug_pull_enemy", "\"id\":%d,\"ok\":false,\"sector\":%d", want, sector);
        return true;
    });
}

void debugKeys(Byte* player) {
    if (!thread() || !s.installed || !player || s.debugKeyBits == 0) return;
    const auto bits = s.debugKeyBits; s.debugKeyBits = 0;
    *reinterpret_cast<std::uint16_t*>(player + 0x98) = static_cast<std::uint16_t>(read<std::uint16_t>(player, 0x98) | bits);
    if (s.active) s.keysGained |= static_cast<std::uint16_t>(bits); // как подбор у этой стороны
    note("world_debug_key", "\"bits\":%u,\"keys\":%u", bits, read<std::uint16_t>(player, 0x98));
}

bool replicaActive() { return replicaOn(); }

void setFriendlyFire(bool on) { s.friendlyFire = on; }

void setOwnPlayer(Byte* actor) {
    if (!thread() || !s.installed || !s.client) return;
    s.ownActor = s.active ? actor : nullptr;
}

bool applyHostHealth(Byte* a, int hostHp, bool silent) {
    if (!thread() || !s.installed || !s.client || !s.active || !a) return false;
    hostHp = std::clamp(hostHp, -32768, 32767);
    const int cur = read<std::int16_t>(a, 0x17a);
    if (cur == hostHp) return false;
    if (cur <= 0) return false; // уже мёртв: оживляет только респаун (P16), не снимок здоровья
    if (!silent && hostHp < cur) {
        // Нативный урон: красная вспышка экрана, звук, анимация боли/смерти. Количество = разница с хостом.
        s.hostDamage = true;
        reinterpret_cast<DamageFn>(s.damage)(a, nullptr, 0, static_cast<short>(std::min(cur - hostHp, 32767)));
        s.hostDamage = false;
        ++s.hostHits;
    }
    const int after = read<std::int16_t>(a, 0x17a);
    if (after > 0 && hostHp > 0 && after != hostHp) *reinterpret_cast<std::int16_t*>(a + 0x17a) = static_cast<std::int16_t>(hostHp); // броня/лечение: число хоста главнее
    if (hostHp <= 0 && after > 0) { // хост считает игрока мёртвым: добить нативно (смерть, не запись -1)
        s.hostDamage = true;
        reinterpret_cast<DamageFn>(s.damage)(a, nullptr, 0, static_cast<short>(std::min(after + 1, 32767)));
        s.hostDamage = false;
    }
    note("world_own_health", "\"host\":%d,\"before\":%d,\"after\":%d,\"silent\":%s,\"hits\":%llu", hostHp, cur, static_cast<int>(read<std::int16_t>(a, 0x17a)), silent ? "true" : "false", s.hostHits);
    return true;
}

int collectEnemies(Enemy* out, int max) {
    int n = 0;
    if (!thread() || !s.installed) return 0;
    forEachActor([&](Byte* a, int) {
        if (n >= max) return true;
        if (read<Byte*>(a) != s.base + AiVtable) return false;
        const int id = read<int>(a, 0x180);
        const auto* def = read<Byte*>(a, 0x170);
        const auto* node = read<Byte*>(a, 0x10);
        if (id < 0 || !def || !node || (read<Byte>(a, 0x40) && read<int>(node, 0x40) < 1)) return false; // удалён
        Enemy& e = out[n++];
        e = Enemy{};
        e.id = static_cast<std::uint32_t>(id);
        e.x = read<float>(a, 0x18); e.y = read<float>(a, 0x1c); e.z = read<float>(a, 0x20);
        e.vx = read<float>(a, 0xe0); e.vy = read<float>(a, 0xe4); e.vz = read<float>(a, 0xe8);
        e.yaw = read<float>(a, 0x38);
        e.hp = read<std::int16_t>(a, 0x17a); e.sector = static_cast<std::int16_t>(read<int>(a, 0x3c));
        e.aiState = static_cast<std::uint16_t>(read<int>(a, 0x1b8)); e.frame = static_cast<std::uint16_t>(read<int>(a, 0x178));
        const auto* anim = read<Byte*>(a, 0x108); // анимация - указатель в дефиниции: передаём смещение поля дефиниции
        for (unsigned off = 0x80; off <= 0x200 && anim; off += 8) if (read<Byte*>(def, off) == anim) { e.anim = static_cast<std::uint16_t>(off); break; }
        return false;
    });
    return n;
}

void applyEnemies(const Enemy* in, int count) {
    if (!thread() || !s.installed || !s.client || !s.active || count <= 0) return;
    Byte* found[256]{}; std::uint32_t ids[256]{}; int known = 0;
    forEachActor([&](Byte* a, int) {
        if (read<Byte*>(a) == s.base + AiVtable && known < 256) { ids[known] = static_cast<std::uint32_t>(read<int>(a, 0x180)); found[known++] = a; }
        return false;
    });
    s.lastEnemyTick = s.ticks;
    for (int i = 0; i < count; ++i) {
        Byte* a = nullptr;
        for (int k = 0; k < known; ++k) if (ids[k] == in[i].id) { a = found[k]; break; }
        if (!a) continue;
        const Enemy& e = in[i];
        if (!std::isfinite(e.x + e.y + e.z + e.yaw + e.vx + e.vy + e.vz)) continue;
        const float dx = e.x - read<float>(a, 0x18), dy = e.y - read<float>(a, 0x1c), dz = e.z - read<float>(a, 0x20);
        if (dx * dx + dy * dy + dz * dz > 0.25f) {
            if (read<int>(a, 0x3c) != e.sector) { if (teleport(a, e.x, e.y, e.z, e.sector)) ++s.enemyMoves; }
            else { *reinterpret_cast<float*>(a + 0x18) = e.x; *reinterpret_cast<float*>(a + 0x1c) = e.y; *reinterpret_cast<float*>(a + 0x20) = e.z; ++s.enemyMoves; }
        }
        *reinterpret_cast<float*>(a + 0xe0) = e.vx; *reinterpret_cast<float*>(a + 0xe4) = e.vy; *reinterpret_cast<float*>(a + 0xe8) = e.vz;
        *reinterpret_cast<float*>(a + 0x38) = e.yaw;
        *reinterpret_cast<std::int16_t*>(a + 0x17a) = e.hp;
        *reinterpret_cast<int*>(a + 0x1b8) = e.aiState;
        if (e.anim != 0xffff) {
            const auto* def = read<Byte*>(a, 0x170);
            Byte* target = def ? read<Byte*>(def, e.anim) : nullptr;
            if (target && read<Byte*>(a, 0x108) != target) reinterpret_cast<std::uint64_t(__fastcall*)(void*, void*)>(s.base + ChangeAnimRva)(a, target);
        }
        ++s.enemyApplies;
    }
    if (s.ticks % 120 == 0) note("world_enemy_apply", "\"applied\":%llu,\"moved\":%llu,\"known\":%d,\"batch\":%d", s.enemyApplies, s.enemyMoves, known, count);
}

namespace {
// Снаряды врагов: тип (id в defs/defines.txt) -> указатель на дефиницию (node+8 из таблицы дефиниций 0xaf3350).
constexpr std::uint32_t DefLookupRva = 0x1bb140, SpawnActorRva = 0x1d4780, RemoveRva = 0x1a02b0, DefTableRva = 0xaf3350;
constexpr std::array<Byte, 16> DefLookupPrefix{0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x83, 0x39, 0x00, 0x48, 0x8b};
constexpr std::array<Byte, 16> SpawnActorPrefix{0x48, 0x89, 0x5c, 0x24, 0x20, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b};
constexpr std::array<Byte, 16> RemovePrefix{0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x81, 0xa1, 0x6c, 0x01, 0x00, 0x00, 0x5d, 0xfc, 0xff, 0xff};
// Только снаряды врагов/ловушек; оружие игроков (пламя, бомба Амуна, кобра) каждая сторона считает сама.
constexpr std::array<std::uint16_t, 9> ProjTypes{18, 19, 20, 23, 26, 1008, 1010, 1012, 201};
constexpr std::uint32_t ProjTag = 0x70000000u; // метка отражения в Actor+0x180 (у снарядов это поле не занято)
struct ProjDef { void* def{}; std::uint16_t type{}; };
std::array<ProjDef, ProjTypes.size()> projDefs{};
int projDefCount = -1; // -1: ещё не строили
bool projProfile() {
    static int ok = -1;
    if (ok < 0) {
        auto same = [&](std::uint32_t rva, const std::array<Byte, 16>& p) { return std::equal(p.begin(), p.end(), s.base + rva); };
        ok = same(DefLookupRva, DefLookupPrefix) && same(SpawnActorRva, SpawnActorPrefix) && same(RemoveRva, RemovePrefix);
    }
    return ok == 1;
}
void buildProjDefs() {
    if (projDefCount >= 0) return;
    projDefCount = 0;
    if (!projProfile()) { note("world_projectile_profile_mismatch", "\"ok\":false"); return; }
    void* table = read<void*>(s.base, DefTableRva);
    if (!table) { projDefCount = -1; return; }
    for (const auto type : ProjTypes) {
        const int key = type;
        auto* node = reinterpret_cast<Byte*(__fastcall*)(void*, const int*)>(s.base + DefLookupRva)(table, &key);
        if (node) projDefs[projDefCount++] = {node + 8, type};
    }
    note("world_projectile_defs", "\"found\":%d", projDefCount);
}
int projTypeOf(const Byte* a) {
    const void* def = read<void*>(a, 0x170);
    for (int i = 0; i < projDefCount; ++i) if (projDefs[i].def == def) return projDefs[i].type;
    return -1;
}
std::unordered_map<Byte*, std::uint32_t> hostProjIds; std::uint32_t nextProjId = 1;
struct Mirror { std::uint64_t lastSeen{}; };
std::unordered_map<std::uint32_t, Mirror> mirrors;  // клиент: id хоста -> когда последний раз виден
std::unordered_map<std::uint32_t, std::uint64_t> deadMirrors; // id, чьё отражение погибло само: до этого тика не ставим заново
void removeActor(Byte* a) { if (!read<Byte>(a, 0x40)) reinterpret_cast<void(__fastcall*)(void*)>(s.base + RemoveRva)(a); }
}

int collectProjectiles(Projectile* out, int max) {
    int n = 0;
    if (!thread() || !s.installed) return 0;
    buildProjDefs();
    if (projDefCount <= 0) return 0;
    std::unordered_map<Byte*, std::uint32_t> seen;
    forEachActor([&](Byte* a, int) {
        if (n >= max) return true;
        const int type = projTypeOf(a);
        if (type < 0) return false;
        const auto* node = read<Byte*>(a, 0x10);
        if (!node || (read<Byte>(a, 0x40) && read<int>(node, 0x40) < 1)) return false; // удалён
        auto it = hostProjIds.find(a);
        const std::uint32_t id = it != hostProjIds.end() ? it->second : nextProjId++;
        seen[a] = id;
        Projectile& p = out[n++];
        p = Projectile{};
        p.id = id; p.type = static_cast<std::uint16_t>(type);
        p.x = read<float>(a, 0x18); p.y = read<float>(a, 0x1c); p.z = read<float>(a, 0x20);
        p.vx = read<float>(a, 0xe0); p.vy = read<float>(a, 0xe4); p.vz = read<float>(a, 0xe8);
        p.yaw = read<float>(a, 0x38); p.sector = static_cast<std::int16_t>(read<int>(a, 0x3c));
        const int off = animOffset(a); p.anim = off >= 0 ? static_cast<std::uint16_t>(off) : 0xffff;
        return false;
    });
    hostProjIds.swap(seen);
    return n;
}

void applyProjectiles(const Projectile* in, int count) {
    if (!thread() || !s.installed || !s.client || !s.active) return;
    buildProjDefs();
    if (projDefCount <= 0) return;
    std::unordered_map<std::uint32_t, Byte*> local;
    forEachActor([&](Byte* a, int) {
        if ((static_cast<std::uint32_t>(read<int>(a, 0x180)) & 0xff000000u) == ProjTag && projTypeOf(a) >= 0) local[static_cast<std::uint32_t>(read<int>(a, 0x180)) & 0x00ffffffu] = a;
        return false;
    });
    for (int i = 0; i < count; ++i) {
        const Projectile& p = in[i];
        if (!std::isfinite(p.x + p.y + p.z + p.vx + p.vy + p.vz + p.yaw) || p.sector < 0) continue;
        mirrors[p.id].lastSeen = s.ticks;
        auto it = local.find(p.id);
        Byte* a = it != local.end() ? it->second : nullptr;
        if (!a) {
            auto dead = deadMirrors.find(p.id);
            if (dead != deadMirrors.end() && s.ticks < dead->second) continue;
            const float pos[3]{p.x, p.y, p.z};
            a = static_cast<Byte*>(reinterpret_cast<void*(__fastcall*)(void*, int, const float*, float, int)>(s.base + SpawnActorRva)(s.base + FactoryRva, p.type, pos, p.yaw, p.sector));
            if (!a) continue;
            *reinterpret_cast<int*>(a + 0x180) = static_cast<int>(ProjTag | (p.id & 0x00ffffffu));
            note("world_projectile_spawned", "\"id\":%u,\"type\":%u,\"pos\":[%.0f,%.0f,%.0f]", p.id, p.type, p.x, p.y, p.z);
        } else {
            const float dx = p.x - read<float>(a, 0x18), dy = p.y - read<float>(a, 0x1c), dz = p.z - read<float>(a, 0x20);
            if (dx * dx + dy * dy + dz * dz > 16.0f) {
                if (read<int>(a, 0x3c) != p.sector) teleport(a, p.x, p.y, p.z, p.sector);
                else { *reinterpret_cast<float*>(a + 0x18) = p.x; *reinterpret_cast<float*>(a + 0x1c) = p.y; *reinterpret_cast<float*>(a + 0x20) = p.z; }
            }
        }
        *reinterpret_cast<float*>(a + 0xe0) = p.vx; *reinterpret_cast<float*>(a + 0xe4) = p.vy; *reinterpret_cast<float*>(a + 0xe8) = p.vz;
        *reinterpret_cast<float*>(a + 0x38) = p.yaw;
        if (p.anim != 0xffff) {
            const auto* def = read<Byte*>(a, 0x170);
            Byte* target = def ? read<Byte*>(def, p.anim) : nullptr;
            if (target && read<Byte*>(a, 0x108) != target) reinterpret_cast<std::uint64_t(__fastcall*)(void*, void*)>(s.base + ChangeAnimRva)(a, target);
        }
    }
    // Хост перестал присылать снаряд (попал/взорвался): убрать отражение; погибшее само (стена) заново не ставить ~1 с.
    for (auto& [id, a] : local) {
        auto m = mirrors.find(id);
        if (m == mirrors.end() || s.ticks - m->second.lastSeen > 15) { removeActor(a); if (m != mirrors.end()) mirrors.erase(m); }
    }
    for (auto it = mirrors.begin(); it != mirrors.end();) {
        if (!local.count(it->first) && s.ticks - it->second.lastSeen > 15) it = mirrors.erase(it); else ++it;
    }
    for (auto& [id, a] : local) if (!read<Byte>(a, 0x40) && read<std::int16_t>(a, 0x17a) <= 0 && false) deadMirrors[id] = s.ticks + 60; // место под «погиб сам» (сейчас не нужно)
}

void cullLocalProjectiles() {
    if (!thread() || !s.installed || !s.client || !s.active || !replicaOn()) return;
    buildProjDefs();
    if (projDefCount <= 0) return;
    forEachActor([&](Byte* a, int) {
        if (projTypeOf(a) < 0 || (static_cast<std::uint32_t>(read<int>(a, 0x180)) & 0xff000000u) == ProjTag) return false;
        removeActor(a);
        return false;
    });
}

void debugFireball(Byte* target) {
    static bool read_env = false; static float dist = 300, speed = 500; static bool on = false; static std::uint64_t last = 0, ticks = 0;
    if (!read_env) { read_env = true; char v[48]{}; if (GetEnvironmentVariableA("PSE_DEBUG_FIREBALL", v, sizeof(v))) { on = true; sscanf_s(v, "%f,%f", &dist, &speed); } }
    if (!on || !thread() || !s.installed || !target) return;
    ++ticks;
    char file[400]{};
    if (GetEnvironmentVariableA("PSE_DEBUG_FIREBALL_FILE", file, sizeof(file)) && GetFileAttributesA(file) == INVALID_FILE_ATTRIBUTES) return;
    if (ticks - last < 90) return;
    last = ticks;
    buildProjDefs();
    if (!projProfile()) return;
    const float yaw = read<float>(target, 0x38), fx = std::sin(yaw), fy = std::cos(yaw);
    const float pos[3]{read<float>(target, 0x18) + fx * dist, read<float>(target, 0x1c) + fy * dist, read<float>(target, 0x20) + 48.0f};
    const int sector = read<int>(target, 0x3c);
    auto* a = static_cast<Byte*>(reinterpret_cast<void*(__fastcall*)(void*, int, const float*, float, int)>(s.base + SpawnActorRva)(s.base + FactoryRva, 18, pos, yaw + 3.14159265f, sector));
    if (!a) { note("world_debug_fireball", "\"ok\":false"); return; }
    *reinterpret_cast<float*>(a + 0xe0) = -fx * speed; *reinterpret_cast<float*>(a + 0xe4) = -fy * speed; *reinterpret_cast<float*>(a + 0xe8) = 0;
    note("world_debug_fireball", "\"ok\":true,\"pos\":[%.0f,%.0f,%.0f],\"speed\":%.0f", pos[0], pos[1], pos[2], speed);
}

void census(const char* why) {
    if (!thread() || !s.installed) return;
    {   // События карты (mapEvent_t, 8 байт: type, sector): двери/лифты и где они.
        const auto nev = read<std::uint64_t>(s.base, 0xa459d0);
        auto* ev = read<Byte*>(s.base, 0xa459c8);
        note("world_events_begin", "\"count\":%llu", nev);
        for (std::uint64_t i = 0; ev && i < nev && i < 2000; ++i) {
            const auto type = read<std::int16_t>(ev + i * 8), sector = read<std::int16_t>(ev + i * 8, 2);
            auto* sec = sectorAt(sector);
            if (!sec) continue;
            if (type == 1 || type == 0x15) { // где стоять у двери/лифта: соседние сектора (портальные стены) с границами и полом
                const auto first = read<std::uint16_t>(sec), last = read<std::uint16_t>(sec, 2);
                auto* faces = read<Byte*>(s.base, 0xa45980);
                int seen[8]{}; int n = 0;
                for (unsigned f = first; faces && f <= last && f - first < 64 && n < 8; ++f) {
                    const int nb = read<std::int16_t>(faces + static_cast<std::size_t>(f) * 0x108, 6);
                    bool dup = nb < 0 || nb == sector;
                    for (int k = 0; k < n; ++k) dup = dup || seen[k] == nb;
                    if (dup) continue;
                    seen[n++] = nb;
                    if (auto* b = sectorAt(nb)) {
                        const auto pr = probe(nb);
                        note("world_event_neighbor", "\"door_sector\":%d,\"sector\":%d,\"floor_d\":%.0f,\"ceiling_d\":%.0f,\"bounds\":[%.0f,%.0f,%.0f,%.0f,%.0f,%.0f]", sector, nb, pr.floorD, pr.ceilingD,
                             read<float>(b, 0x18), read<float>(b, 0x1c), read<float>(b, 0x20), read<float>(b, 0x24), read<float>(b, 0x28), read<float>(b, 0x2c));
                    }
                }
            }
            note("world_event", "\"idx\":%llu,\"type\":%d,\"sector\":%d,\"flags\":%u,\"bounds\":[%.0f,%.0f,%.0f,%.0f,%.0f,%.0f]", i, type, sector, read<std::uint16_t>(sec, 0x14),
                 read<float>(sec, 0x18), read<float>(sec, 0x1c), read<float>(sec, 0x20), read<float>(sec, 0x24), read<float>(sec, 0x28), read<float>(sec, 0x2c));
        }
    }
    unsigned n = 0;
    note("world_census_begin", "\"why\":\"%s\",\"sectors\":%llu", why, sectorCount());
    forEachActor([&](Byte* a, int sector) {
        if (++n > 900) return true;
        const auto* def = read<Byte*>(a, 0x170);
        note("world_actor", "\"i\":%u,\"vt\":\"%x\",\"sector\":%d,\"pos\":[%.1f,%.1f,%.1f],\"yaw\":%.2f,\"hp\":%d,\"flags\":\"%x\",\"f1c0\":\"%x\",\"spawn_id\":%d,\"def\":\"%p\",\"ai1d8\":\"%x\"",
             n, static_cast<unsigned>(read<Byte*>(a) - s.base), sector, read<float>(a, 0x18), read<float>(a, 0x1c), read<float>(a, 0x20), read<float>(a, 0x38),
             read<std::int16_t>(a, 0x17a), read<unsigned>(a, 0x16c), read<unsigned>(a, 0x1c0), read<int>(a, 0x180),
             static_cast<const void*>(def), read<unsigned>(a, 0x1d8));
        return false;
    });
    note("world_census_end", "\"why\":\"%s\",\"actors\":%u", why, n);
}
}
