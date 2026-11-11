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
#include <vector>
#include <intrin.h>
#include "map_reader.hpp"

namespace pse::world {
bool gatherTo(unsigned char* actor, float x, float y, float z);
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
// Подбор предмета (consume: звук, сообщение на HUD, вспышка, удаление) и общий HUD-объект движка (глобал +0xa43e20: vtable +0x18 = сообщение,
// +0x20 = вспышка при уроне, +0x28 = вспышка при подборе). HUD в игре один — тела других игроков на хосте не должны давать хосту ни рамку урона,
// ни сообщений о подборе: вызовы HUD внутри Damage/подбора ЧУЖОГО тела пропускаются (звук и логика остаются).
constexpr std::uint32_t ConsumeRva = 0x1d8360, HudObjectRva = 0xa43e20;
constexpr std::array<Byte, 16> ConsumePrefix{0x40, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xc2, 0x48, 0x8b, 0xf9, 0x48, 0x8b, 0x91, 0x70};
using SinkFn = std::uintptr_t(__fastcall*)(void*, void*, void*, void*);
constexpr std::array<Byte, 16> DamagePrefix{0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
using DamageFn = void(__fastcall*)(void*, void*, int, short);
// SetAchievement(service, key, 0): единственный путь всех 20 ключей (DOC-63). service = *(base+0xaeaf80).
constexpr std::uint32_t AchRva = 0x184110, AchServiceRva = 0xaeaf80;
constexpr std::array<Byte, 16> AchPrefix{0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x81, 0xec, 0xb0, 0x00, 0x00, 0x00, 0x80, 0xb9, 0x1c};
constexpr std::array<const char*, 20> AchKey{"ACH_GOOD_ENDING", "ACH_BAD_ENDING", "ACH_TEAM_DOLL", "ACH_ALL_TEAM_DOLLS", "ACH_SANDALS", "ACH_MASK", "ACH_SHAWL",
    "ACH_ANKLETS", "ACH_SCEPTER", "ACH_FEATHER", "ACH_DEFEAT_SET", "ACH_DEFEAT_SELKIS", "ACH_DEFEAT_QUEEN", "ACH_FINISH_HARD", "ACH_FINISH_PHARAOH",
    "ACH_FINISH_NO_ANKAHS", "ACH_WEAPON_BOOST", "ACH_KILL_ENVIRONMENT", "ACH_KILL_MANTIS_MACHETE", "ACH_ALL_ANKAHS"};
using AchFn = std::uint8_t(__fastcall*)(void*, const char*, int);
// CallDelayedMapScript(int scriptNum, kexActor* instigator, float delay): диспетчер событий карты (тип 52/63: tag+1), уровень (номер 0) и
// привязка AngelScript (цепочки) идут через неё. Корневой вызов диспетчера отличаем по адресу возврата: код игры < 0x400000 (единственные вызовы оттуда — уровень (номер 0) и диспетчер событий).
constexpr std::uint32_t ScriptRva = 0x1f4a60, ScriptRootLo = 0x1000, ScriptRootHi = 0x400000; // цепочки AngelScript возвращаются в 0x4146d5
constexpr std::array<Byte, 16> ScriptPrefix{0x4c, 0x8b, 0xdc, 0x49, 0x89, 0x5b, 0x08, 0x57, 0x48, 0x81, 0xec, 0xc0, 0x00, 0x00, 0x00, 0x65};
using ScriptFn = void(__fastcall*)(int, void*, float);
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
    bool debugTeleportOn{}; float debugTx{}, debugTy{}, debugTyaw{}, debugTz{NAN};
    int debugEnemy{-1}, debugKill{-1};
    void* ach{}; bool achDryRun{}; int debugAch{-1};
    void* script{}; bool forceRoot{}; int debugScriptNum{-1};
    bool (*remoteTest)(const void*){}; unsigned char* (*localActor)(){}; void* consume{}; int hudKind{}; unsigned hudLogged{}; bool hudTried{}; SinkFn sinkOrig[12]{};
    // Клиент: шары здоровья/патронов своего игрока берёт хост (ReqPickup). healthTouch/ammoTouch — оригиналы OnTouch; pickupAsked — номер шара ->
    // тик последнего запроса (повтор не чаще раза в 30 тиков, отклик подбора только на свой запрос).
    void* healthTouch{}; void* ammoTouch{}; std::unordered_map<int, std::uint64_t> pickupAsked;
    std::array<int, 16> askQueue{}; std::size_t askHead{}, askCount{}; unsigned askLogged{}, takeLogged{};
    // Попадания игрока клиента по врагам хоста (ReqHit): очередь на отправку; хост — флаг «урон по запросу клиента», счётчики.
    struct Hit { int id{}; std::uint16_t amount{}; std::uint8_t kind{}; };
    std::array<Hit, 64> hitQueue{}; std::size_t hitHead{}, hitCount{}; bool clientHit{};
    unsigned hitLogged{}, hitAppliedLogged{}; std::uint64_t remoteHitsMuted{}, hitsSent{}; unsigned capLogged{};
    // Урон окружения своему игроку клиента (утопление, лава, падение: Damage без источника) — решает клиент (ReqSelfDamage); хост: флаг применения.
    std::array<Hit, 32> selfQueue{}; std::size_t selfHead{}, selfCount{}; bool selfDamage{}, replicaKill{}; unsigned selfLogged{}; std::uint64_t remoteSelfMuted{};
    // Скарабеи (чекпоинты): каждый игрок — свой. Своё касание: номер скарабея карты в очередь (хост — для себя, клиент — хосту ReqCheckpoint).
    void* checkpointTouch{}; int lastCheckpoint{-1}; std::array<int, 8> cpQueue{}; std::size_t cpHead{}, cpCount{};
    // Этап 2: касания своего игрока у клиента (ReqTouch) — одна очередь для всех видов; повтор того же актора не чаще раза в 30 тиков.
    std::array<Touch, 32> touchQueue{}; std::size_t touchHead{}, touchCount{}; std::unordered_map<std::uint64_t, std::uint64_t> touchAsked;
    unsigned touchLogged{}, touchAppliedLogged{}; void* weaponTouch{};
    std::array<ScriptCall, 16> scriptQueue{}; std::size_t scriptHead{}, scriptCount{};
    std::array<int, 32> breakQueue{}; std::size_t breakHead{}, breakCount{}; std::unordered_map<int, std::uint64_t> brokenSeen; unsigned breakLogged{};
    std::array<bool, 256> sceneDone{}, sceneSubstituted{}; int debugBreak{-1}; // сюжетные сцены, уже сыгранные в этом мире (отсюда или реплицированные)
    std::array<std::uint64_t, 256> scriptTick{}; // тик последнего запуска каждого номера здесь (для защиты от двойного запуска)
    std::uint32_t achLocal{}, achSeen{}; // локальные, ещё не отправленные / уже виденные за прохождение (отправлено или получено)
    // Звуки своего игрока (см. EvSound): перехват PlaySound / зацикленного / остановки, очередь на отправку, повтор чужого звука без эха.
    void* playSound{}; void* playLoop{}; void* stopLoop{}; bool soundReplay{}; unsigned soundUnknownLogged{}, soundsSent{};
    std::array<std::uint32_t, 32> soundQueue{}; std::size_t soundHead{}, soundCount{};
    std::array<std::uint64_t, 64> soundLastTick{}; // по номеру звука: тик последней отправки (взмахи мачете и очереди М60 не забивают сеть)
    // Хост: враги карты, пропавшие из мира (EvEnemyGone), и подобранные шары карты (EvPickup).
    std::array<int, 64> goneQueue{}; std::size_t goneHead{}, goneCount{};
    std::array<int, 64> pickupQueue{}; std::size_t pickupHead{}, pickupCount{};
    std::unordered_map<int, std::uint64_t> enemySeen; // номер врага карты -> номер сбора, в котором он был в мире
    std::uint64_t enemyRound{};
    // Лифт «встань и жди» (класс Lift): пересылается, когда он здесь поехал (см. trackMovers). Двери: слежение за ходом потолка (обратный ход).
    struct PendingLift { int sector{-1}; std::uint8_t type{}; float floor{}; std::uint64_t since{}; void* mover{}; };
    std::array<PendingLift, 16> lifts{};
    struct DoorTrack { std::uint8_t type{}; float ceiling{}; bool falling{}; std::uint64_t reversed{}; };
    std::unordered_map<int, DoorTrack> doors;          // сектор -> дверь, которая сейчас движется здесь
    struct Reopen { std::uint8_t type{}; std::uint64_t deadline{}; };
    std::unordered_map<int, Reopen> reopen;            // клиент: сектор -> открыть снова, как только закроется
    // Рычаги (EvSwitch) и платформы (снимок мира, тип 4).
    void* useSwitch{}; void* remoteTrigger{}; bool switchReplay{};
    std::array<std::pair<int, int>, 32> switchQueue{}; std::size_t switchHead{}, switchCount{};
    std::vector<int> floatSectors, padSectors; bool platformsScanned{};
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

constexpr int ClassDoor = 0, ClassLift = 1, ClassLiftImmediate = 2; // индексы ClassName
// Двери «открыть — подождать — закрыть» (обычная 1, с ключами 3..6) идут обратно вверх, если закрываются на ком-то (CheckActorHeight: твёрдый
// актор под дверью). У хоста под дверью бывает сам хост и тела клиентов; у клиента другие игроки — прокси, на которых дверь не реагирует: у клиента
// дверь закрывалась, у хоста оставалась открытой ещё на цикл. Двери 2 (рычаг со сбросом), 7 (навсегда открыть), 8/9 (навсегда закрыть) — не трогаем.
bool reversibleDoor(int type) { return type == 1 || (type >= 3 && type <= 6); }
void trackDoor(int cls, int type, int sector) {
    if (cls != ClassDoor || !reversibleDoor(type)) return;
    s.doors[sector] = {static_cast<std::uint8_t>(type), probe(sector).ceilingD, false, 0};
}
void queueMover(const Mover& m) { if (s.count < QueueCap) s.movers[(s.head + s.count++) % QueueCap] = m; }
// Здесь стоит свой «ждущий» лифт (сюда вставал и сошёл свой игрок), а у другого игрока этот лифт уже поехал: его LiftImmediate здесь отвергнут
// (сектор занят) и лифты разошлись бы. Запустить свой так же, как сам движок, когда на лифте 2 с постоял игрок (0x1d212e: state +0xf0 = bDirection
// +0xd8 ? 1 : 2, задержка +0xec = 0). Только свой проверенный объект kexSlaveMoverLift (vtable 0x52bc18) этого сектора в состоянии ожидания (0).
constexpr std::uint32_t LiftVtable = 0x52bc18;
bool startIdleLift(int sector) {
    for (auto& l : s.lifts) {
        if (l.sector != sector || !l.mover) continue;
        auto* mv = static_cast<Byte*>(l.mover); auto* sec = sectorAt(sector);
        l.sector = -1;
        if (!sec || !(read<Byte>(sec, 0x14) & 4) || read<Byte*>(mv) != s.base + LiftVtable || read<Byte*>(mv, 0xc0) != sec || read<Byte>(mv, 0x40) || read<int>(mv, 0xf0) != 0) return false;
        *reinterpret_cast<int*>(mv + 0xf0) = read<Byte>(mv, 0xd8) ? 1 : 2;
        *reinterpret_cast<float*>(mv + 0xec) = 0.0f;
        note("world_lift_started_by_peer", "\"sector\":%d,\"type\":%u", sector, l.type);
        return true;
    }
    return false;
}
// Каждый тик активного мира: лифты, которые здесь поехали (уходят как LiftImmediate); двери, пошедшие обратно (хост сообщает клиентам);
// у клиента — двери, которые хост открыл снова: открыть, как только здесь закроются.
void trackMovers() {
    for (auto& l : s.lifts) {
        if (l.sector < 0) continue;
        const auto p = probe(l.sector);
        if (!p.valid || !(p.flags & 4) || s.ticks - l.since > 36000) { l.sector = -1; continue; } // мовер снят, так и не поехав (никто не остался)
        if (std::fabs(p.floorD - l.floor) <= 0.5f) continue;
        queueMover(Mover{static_cast<std::uint8_t>(ClassLiftImmediate), l.type, static_cast<std::int16_t>(l.sector)});
        note("world_lift_started", "\"sector\":%d,\"type\":%u,\"waited_ticks\":%llu,\"sent_as\":\"LiftImmediate\"", l.sector, l.type, s.ticks - l.since);
        l.sector = -1;
    }
    for (auto it = s.doors.begin(); it != s.doors.end();) {
        const auto p = probe(it->first);
        if (!p.valid || !(p.flags & 4)) { it = s.doors.erase(it); continue; } // дверь закончила цикл
        auto& d = it->second;
        if (p.ceilingD < d.ceiling - 0.5f) d.falling = true;
        else if (p.ceilingD > d.ceiling + 0.5f && d.falling) {
            d.falling = false; d.reversed = s.ticks;
            if (!s.client) queueMover(Mover{static_cast<std::uint8_t>(ClassDoor), d.type, static_cast<std::int16_t>(it->first), true});
            note("world_door_reversed", "\"sector\":%d,\"type\":%u,\"sent\":%s", it->first, d.type, s.client ? "false" : "true");
        }
        d.ceiling = p.ceilingD;
        ++it;
    }
    for (auto it = s.reopen.begin(); it != s.reopen.end();) {
        const auto d = s.doors.find(it->first);
        if (s.ticks > it->second.deadline || (d != s.doors.end() && !d->second.falling)) { it = s.reopen.erase(it); continue; } // здесь пошла вверх сама
        auto* sec = sectorAt(it->first);
        if (sec && !(read<Byte>(sec, 0x14) & 4)) {
            const Mover m{static_cast<std::uint8_t>(ClassDoor), it->second.type, static_cast<std::int16_t>(it->first)};
            it = s.reopen.erase(it);
            note("world_door_reopened", "\"sector\":%d,\"type\":%u", m.sector, m.type);
            applyMover(m);
            continue;
        }
        ++it;
    }
}
// ---- Рычаги. UseWallSwitch(face, ev, point) 0x1bd1b0: полигоны грани с текстурой «выключен» (байт ev+7) получают «включён» (ev+6) — при нулевой
// точке все, иначе только у места нажатия; если что-то сменилось, SendRemoteTrigger(sector, ev) 0x1bdd00 и true. Звук щелчка играет вызывающий.
constexpr std::uint32_t UseSwitchRva = 0x1bd1b0, RemoteTriggerRva = 0x1bdd00, FacesRva = 0xa45980, FaceCountRva = 0xa45988, EventsRva = 0xa459c8,
    EventCountRva = 0xa459d0, FaceStride = 0x108;
constexpr std::array<Byte, 16> UseSwitchPrefix{0x4c, 0x8b, 0xdc, 0x49, 0x89, 0x53, 0x10, 0x53, 0x55, 0x56, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56};
constexpr std::array<Byte, 16> RemoteTriggerPrefix{0x40, 0x53, 0x55, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x1d};
using UseSwitchFn = std::uint8_t(__fastcall*)(void*, void*, const float*);
int faceIndex(const void* face) {
    auto* faces = read<Byte*>(s.base, FacesRva); const auto n = read<std::uint64_t>(s.base, FaceCountRva);
    const auto off = static_cast<const Byte*>(face) - faces;
    return faces && face && off >= 0 && off % FaceStride == 0 && static_cast<std::uint64_t>(off / FaceStride) < n && n < 200000 ? static_cast<int>(off / FaceStride) : -1;
}
int eventIndex(const void* ev) {
    auto* evs = read<Byte*>(s.base, EventsRva); const auto n = read<std::uint64_t>(s.base, EventCountRva);
    const auto off = static_cast<const Byte*>(ev) - evs;
    return evs && ev && off >= 0 && off % 8 == 0 && static_cast<std::uint64_t>(off / 8) < n && n < 20000 ? static_cast<int>(off / 8) : -1;
}
std::uint8_t __fastcall hookUseSwitch(void* face, void* ev, const float* point) {
    const auto r = reinterpret_cast<UseSwitchFn>(s.useSwitch)(face, ev, point);
    if (r && thread() && s.installed && !s.client && !s.switchReplay) {
        const int f = faceIndex(face), e = eventIndex(ev);
        if (f >= 0 && e >= 0 && s.switchCount < s.switchQueue.size()) s.switchQueue[(s.switchHead + s.switchCount++) % s.switchQueue.size()] = {f, e};
        note("world_switch_local", "\"face\":%d,\"event\":%d", f, e);
    }
    return r;
}
void __fastcall hookRemoteTrigger(void* sector, void* ev) {
    if (s.switchReplay && thread()) return; // повтор чужого рычага: только текстура, его двери/лифты придут своими событиями
    reinterpret_cast<void(__fastcall*)(void*, void*)>(s.remoteTrigger)(sector, ev);
}
// ---- Платформы. Механизм сектора: sector+0x60 -> узел -> +0x28 объект (SetupFloatingPlatforms 0x1cb8df); объект +0x10 = тот же узел, +0xc0 = сектор.
constexpr std::uint32_t FloatingVtable = 0x52baf8, DropPadVtable = 0x52bbd0;
Byte* sectorThinker(int i, std::uint32_t vtable) {
    auto* sec = sectorAt(i);
    auto* node = sec ? read<Byte*>(sec, 0x60) : nullptr;
    auto* obj = node ? read<Byte*>(node, 0x28) : nullptr;
    if (!obj || read<Byte*>(obj) != s.base + vtable || read<Byte*>(obj, 0x10) != node || read<Byte>(obj, 0x40) || read<Byte*>(obj, 0xc0) != sec) return nullptr;
    return obj;
}
void scanPlatforms() {
    if (s.platformsScanned) return;
    s.platformsScanned = true; s.floatSectors.clear(); s.padSectors.clear();
    const auto n = sectorCount();
    for (std::uint64_t i = 0; i < n && n <= 8192; ++i) {
        if (sectorThinker(static_cast<int>(i), FloatingVtable)) s.floatSectors.push_back(static_cast<int>(i));
        if (sectorThinker(static_cast<int>(i), DropPadVtable)) s.padSectors.push_back(static_cast<int>(i));
    }
    note("world_platforms", "\"floating\":%zu,\"drop_pads\":%zu", s.floatSectors.size(), s.padSectors.size());
}
// Площадка: запуск и подъём — те же записи, что делает движок (запуск: тик площадки 0x1d271d, когда на ней стоит Puppet; подъём: TriggerEvent
// 0x1bdc56, тип 48, только из состояния 2). Состояния +0xf0: 0 ждёт, 4 отсчёт, 1 падает, 2 внизу, 3 поднимается.
void padMove(Byte* pad, int state, int heightOffset) {
    auto* linked = read<Byte*>(pad, 0xe8); auto* face = linked ? read<Byte*>(linked, 0x70) : nullptr; auto* sec = read<Byte*>(pad, 0xc0);
    if (!face || !sec) return;
    const float base = read<float>(face, 0x18);
    *reinterpret_cast<float*>(pad + 0xd0) = base; *reinterpret_cast<float*>(pad + 0xd8) = base;
    *reinterpret_cast<std::uint64_t*>(pad + 0xdc) = 0; // задержка +0xdc и ускорение +0xe0
    *reinterpret_cast<float*>(pad + 0xd4) = static_cast<float>(read<std::int16_t>(linked, heightOffset));
    *reinterpret_cast<int*>(pad + 0xf0) = state;
    *reinterpret_cast<std::uint16_t*>(sec + 0x14) = static_cast<std::uint16_t>(read<std::uint16_t>(sec, 0x14) | 4);
}
void* __fastcall hookSpawn(void* self, const char* name, int type, int sector) {
    void* result = s.spawn(self, name, type, sector);
    if (!thread() || !s.installed) return result;
    const int cls = result && name ? classIndex(name) : -1;
    if (s.active && !s.replaying && cls >= 0 && type >= 0 && type <= 255 && sector >= 0 && sector <= 32767) {
        ++s.localMovers;
        watchSector(sector);
        trackDoor(cls, type, sector);
        if (cls == ClassLift) {
            // «Встань и жди»: лифт трогается, только когда на нём 2 с стоит Puppet. У получателя другой игрок — прокси, не Puppet, и такой лифт там
            // стоял бы вечно. Поэтому он уходит, когда здесь поехал, и как LiftImmediate (тот же тип, едет сразу) — см. trackMovers.
            for (auto& l : s.lifts) if (l.sector < 0 || l.sector == sector) { l = {sector, static_cast<std::uint8_t>(type), probe(sector).floorD, s.ticks, result}; break; }
            note("world_mover_local", "\"class\":\"%s\",\"type\":%d,\"sector\":%d,\"deferred\":\"until_it_moves\"", ClassName[cls], type, sector);
            return result;
        }
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
// Метка отражения порождённого актора хоста в Actor+0x180 (шары, враги из ваз): 0x60000000 | номер хоста (см. applyDynamic).
constexpr std::uint32_t SphereMirrorTag = 0x60000000u;
// Номер врага на проводе: номер актора карты (+0x180) или метка отражения порождённого врага хоста; -1 — враг, которого у хоста нет.
int enemyWireId(const void* a) {
    const int id = read<int>(a, 0x180);
    if (id >= 0 && id < 100000) return id;
    if ((static_cast<std::uint32_t>(id) & 0xff000000u) == SphereMirrorTag) return id;
    return -1;
}
// Хост: урон, нанесённый телом игрока-клиента (Puppet) или его снарядом (владелец снаряда — цель +0x28: узел -> актор, как в 2015 SetTarget).
bool remoteOwned(void* instigator) {
    if (!instigator || !s.remoteTest) return false;
    if (s.remoteTest(instigator)) return true;
    __try {
        if (read<const Byte*>(instigator) == s.base + PuppetVtable) return false;
        auto* node = read<Byte*>(instigator, 0x28);
        auto* owner = node ? read<Byte*>(node, 0x28) : nullptr;
        return owner && read<Byte*>(owner, 0x10) == node && s.remoteTest(owner);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
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
    // Враги у клиента — отражения хоста. Своё попадание игрок клиента по ним у себя не наносит: локальный урон убивал врага у клиента
    // (Damage 0x1a1370 при здоровье <= 0 сам ставит анимацию смерти, снимает «простреливаемый» 0x20 и зовёт OnDeath), а у хоста тело этого
    // игрока стреляет с задержкой и часто мимо — враг оставался живым у хоста. Попадание уходит хосту (ReqHit), здесь — только вспышка удара.
    if (thread() && s.client && s.installed && s.active && !s.hostDamage && !s.replicaKill && actor && read<Byte*>(actor) == s.base + AiVtable && replicaOn()) {
        const int id = enemyWireId(actor);
        if (id >= 0) {
            if ((read<unsigned>(actor, 0x16c) & 0x20u) && read<std::int16_t>(actor, 0x17a) > 0 && amount > 0) {
                *reinterpret_cast<unsigned*>(static_cast<Byte*>(actor) + 0x16c) |= 0x10u; *reinterpret_cast<int*>(static_cast<Byte*>(actor) + 0x134) = 1;
                if (s.hitCount < s.hitQueue.size()) s.hitQueue[(s.hitHead + s.hitCount++) % s.hitQueue.size()] = {id, static_cast<std::uint16_t>(amount), static_cast<std::uint8_t>(kind & 0xff)};
                if (++s.hitLogged <= 30) note("world_hit_local", "\"id\":%d,\"amount\":%d,\"kind\":%d,\"hp\":%d", id, static_cast<int>(amount), kind, static_cast<int>(read<std::int16_t>(actor, 0x17a)));
            }
            return;
        }
    }
    // Хост: враги получают урон игрока-клиента только по его сообщению (applyHit). Своё тело этого игрока здесь стреляет по позе клиента с
    // задержкой: его попадания (и его снарядов) по врагам не считаются, иначе урон был бы двойным и не таким, как у клиента.
    if (thread() && s.installed && !s.client && s.active && !s.clientHit && actor && read<Byte*>(actor) == s.base + AiVtable && remoteOwned(instigator)) {
        if (++s.remoteHitsMuted % 60 == 1) note("world_remote_hit_muted", "\"kind\":%d,\"amount\":%d,\"total\":%llu", kind, static_cast<int>(amount), s.remoteHitsMuted);
        return;
    }
    // Дружественный огонь выключен по умолчанию: игрок (Puppet) не ранит другого игрока. Урон себе (взрыв, ловушка) остаётся.
    if (thread() && s.installed && !s.friendlyFire && instigator && instigator != actor && isPuppet(actor) && isPuppet(instigator)) {
        if (++s.friendlySuppressed % 30 == 1) note("world_friendly_fire_blocked", "\"kind\":%d,\"amount\":%d,\"total\":%llu", kind, static_cast<int>(amount), s.friendlySuppressed);
        return;
    }
    if (thread() && s.client && s.ownActor && actor == s.ownActor && !s.hostDamage) {
        // Урон без источника (утопление, лава/слизь, падение, раздавило) — от мира вокруг СВОЕГО игрока, а тело этого игрока у хоста ставится в его
        // позу каждый тик и само не тонет (воздух у друга был бесконечным, а после обрыва связи накопленный урон убил его разом). Его считает
        // клиент: урон уходит хосту, тот наносит его телу игрока; здоровье, как всегда, приходит от хоста.
        if (!instigator && amount > 0 && s.active && s.selfCount < s.selfQueue.size()) {
            s.selfQueue[(s.selfHead + s.selfCount++) % s.selfQueue.size()] = {0, static_cast<std::uint16_t>(amount), static_cast<std::uint8_t>(kind & 0xff)};
            if (++s.selfLogged <= 30) note("world_self_damage", "\"amount\":%d,\"kind\":%d,\"hp\":%d", static_cast<int>(amount), kind, static_cast<int>(read<std::int16_t>(actor, 0x17a)));
        }
        if (++s.suppressedDamage % 60 == 1) note("world_own_damage_suppressed", "\"kind\":%d,\"amount\":%d,\"total\":%llu", kind, static_cast<int>(amount), s.suppressedDamage);
        return;
    }
    if (thread() && !s.client && s.active && !s.selfDamage && !instigator && s.remoteTest && s.remoteTest(actor)) { // its player reports it (ReqSelfDamage)
        if (++s.remoteSelfMuted % 60 == 1) note("world_remote_self_damage_muted", "\"kind\":%d,\"amount\":%d,\"total\":%llu", kind, static_cast<int>(amount), s.remoteSelfMuted);
        return;
    }
    if (thread() && s.remoteTest && s.remoteTest(actor)) { // тело другого игрока на хосте: боль не рисуется на HUD хоста
        const int before = s.hudKind; s.hudKind = 1;
        reinterpret_cast<DamageFn>(s.damage)(actor, instigator, kind, amount);
        s.hudKind = before;
        return;
    }
    if (thread() && s.installed && s.active && !s.replaying && actor && !isPuppet(actor) && read<Byte*>(actor) != s.base + AiVtable) {
        // Разрушаемый предмет (не игрок и не враг): если этим ударом он погиб, номер актора карты уходит остальным.
        const auto before = read<std::int16_t>(actor, 0x17a);
        reinterpret_cast<DamageFn>(s.damage)(actor, instigator, kind, amount);
        const int id = read<int>(actor, 0x180);
        if (before > 0 && read<std::int16_t>(actor, 0x17a) <= 0 && id >= 0 && id < 100000 && !s.brokenSeen.count(id) && s.breakCount < s.breakQueue.size()) {
            s.brokenSeen[id] = s.ticks;
            s.breakQueue[(s.breakHead + s.breakCount++) % s.breakQueue.size()] = id;
            if (++s.breakLogged <= 30) note("world_break_local", "\"id\":%d,\"vt_rva\":%llu,\"flags\":%u,\"kind\":%d,\"amount\":%d", id, static_cast<unsigned long long>(read<Byte*>(actor) - s.base), read<unsigned>(actor, 0x16c), kind, static_cast<int>(amount));
        }
        return;
    }
    reinterpret_cast<DamageFn>(s.damage)(actor, instigator, kind, amount);
}
// Шары здоровья и патронов (kexSlavePickupHealth / kexSlavePickupAmmo): их подбор у хоста убирает шар у всех. Оружие, артефакты, куклы, ключи,
// анкхи не трогаем: оружие остаётся в мире для каждого, ключи и анкхи общие своими путями, артефакты каждый игрок берёт сам.
constexpr std::uint32_t HealthPickupVtable = 0x52c868, AmmoPickupVtable = 0x52cac8;
bool sphere(const void* a) { const auto* vt = read<const Byte*>(a); return vt == s.base + HealthPickupVtable || vt == s.base + AmmoPickupVtable; }
// Номер шара на проводе: номер актора карты (+0x180) или метка отражения порождённого шара хоста (SphereMirrorTag | номер хоста, см. applyDynamic).
int sphereWireId(const void* a) {
    const int id = read<int>(a, 0x180);
    if (id >= 0 && id < 100000) return id;
    if ((static_cast<std::uint32_t>(id) & 0xff000000u) == SphereMirrorTag) return id;
    return -1;
}
// OnTouch шара здоровья (0x1d8770) и патронов (0x1d8590). У хоста на них стоят хуки P14 (кому из игроков отдать); у клиента — эти.
// Раньше клиент брал шар у себя (здоровье, вспышка, шар исчез), а у хоста тело клиента касалось шара само — только если его поставили ровно на шар:
// часто не касалось, хост возвращал клиенту прежнее здоровье нативным уроном (лечение -> красная вспышка), шар оставался у хоста; иногда шар у
// хоста брал другой игрок. Теперь касание своего игрока у клиента только просит хост (ReqPickup); хост отдаёт шар телу этого игрока нативным
// OnTouch, убирает его у всех (EvPickup), а взявший клиент показывает подбор (звук, надпись, вспышки) по EvPickup на свой запрос.
constexpr std::uint32_t HealthTouchRva = 0x1d8770, AmmoTouchRva = 0x1d8590;
} // namespace (temporarily: healthCap is part of the public API)
// Потолок здоровья игрока, как в GiveHealth 0x1e0850: (анкхи + 1) * (100 на сложностях 1-2, иначе 200).
int healthCap(const unsigned char* player) {
    const int d = difficulty();
    const int ankhs = player ? std::clamp<int>(read<std::int16_t>(player, 0x84), 0, 8) : 0;
    return (ankhs + 1) * ((d == 1 || d == 2) ? 100 : 200);
}
namespace {
constexpr std::array<Byte, 16> HealthTouchPrefix{0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x83, 0xb9, 0x70, 0x01, 0x00};
constexpr std::array<Byte, 16> AmmoTouchPrefix{0x40, 0x56, 0x41, 0x56, 0x48, 0x83, 0xec, 0x28, 0x48, 0x83, 0xb9, 0x70, 0x01, 0x00, 0x00, 0x00};
// Касание своего игрока у клиента -> одна очередь ReqTouch (повтор того же актора того же вида не чаще раза в 30 тиков).
void pushTouch(TouchKind kind, int id) {
    if (id < 0) return;
    const std::uint64_t key = (static_cast<std::uint64_t>(kind) << 32) | static_cast<std::uint32_t>(id);
    auto& last = s.touchAsked[key];
    if (last && s.ticks - last < 30) return;
    last = s.ticks ? s.ticks : 1;
    if (s.touchCount < s.touchQueue.size()) s.touchQueue[(s.touchHead + s.touchCount++) % s.touchQueue.size()] = Touch{id, kind};
    if (++s.touchLogged <= 60) note("world_touch_asked", "\"kind\":%u,\"id\":%d", static_cast<unsigned>(kind), id);
}
template <unsigned Kind> void __fastcall hookSphereTouch(void* pickup, void* toucher) {
    auto* original = reinterpret_cast<TouchFn>(Kind == 0 ? s.healthTouch : s.ammoTouch);
    if (!original) return; // only if one of the two hooks failed to install
    if (thread() && s.installed && s.client && s.active && toucher && toucher == s.ownActor && pickup && sphere(pickup)) {
        if (read<Byte>(pickup, 0x40) || read<std::int16_t>(toucher, 0x17a) <= 0) return;
        const int id = sphereWireId(pickup);
        if (id < 0) return; // шар, которого у хоста нет (свой случайный): не берётся вовсе — хост всё равно вернул бы прежнее здоровье
        auto& last = s.pickupAsked[id];
        if (last && s.ticks - last < 30) return;
        last = s.ticks ? s.ticks : 1;
        pushTouch(Kind == 0 ? TouchHealth : TouchAmmo, id);
        if (++s.askLogged <= 40) note("world_pickup_asked", "\"id\":%d,\"kind\":%u", id, Kind);
        return;
    }
    original(pickup, toucher);
}
void __fastcall hookConsume(void* pickup, void* toucher) {
    const int id = thread() && s.installed && pickup && sphere(pickup) ? read<int>(pickup, 0x180) : -1; // also with no client yet: a later joiner learns it
    if (thread() && s.remoteTest && s.remoteTest(toucher)) {
        const int before = s.hudKind; s.hudKind = 2;
        reinterpret_cast<void(__fastcall*)(void*, void*)>(s.consume)(pickup, toucher);
        s.hudKind = before;
    } else reinterpret_cast<void(__fastcall*)(void*, void*)>(s.consume)(pickup, toucher);
    if (id >= 0 && id < 100000 && s.pickupCount < s.pickupQueue.size()) s.pickupQueue[(s.pickupHead + s.pickupCount++) % s.pickupQueue.size()] = id;
}
// ---- Звуки игрока (EvSound / ReqSound). Номер звука на проводе — индекс в этой таблице: все звуки оружия из sprites/weapons/*_anim.txt
// (A_PlayLocalSound / A_PlayLoopingLocalSound) и звуки тела игрока из EXE (прыжок, боль, смерть, вода, удар током). Порядок не менять: он общий
// для всех машин одной версии мода.
constexpr std::array<const char*, 49> PlayerSounds{
    "sounds/machfire.wav", "sounds/machflesh.wav", "sounds/machhit.wav", "sounds/machup.wav", "sounds/machdown.wav",
    "sounds/pistolfire.wav", "sounds/m90fire.wav", "sounds/m90up.wav", "sounds/m90down.wav", "sounds/bombthrow.wav",
    "sounds/fthrowfire.wav", "sounds/fthrowup.wav", "sounds/fthrowdown.wav", "sounds/cstafffire.wav", "sounds/cstaffup.wav",
    "sounds/rafire.wav", "sounds/braceletfire.wav", "sounds/braceletup.wav", "sounds/braceletdown.wav", "sounds/blasted2.wav", "sounds/shockold.wav",
    "sounds/pjump01.wav", "sounds/pjump02.wav", "sounds/pjump03.wav", "sounds/pjump04.wav", "sounds/pjump05.wav", "sounds/fly.wav",
    "sounds/ppain01.wav", "sounds/ppain02.wav", "sounds/ppain03.wav", "sounds/pwpain01.wav", "sounds/pwpain02.wav", "sounds/pwpain03.wav",
    "sounds/pwpain04.wav", "sounds/pwpain05.wav", "sounds/pdeath01.wav", "sounds/pdeath02.wav", "sounds/pdeath03.wav", "sounds/pxdeath.wav",
    "sounds/pdrown.wav", "sounds/pshock.wav", "sounds/psizzle.wav", "sounds/pelectrocute.wav", "sounds/splash01.wav", "sounds/splash02.wav",
    "sounds/pwgasp.wav", "sounds/pwbreathe.wav", "sounds/dolphin01.wav", "sounds/dolphin02.wav"};
static_assert(PlayerSounds.size() <= 64, "sound index fits the per-sound rate table and the low byte of the wire value");
// PlaySound(actor, path, flag) 0x1d3b30 (A_PlayLocalSound, боль, прыжок ...), PlayLoopingSound(actor, path) 0x1d36b0 (A_PlayLoopingLocalSound,
// огнемёт), StopLoopingSounds(actor) 0x1d3930 (A_StopLoopingSounds).
constexpr std::uint32_t PlaySoundRva = 0x1d3b30, PlayLoopRva = 0x1d36b0, StopLoopRva = 0x1d3930, SoundFlag = 0x400;
constexpr std::array<Byte, 16> PlaySoundPrefix{0x40, 0x53, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b, 0xf1, 0x45, 0x0f, 0xb6, 0xf8};
constexpr std::array<Byte, 16> PlayLoopPrefix{0x48, 0x89, 0x5c, 0x24, 0x10, 0x55, 0x56, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b, 0xf1, 0x33};
constexpr std::array<Byte, 16> StopLoopPrefix{0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xf9, 0x33, 0xdb, 0x48};
using PlaySoundFn = void(__fastcall*)(void*, const char*, bool);
using PlayLoopFn = void(__fastcall*)(void*, const char*);
int soundIndex(const char* path) {
    if (!path) return -1;
    __try { for (std::size_t i = 0; i < PlayerSounds.size(); ++i) if (_stricmp(path, PlayerSounds[i]) == 0) return static_cast<int>(i); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return -1;
}
Byte* ownSoundActor() { return !s.active ? nullptr : s.client ? s.ownActor : s.localActor ? s.localActor() : nullptr; }
void queueSound(const char* path, std::uint32_t flags) {
    const int idx = flags & SoundStop ? 0 : soundIndex(path);
    if (idx < 0) { if (++s.soundUnknownLogged <= 20) note("world_sound_unknown", "\"path\":\"%.60s\"", path ? path : ""); return; }
    if (!(flags & SoundStop)) {
        if (s.soundLastTick[idx] && s.ticks - s.soundLastTick[idx] < 2) return; // the same sound twice within a tick or two: once
        s.soundLastTick[idx] = s.ticks;
    }
    if (s.soundCount < s.soundQueue.size()) s.soundQueue[(s.soundHead + s.soundCount++) % s.soundQueue.size()] = static_cast<std::uint32_t>(idx) | flags;
}
// Звук на теле клиента у хоста: известный звук игрока не играется (его пришлёт сам клиент со своей машины, где прыжок, оружие и боль и случились).
bool mutedBody(void* actor, const char* path) { return !s.client && s.remoteTest && s.remoteTest(actor) && soundIndex(path) >= 0; }
void __fastcall hookPlaySound(void* actor, const char* path, bool flag) {
    if (thread() && s.installed && !s.soundReplay && actor) {
        if (actor == ownSoundActor()) queueSound(path, flag ? SoundFlag : 0);
        else if (mutedBody(actor, path)) return;
    }
    reinterpret_cast<PlaySoundFn>(s.playSound)(actor, path, flag);
}
void __fastcall hookPlayLoop(void* actor, const char* path) {
    if (thread() && s.installed && !s.soundReplay && actor) {
        if (actor == ownSoundActor()) queueSound(path, SoundLoop);
        else if (mutedBody(actor, path)) return;
    }
    reinterpret_cast<PlayLoopFn>(s.playLoop)(actor, path);
}
void __fastcall hookStopLoop(void* actor) {
    if (thread() && s.installed && !s.soundReplay && actor && actor == ownSoundActor()) queueSound(nullptr, SoundStop);
    reinterpret_cast<void(__fastcall*)(void*)>(s.stopLoop)(actor);
}
template<unsigned K> std::uintptr_t __fastcall hudSink(void* a, void* b, void* c, void* d) {
    if (s.hudKind && thread()) {
        // K: 3 сообщение, 4 рамка урона, 5 вспышка подбора, 6 вспышка полосы здоровья (шар здоровья, анкх), 7 рамка урона под водой, 8 вспышка
        // телепорта. Чужое тело на хосте берёт шар (2): ни надписи, ни вспышек; получает урон (1): ни рамок; любое чужое действие (3): ничего.
        const bool skip = s.hudKind == 3 || (s.hudKind == 2 && (K == 3 || K == 5 || K == 6)) || (s.hudKind == 1 && (K == 4 || K == 7));
        if (++s.hudLogged <= 40) note("world_hud_sink_remote_scope", "\"vslot\":%u,\"kind\":%d,\"skipped\":%s", K, s.hudKind, skip ? "true" : "false");
        if (skip) return 0;
    }
    return s.sinkOrig[K](a, b, c, d);
}
template<unsigned K> void hookHudSink(Byte* vtable) {
    auto* target = *reinterpret_cast<Byte**>(vtable + K * 8);
    const auto rva = static_cast<std::uint64_t>(target - s.base);
    if (rva < 0x1000 || rva >= 0x482000) { note("world_hud_sink_skipped", "\"vslot\":%u,\"reason\":\"outside_text\"", K); return; }
    // Queued: all six are switched on by ONE MH_ApplyQueued (each MinHook enable suspends every thread of the game: six in a row stalled the host's
    // tick ~0.2 s at the first join).
    if (MH_CreateHook(target, reinterpret_cast<void*>(&hudSink<K>), reinterpret_cast<void**>(&s.sinkOrig[K])) != MH_OK || MH_QueueEnableHook(target) != MH_OK) {
        note("world_hud_sink_skipped", "\"vslot\":%u,\"reason\":\"hook\",\"rva\":%llu", K, rva); return;
    }
    char hex[40]{}; for (int i = 0; i < 12; ++i) sprintf_s(hex + 2 * i, 3, "%02x", target[i]);
    note("world_hud_sink_ready", "\"vslot\":%u,\"rva\":%llu,\"bytes\":\"%s\"", K, rva, hex);
}
// Хост, один раз, на Sim-потоке, когда игра уже идёт (глобал HUD к этому времени точно построен).
void installHudSinks() {
    if (s.hudTried) return;
    auto* vtable = *reinterpret_cast<Byte**>(s.base + HudObjectRva);
    const auto vrva = static_cast<std::uint64_t>(vtable - s.base);
    if (!vtable || vrva < 0x482000 || vrva >= 0x59f000) { // the HUD is not built yet: try again on a later tick
        static unsigned waits = 0; if (++waits == 1 || waits % 600 == 0) note("world_hud_sink_skipped", "\"reason\":\"vtable\",\"rva\":%llu,\"waits\":%u", vrva, waits);
        return;
    }
    s.hudTried = true;
    hookHudSink<3>(vtable); hookHudSink<4>(vtable); hookHudSink<5>(vtable);
    // vtable +0x30/+0x38/+0x40 (kexSlaveHudLocal 0x52b1f0: 0x1c16d0/0x1c16e0/0x1c16f0 — только таймеры вспышек +0xa4/+0xa8/+0xac): полоса здоровья
    // мигала у хоста, когда шар здоровья брало тело друга (OnTouch 0x1d8770 зовёт +0x30 до consume), и рамка урона под водой (0x1dbd00, +0x38).
    hookHudSink<6>(vtable); hookHudSink<7>(vtable); hookHudSink<8>(vtable);
    if (MH_ApplyQueued() != MH_OK) note("world_hud_sink_skipped", "\"reason\":\"apply_queued\"");
}

int achOrdinal(const char* key) {
    if (!key) return -1;
    for (std::size_t i = 0; i < AchKey.size(); ++i) if (std::strcmp(key, AchKey[i]) == 0) return static_cast<int>(i);
    return -1;
}
// Игра зовёт SetAchievement на этой машине: свой Steam получает ключ как обычно, а ординал уходит второй стороне (один раз за ключ).
// PSE_ACH_DRYRUN=1 (смоук/разработка): нативный вызов не делается совсем, достижения пользователя не трогаются.
std::uint8_t __fastcall hookAch(void* service, const char* key, int flag) {
    const int ord = thread() && s.installed ? achOrdinal(key) : -1;
    if (ord >= 0 && !s.replaying && s.active && !(s.achSeen >> ord & 1)) {
        s.achSeen |= 1u << ord; s.achLocal |= 1u << ord;
        note("ach_local", "\"key\":\"%s\",\"ordinal\":%d,\"dry_run\":%s", key, ord, s.achDryRun ? "true" : "false");
    }
    if (s.achDryRun && ord >= 0) return 1;
    return reinterpret_cast<AchFn>(s.ach)(service, key, flag);
}

// Повтор чужого события (мовер, ключи, скрипт, разбитая ваза) — не действие этого игрока: его HUD-надписи и вспышки не показываются.
struct ForeignScope { int before; ForeignScope() : before(s.hudKind) { s.hudKind = 3; } ~ForeignScope() { s.hudKind = before; } };
// Сюжетные сцены, где скрипт двигает инициатора (instigator.GetOrigin().Lerp): TOMB и TOMBEND, скрипт 1 (Рамзес). Остальные скрипты игроков не двигают.
bool isScene(int num) {
    if (num != 1) return false;
    const auto o = map56::observe(s.base);
    if (!o.nameReadable || o.nameLength == 0) return false;
    const char* n = o.name.data();
    return _strnicmp(n, "maps/TOMB.MAP", 14) == 0 || _strnicmp(n, "maps/TOMBEND.MAP", 17) == 0;
}
void __fastcall hookScript(int num, void* instigator, float delay) {
    if (thread() && s.installed) {
        const auto ret = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - reinterpret_cast<std::uintptr_t>(s.base));
        const bool root = s.forceRoot || (ret >= ScriptRootLo && ret < ScriptRootHi);
        // Host: a story scene started by another player's BODY (it stands in the trigger) is played by the host's own player: scripts move/lock only
        // their instigator, so with the body as instigator the host's screen would not take part. The host player is gathered to the body, the
        // instigator becomes the host player (the client of that body plays the scene with its own player: its own trigger fires there).
        if (!s.client && !s.replaying && num == 1 && instigator && s.remoteTest && s.localActor && s.remoteTest(instigator) && !s.sceneSubstituted[num] && !s.sceneDone[num] && isScene(num)) {
            s.sceneSubstituted[num] = true; // once: the body stands in the trigger and the engine calls the script every tick
            if (Byte* p1 = s.localActor()) {
                const bool ok = gatherTo(p1, read<float>(instigator, 0x18), read<float>(instigator, 0x1c), read<float>(instigator, 0x20));
                note("script_instigator_host", "\"num\":%d,\"gathered\":%s", num, ok ? "true" : "false");
                if (ok) instigator = p1;
            }
        }
        const bool puppet = isPuppet(instigator);
        if (num > 0 && num < 256) {
            // Триггер-сектор, в котором игрок стоит, зовёт скрипт каждый тик (сам скрипт отсекает повтор флагом): по сети — один раз,
            // пока вызовы не прекратятся на 3 с.
            bool fresh = !s.scriptTick[num] || s.ticks - s.scriptTick[num] >= 180;
            if (root && puppet) s.scriptTick[num] = s.ticks;
            if (fresh && isScene(num) && !s.replaying) { if (s.sceneDone[num]) fresh = false; else if (root && puppet && s.active) s.sceneDone[num] = true; } // сцена идёт по сети один раз за мир
            if (root && puppet && fresh && !s.replaying && s.active && s.scriptCount < s.scriptQueue.size()) {
                ScriptCall c; c.num = num;
                c.x = read<float>(instigator, 0x18); c.y = read<float>(instigator, 0x1c); c.z = read<float>(instigator, 0x20); c.pos = std::isfinite(c.x + c.y + c.z) && std::fabs(c.x) < 32000 && std::fabs(c.y) < 32000 && std::fabs(c.z) < 32000;
                s.scriptQueue[(s.scriptHead + s.scriptCount++) % s.scriptQueue.size()] = c;
            }
            if (!root || !puppet || fresh) note("script_call", "\"num\":%d,\"ret_rva\":%u,\"root\":%s,\"puppet\":%s,\"replay\":%s,\"delay\":%.2f", num, ret, root ? "true" : "false", puppet ? "true" : "false", s.replaying ? "true" : "false", delay);
        }
    }
    reinterpret_cast<ScriptFn>(s.script)(num, instigator, delay);
}

void __fastcall hookKeyTouch(void* key, void* instigator) {
    Byte* owner = nullptr;
    if (thread() && s.installed && instigator && read<Byte*>(instigator) == s.base + PuppetVtable) owner = read<Byte*>(instigator, 0x1b8);
    const std::uint16_t before = owner ? read<std::uint16_t>(owner, 0x98) : 0;
    if (s.client && s.active && key && instigator == s.ownActor && thread()) pushTouch(TouchKey, read<int>(key, 0x180)); // the host's body takes it too
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

// Скарабей (kexSlaveCheckpoint, vtable 0x5288f0): OnTouch 0x1a3b50 проверяет g_enableCheckpoints (GameInstance vtable +0x140), живой Puppet и зовёт
// MarkCheckpoint (vtable +0x118 = 0x1b5260): одна точка на всю игру в GameInstance +0x280 (номер скарабея карты) .. +0x298, а «Reload Last Checkpoint»
// (+0x120 = 0x1b5390) перезагружает весь уровень — для кооператива не годится. Каждый игрок отмечает свой скарабей: своё касание проходит родным путём
// (надпись, анимация), номер скарабея уходит в очередь; тело клиента у хоста скарабеев не касается — свой скарабей сообщает сам клиент.
constexpr std::uint32_t CheckpointTouchRva = 0x1a3b50, CheckpointVtable = 0x5288f0;
constexpr std::array<Byte, 16> CheckpointTouchPrefix{0x48, 0x89, 0x5c, 0x24, 0x20, 0x56, 0x48, 0x81, 0xec, 0xa0, 0x00, 0x00, 0x00, 0x48, 0x83, 0xb9};
void __fastcall hookCheckpointTouch(void* cp, void* toucher) {
    auto* original = reinterpret_cast<TouchFn>(s.checkpointTouch);
    if (!original) return;
    if (thread() && s.installed && s.active && cp && toucher) {
        if (!s.client && s.remoteTest && s.remoteTest(toucher)) return;
        const bool mine = s.client ? toucher == s.ownActor : (s.localActor && toucher == s.localActor());
        original(cp, toucher);
        if (mine) {
            const int id = read<int>(s.base + 0xaf33a0, 0x280), own = read<int>(cp, 0x180);
            if (id >= 0 && id == own && id != s.lastCheckpoint && (s.client || s.cpCount < s.cpQueue.size())) {
                s.lastCheckpoint = id;
                if (s.client) pushTouch(TouchScarab, id); else s.cpQueue[(s.cpHead + s.cpCount++) % s.cpQueue.size()] = id;
                note("world_checkpoint_local", "\"id\":%d,\"pos\":[%.0f,%.0f,%.0f]", id, read<float>(cp, 0x18), read<float>(cp, 0x1c), read<float>(cp, 0x20));
            }
        }
        return;
    }
    original(cp, toucher);
}
// Оружие (kexSlavePickupWeapon, vtable 0x52cb60): OnTouch 0x1d8470. У хоста на нём хук P16 (каждый игрок получает оружие, оно остаётся в мире);
// у клиента — этот: своё касание проходит родным путём (оружие сразу в руках) и уходит хосту (ReqTouch), тело этого игрока получает его там.
constexpr std::uint32_t WeaponTouchRva = 0x1d8470, WeaponVtable = 0x52cb60;
constexpr std::array<Byte, 16> WeaponTouchPrefix{0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x83, 0xb9, 0x70, 0x01, 0x00};
void __fastcall hookWeaponTouch(void* weapon, void* toucher) {
    auto* original = reinterpret_cast<TouchFn>(s.weaponTouch);
    if (!original) return;
    if (thread() && s.installed && s.client && s.active && weapon && toucher && toucher == s.ownActor) pushTouch(TouchWeapon, read<int>(weapon, 0x180));
    original(weapon, toucher);
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
    {   // Достижения: хук нужен всегда; без точного совпадения профиля мод остаётся рабочим, но достижения не общие.
        char v[8]{};
        s.achDryRun = GetEnvironmentVariableA("PSE_ACH_DRYRUN", v, sizeof(v)) && v[0] == '1';
        if (GetEnvironmentVariableA(host ? "PSE_DEBUG_ACH" : "PSE_DEBUG_ACH_CLIENT", v, sizeof(v))) s.debugAch = std::atoi(v);
        if (!std::equal(AchPrefix.begin(), AchPrefix.end(), base + AchRva)) note("world_profile_mismatch", "\"site\":\"achievement\"");
        else if (MH_CreateHook(base + AchRva, reinterpret_cast<void*>(&hookAch), &s.ach) != MH_OK || MH_EnableHook(base + AchRva) != MH_OK) note("world_hook_failed", "\"site\":\"Achievement\"");
        else note("ach_hook_ready", "\"rva\":%u,\"dry_run\":%s", AchRva, s.achDryRun ? "true" : "false");
    }
    {   // Сюжетные сцены уровня (скрипты карты): без точного профиля мод работает, но сцены не общие.
        if (!std::equal(ScriptPrefix.begin(), ScriptPrefix.end(), base + ScriptRva)) note("world_profile_mismatch", "\"site\":\"script\"");
        else if (MH_CreateHook(base + ScriptRva, reinterpret_cast<void*>(&hookScript), &s.script) != MH_OK || MH_EnableHook(base + ScriptRva) != MH_OK) note("world_hook_failed", "\"site\":\"MapScript\"");
        else note("script_hook_ready", "\"rva\":%u", ScriptRva);
        char v[8]{};
        if (GetEnvironmentVariableA(host ? "PSE_DEBUG_SCRIPT" : "PSE_DEBUG_SCRIPT_CLIENT", v, sizeof(v))) s.debugScriptNum = std::atoi(v);
        if (GetEnvironmentVariableA(host ? "PSE_DEBUG_BREAK" : "PSE_DEBUG_BREAK_CLIENT", v, sizeof(v))) s.debugBreak = std::atoi(v); // test: break the map actor with this number at world tick 350
    }
    {   // Урон: клиент глушит локальный урон своему игроку, обе стороны — дружественный огонь (по умолчанию выключен, PSE_FRIENDLY_FIRE=1 включает).
        char v[8]{};
        s.friendlyFire = GetEnvironmentVariableA("PSE_FRIENDLY_FIRE", v, sizeof(v)) && v[0] == '1';
        if (!std::equal(DamagePrefix.begin(), DamagePrefix.end(), base + DamageRva)) { note("world_profile_mismatch", "\"site\":\"damage\""); return false; }
        if (MH_CreateHook(base + DamageRva, reinterpret_cast<void*>(&hookDamage), &s.damage) != MH_OK || MH_EnableHook(base + DamageRva) != MH_OK) {
            note("world_hook_failed", "\"site\":\"Damage\""); return false;
        }
    }
    {   // Звуки игрока: без точного профиля мод работает, но звуков других игроков не слышно.
        if (!std::equal(PlaySoundPrefix.begin(), PlaySoundPrefix.end(), base + PlaySoundRva) || !std::equal(PlayLoopPrefix.begin(), PlayLoopPrefix.end(), base + PlayLoopRva) ||
            !std::equal(StopLoopPrefix.begin(), StopLoopPrefix.end(), base + StopLoopRva)) note("world_profile_mismatch", "\"site\":\"sound\"");
        else if (MH_CreateHook(base + PlaySoundRva, reinterpret_cast<void*>(&hookPlaySound), &s.playSound) != MH_OK || MH_EnableHook(base + PlaySoundRva) != MH_OK ||
                 MH_CreateHook(base + PlayLoopRva, reinterpret_cast<void*>(&hookPlayLoop), &s.playLoop) != MH_OK || MH_EnableHook(base + PlayLoopRva) != MH_OK ||
                 MH_CreateHook(base + StopLoopRva, reinterpret_cast<void*>(&hookStopLoop), &s.stopLoop) != MH_OK || MH_EnableHook(base + StopLoopRva) != MH_OK) {
            note("world_hook_failed", "\"site\":\"Sound\""); s.playSound = s.playLoop = s.stopLoop = nullptr;
        } else note("sound_hook_ready", "\"sounds\":%zu", PlayerSounds.size());
    }
    {   // Рычаги: хост замечает нажатие (UseWallSwitch), клиент повторяет только текстуру (SendRemoteTrigger на время повтора подавлен).
        const bool ok = std::equal(UseSwitchPrefix.begin(), UseSwitchPrefix.end(), base + UseSwitchRva) &&
                        std::equal(RemoteTriggerPrefix.begin(), RemoteTriggerPrefix.end(), base + RemoteTriggerRva);
        if (!ok) note("world_profile_mismatch", "\"site\":\"switch\"");
        else if (host ? (MH_CreateHook(base + UseSwitchRva, reinterpret_cast<void*>(&hookUseSwitch), &s.useSwitch) != MH_OK || MH_EnableHook(base + UseSwitchRva) != MH_OK)
                      : (MH_CreateHook(base + RemoteTriggerRva, reinterpret_cast<void*>(&hookRemoteTrigger), &s.remoteTrigger) != MH_OK || MH_EnableHook(base + RemoteTriggerRva) != MH_OK)) {
            note("world_hook_failed", "\"site\":\"Switch\""); s.useSwitch = s.remoteTrigger = nullptr;
        } else note("switch_hook_ready", "\"role\":\"%s\"", host ? "host" : "client");
    }
    if (!std::equal(CheckpointTouchPrefix.begin(), CheckpointTouchPrefix.end(), base + CheckpointTouchRva)) note("world_profile_mismatch", "\"site\":\"checkpoint\"");
    else if (MH_CreateHook(base + CheckpointTouchRva, reinterpret_cast<void*>(&hookCheckpointTouch), &s.checkpointTouch) != MH_OK || MH_EnableHook(base + CheckpointTouchRva) != MH_OK) {
        note("world_hook_failed", "\"site\":\"Checkpoint\""); s.checkpointTouch = nullptr;
    } else note("checkpoint_hook_ready", "\"rva\":%u", CheckpointTouchRva);
    if (host) {
        if (!std::equal(ConsumePrefix.begin(), ConsumePrefix.end(), base + ConsumeRva)) note("world_profile_mismatch", "\"site\":\"consume\"");
        else if (MH_CreateHook(base + ConsumeRva, reinterpret_cast<void*>(&hookConsume), &s.consume) != MH_OK || MH_EnableHook(base + ConsumeRva) != MH_OK) note("world_hook_failed", "\"site\":\"Consume\"");
    } else { // клиент: шары своего игрока берёт хост (у хоста на этих адресах хуки P14)
        if (!std::equal(HealthTouchPrefix.begin(), HealthTouchPrefix.end(), base + HealthTouchRva) || !std::equal(AmmoTouchPrefix.begin(), AmmoTouchPrefix.end(), base + AmmoTouchRva) ||
            !std::equal(ConsumePrefix.begin(), ConsumePrefix.end(), base + ConsumeRva)) note("world_profile_mismatch", "\"site\":\"sphere_touch\"");
        else if (MH_CreateHook(base + HealthTouchRva, reinterpret_cast<void*>(&hookSphereTouch<0>), &s.healthTouch) != MH_OK || MH_EnableHook(base + HealthTouchRva) != MH_OK ||
                 MH_CreateHook(base + AmmoTouchRva, reinterpret_cast<void*>(&hookSphereTouch<1>), &s.ammoTouch) != MH_OK || MH_EnableHook(base + AmmoTouchRva) != MH_OK) {
            note("world_hook_failed", "\"site\":\"SphereTouch\""); s.healthTouch = s.ammoTouch = nullptr;
        } else note("sphere_hook_ready", "\"health_rva\":%u,\"ammo_rva\":%u", HealthTouchRva, AmmoTouchRva);
        if (!std::equal(WeaponTouchPrefix.begin(), WeaponTouchPrefix.end(), base + WeaponTouchRva)) note("world_profile_mismatch", "\"site\":\"weapon_touch\"");
        else if (MH_CreateHook(base + WeaponTouchRva, reinterpret_cast<void*>(&hookWeaponTouch), &s.weaponTouch) != MH_OK || MH_EnableHook(base + WeaponTouchRva) != MH_OK) {
            note("world_hook_failed", "\"site\":\"WeaponTouch\""); s.weaponTouch = nullptr;
        } else note("touch_hook_ready", "\"weapon_rva\":%u", WeaponTouchRva);
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
            s.debugTeleportOn = sscanf_s(v, "%f,%f,%f,%f", &s.debugTx, &s.debugTy, &s.debugTyaw, &s.debugTz) >= 3; // x,y,yaw[,z]: z — высота, на которой искать сектор (по умолчанию высота актора)
    }
    {
        char v[16]{};
        if (GetEnvironmentVariableA("PSE_DEBUG_ENEMY", v, sizeof(v))) s.debugEnemy = std::atoi(v);
        if (host && GetEnvironmentVariableA("PSE_DEBUG_KILL", v, sizeof(v))) s.debugKill = std::atoi(v);
    }
    note("world_hooks_ready", "\"spawn_rva\":%u,\"key_touch_rva\":%u", SpawnRva, KeyTouchRva);
    return true;
}

std::uint32_t popSound() {
    if (!s.soundCount) return 0xffffffffu;
    const auto v = s.soundQueue[s.soundHead]; s.soundHead = (s.soundHead + 1) % s.soundQueue.size(); --s.soundCount;
    if (++s.soundsSent <= 20) note("world_sound_sent", "\"sound\":%u,\"path\":\"%s\"", v, v & SoundStop ? "stop" : PlayerSounds[v & 0xff]);
    return v;
}
void playPlayerSound(Byte* actor, std::uint32_t sound) {
    if (!thread() || !s.installed || !actor || !s.playSound || (sound & 0xff) >= PlayerSounds.size()) return;
    ForeignScope foreign; s.soundReplay = true;
    // Через собственные хуки (адрес функции): флаг soundReplay не даёт повтору уйти обратно в сеть.
    if (sound & SoundStop) reinterpret_cast<void(__fastcall*)(void*)>(s.base + StopLoopRva)(actor);
    else if (sound & SoundLoop) reinterpret_cast<PlayLoopFn>(s.base + PlayLoopRva)(actor, PlayerSounds[sound & 0xff]);
    else reinterpret_cast<PlaySoundFn>(s.base + PlaySoundRva)(actor, PlayerSounds[sound & 0xff], (sound & SoundFlag) != 0);
    s.soundReplay = false;
    static unsigned logged = 0; if (++logged <= 20) note("world_sound_played", "\"sound\":%u", sound);
}
void setRemoteBodyTest(bool (*test)(const void*)) { s.remoteTest = test; }
void setLocalActorGetter(unsigned char* (*get)()) { s.localActor = get; }
int setHudKind(int kind) { const int before = s.hudKind; s.hudKind = kind; return before; }
void setActive(bool on) {
    if (!thread()) return;
    if (on && on == s.active && s.installed) trackMovers(); // (not on the first active tick: the queues are reset just below)
    if (on) {
        ++s.ticks;
        if (s.installed) installHudSinks();
        if (s.debugBreak >= 0 && s.ticks == 350) {
            const int want = s.debugBreak; s.debugBreak = -1;
            Byte* target = nullptr;
            forEachActor([&](Byte* a, int) { if (isPuppet(a) || read<Byte*>(a) == s.base + AiVtable || read<int>(a, 0x180) != want || read<std::int16_t>(a, 0x17a) <= 0) return false; target = a; return true; });
            note("world_debug_break", "\"id\":%d,\"found\":%s", want, target ? "true" : "false");
            if (target) hookDamage(target, nullptr, 0, 30000);
        }
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
    s.head = s.count = 0; s.keysGained = 0; s.sceneDone.fill(false); s.sceneSubstituted.fill(false); s.breakHead = s.breakCount = 0; s.brokenSeen.clear();
    s.soundHead = s.soundCount = 0; s.soundLastTick.fill(0);
    for (auto& l : s.lifts) l.sector = -1;
    s.doors.clear(); s.reopen.clear();
    note("world_sync_active", "\"active\":%s", on ? "true" : "false");
}
bool popMover(Mover& out) {
    if (!s.installed || !s.count) return false;
    out = s.movers[s.head]; s.head = (s.head + 1) % QueueCap; --s.count;
    return true;
}
std::uint16_t popKeysGained() { const auto v = s.keysGained; s.keysGained = 0; return v; }
int popBreak() {
    if (!s.installed || !s.breakCount) return -1;
    const int id = s.breakQueue[s.breakHead]; s.breakHead = (s.breakHead + 1) % s.breakQueue.size(); --s.breakCount;
    return id;
}
bool applyBreak(int id) {
    ForeignScope foreign;
    if (!thread() || !s.installed || !s.damage || !s.active || id < 0 || s.brokenSeen.count(id)) return false;
    s.brokenSeen[id] = s.ticks;
    Byte* target = nullptr;
    forEachActor([&](Byte* a, int) {
        if (isPuppet(a) || read<Byte*>(a) == s.base + AiVtable || read<int>(a, 0x180) != id || read<std::int16_t>(a, 0x17a) <= 0) return false;
        target = a; return true;
    });
    if (!target) { note("world_break_missing", "\"id\":%d", id); return false; }
    s.replaying = true;
    reinterpret_cast<DamageFn>(s.damage)(target, nullptr, 0, 30000);
    s.replaying = false;
    note("world_break_applied", "\"id\":%d,\"alive_after\":%s", id, read<std::int16_t>(target, 0x17a) > 0 ? "true" : "false");
    return true;
}
ScriptCall popScript() {
    if (!s.installed || !s.scriptCount) return {};
    const auto c = s.scriptQueue[s.scriptHead]; s.scriptHead = (s.scriptHead + 1) % s.scriptQueue.size(); --s.scriptCount;
    return c;
}
float floorAt(float x, float y, float z, int sector);
int findSector(float x, float y, float z);
// Игрока ставят туда, где стоял запустивший сцену: ближайшая валидная точка вокруг (сетка 64) на близких высотах, родная перелинковка сектора.
bool gatherTo(Byte* actor, float x, float y, float z) {
    int sector = findSector(x, y, z); float tx = x, ty = y, pz = z;
    for (int ring = 1; ring <= 6 && sector < 0; ++ring)
        for (int dx = -ring; dx <= ring && sector < 0; ++dx)
            for (int dy = -ring; dy <= ring && sector < 0; ++dy) {
                if (std::abs(dx) != ring && std::abs(dy) != ring) continue;
                for (float dz : {0.0f, 64.0f, -64.0f, 128.0f, -128.0f}) {
                    const int sc = findSector(x + dx * 64.0f, y + dy * 64.0f, z + dz);
                    if (sc >= 0) { sector = sc; tx = x + dx * 64.0f; ty = y + dy * 64.0f; pz = z + dz; break; }
                }
            }
    if (sector < 0) return false;
    return teleport(actor, tx, ty, floorAt(tx, ty, pz, sector), sector);
}
bool applyScript(int num, Byte* instigator, const ScriptCall* from) {
    ForeignScope foreign;
    if (!thread() || !s.installed || !s.script || num <= 0 || num >= 256 || !isPuppet(instigator)) return false;
    if (s.scriptTick[num] && s.ticks - s.scriptTick[num] < 180) { note("script_skipped", "\"num\":%d,\"reason\":\"already_started_here\"", num); return false; }
    if (isScene(num)) {
        if (s.sceneDone[num]) { note("script_skipped", "\"num\":%d,\"reason\":\"scene_already_played\"", num); return false; }
        s.sceneDone[num] = true;
        if (from && from->pos) {
            const float dx = from->x - read<float>(instigator, 0x18), dy = from->y - read<float>(instigator, 0x1c), dz = from->z - read<float>(instigator, 0x20);
            if (dx * dx + dy * dy > 400.0f * 400.0f || std::fabs(dz) > 200.0f) {
                const bool ok = gatherTo(instigator, from->x, from->y, from->z);
                note("script_gather", "\"num\":%d,\"ok\":%s,\"to\":[%.1f,%.1f,%.1f]", num, ok ? "true" : "false", from->x, from->y, from->z);
                if (!ok) return false; // без места рядом со сценой её не играют: лучше пропустить, чем чёрный экран
            }
        }
    }
    s.scriptTick[num] = s.ticks;
    s.replaying = true;
    reinterpret_cast<ScriptFn>(s.script)(num, instigator, 0.0f);
    s.replaying = false;
    note("script_applied", "\"num\":%d", num);
    return true;
}
void debugScript(Byte* instigator) {
    if (!thread() || !s.installed || !s.script || s.debugScriptNum <= 0 || !isPuppet(instigator)) return;
    const int num = s.debugScriptNum; s.debugScriptNum = -1;
    note("script_debug_trigger", "\"num\":%d", num);
    s.forceRoot = true; hookScript(num, instigator, 0.0f); s.forceRoot = false; // как пересечение триггера игроком
}
std::uint32_t popAchievements() { const auto v = s.achLocal; s.achLocal = 0; return v; }
bool applyAchievement(unsigned ordinal) {
    if (!thread() || !s.installed || !s.ach || ordinal >= AchKey.size() || (s.achSeen >> ordinal & 1)) return false;
    s.achSeen |= 1u << ordinal;
    auto* service = read<void*>(s.base, AchServiceRva);
    bool done = s.achDryRun;
    if (!s.achDryRun && service) {
        s.replaying = true;
        __try { reinterpret_cast<AchFn>(s.ach)(service, AchKey[ordinal], 0); done = true; } __except (EXCEPTION_EXECUTE_HANDLER) { done = false; }
        s.replaying = false;
    }
    note("ach_applied", "\"key\":\"%s\",\"ordinal\":%u,\"native\":%s,\"dry_run\":%s", AchKey[ordinal], ordinal, done && !s.achDryRun ? "true" : "false", s.achDryRun ? "true" : "false");
    return true;
}
void debugAchievement() {
    if (!thread() || !s.installed || !s.ach || !s.achDryRun || s.debugAch < 0 || s.debugAch >= static_cast<int>(AchKey.size())) return; // только в dry-run: тест никогда не трогает настоящий Steam
    const int ord = s.debugAch; s.debugAch = -1;
    note("ach_debug_trigger", "\"ordinal\":%d", ord);
    auto* service = read<void*>(s.base, AchServiceRva);
    hookAch(service, AchKey[ord], 0); // как вызов из эмиттера игры: уйдёт второй стороне
}

bool applyMover(const Mover& m) {
    ForeignScope foreign;
    if (!thread() || !s.installed || m.cls >= ClassRva.size()) return false;
    auto* sector = sectorAt(m.sector);
    if (!sector) { note("world_mover_invalid", "\"sector\":%d", m.sector); return false; }
    if (read<Byte>(sector, 0x14) & 4) {
        if (m.reopen && s.client) {   // the host's door went back up; ours is still moving: closing -> open it again once it has closed, else it rose by itself here
            const auto d = s.doors.find(m.sector);
            const bool closing = d != s.doors.end() && d->second.falling;
            if (closing) s.reopen[m.sector] = {m.type, s.ticks + 300};
            note("world_door_reopen_received", "\"sector\":%d,\"type\":%u,\"closing_here\":%s", m.sector, m.type, closing ? "true" : "false");
            return false;
        }
        if (m.cls == ClassLiftImmediate && startIdleLift(m.sector)) return true; // the other player's lift is moving: start our waiting one too
        ++s.busySkips; note("world_mover_busy", "\"class\":\"%s\",\"type\":%u,\"sector\":%d", ClassName[m.cls], m.type, m.sector); return false;
    }
    if (m.reopen && !s.client) return false;
    s.replaying = true;
    void* r = s.spawn(s.base + FactoryRva, reinterpret_cast<const char*>(s.base + ClassRva[m.cls]), m.type, m.sector);
    s.replaying = false;
    if (r) trackDoor(m.cls, m.type, m.sector);
    watchSector(m.sector);
    ++s.appliedMovers;
    const auto p = probe(m.sector);
    note("world_mover_applied", "\"class\":\"%s\",\"type\":%u,\"sector\":%d,\"spawned\":%s,\"flags\":%u,\"floor_d\":%.3f,\"ceiling_d\":%.3f", ClassName[m.cls], m.type, m.sector, r ? "true" : "false", p.flags, p.floorD, p.ceilingD);
    return r != nullptr;
}

bool applyKeys(Byte* player, Byte* puppet, std::uint16_t mask) {
    ForeignScope foreign;
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
    // The body arrives standing: no momentum is carried over (the engine's own OnDamaged zeroes the same Vec3 at +0xe0). Without this the fall
    // speed kept growing while the pose kept pulling a body down a lift shaft back up, and the landing at the bottom killed it (-303).
    *reinterpret_cast<float*>(a + 0xe0) = 0.0f; *reinterpret_cast<float*>(a + 0xe4) = 0.0f; *reinterpret_cast<float*>(a + 0xe8) = 0.0f;
    return true;
}
void debugTeleport(Byte* actor) {
    if (!thread() || !s.installed || !s.debugTeleportOn || !actor) return;
    s.debugTeleportOn = false;
    float probeZ = std::isfinite(s.debugTz) ? s.debugTz : read<float>(actor, 0x20);
    float tx = s.debugTx, ty = s.debugTy;
    int sector = findSector(tx, ty, probeZ);
    if (sector < 0 && std::isfinite(s.debugTz)) {   // z задан: ближайшая валидная точка вокруг цели (сетка 64, до 640) и на близких высотах
        for (int ring = 1; ring <= 10 && sector < 0; ++ring)
            for (int dx = -ring; dx <= ring && sector < 0; ++dx)
                for (int dy = -ring; dy <= ring && sector < 0; ++dy) {
                    if (std::abs(dx) != ring && std::abs(dy) != ring) continue;
                    for (float dz : {0.0f, 64.0f, -64.0f, 128.0f, -128.0f, 256.0f, -256.0f}) {
                        const int sc = findSector(s.debugTx + dx * 64.0f, s.debugTy + dy * 64.0f, s.debugTz + dz);
                        if (sc >= 0) { sector = sc; tx = s.debugTx + dx * 64.0f; ty = s.debugTy + dy * 64.0f; probeZ = s.debugTz + dz; break; }
                    }
                }
    }
    const float floor = sector >= 0 ? floorAt(tx, ty, probeZ, sector) : NAN;
    const bool ok = sector >= 0 && teleport(actor, tx, ty, floor, sector);
    if (ok) { *reinterpret_cast<float*>(actor + 0x38) = s.debugTyaw; reinterpret_cast<ResetFn>(s.base + ResetRva)(actor + 0x78); }
    note("world_debug_teleport", "\"ok\":%s,\"sector\":%d,\"floor\":%.1f,\"x\":%.1f,\"y\":%.1f,\"yaw\":%.3f", ok ? "true" : "false", sector, floor, tx, ty, s.debugTyaw);
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

// Тест (клиент): PSE_DEBUG_HIT_CLIENT="номер врага,урон" + PSE_DEBUG_HIT_FILE — когда файл появится, свой игрок «попадает» во врага карты тем же
// нативным Damage, что и выстрел (через перехват: у клиента урон не наносится, а уходит хосту).
void debugHit(Byte* own) {
    static int id = -1, amount = 0; static bool readEnv = false;
    if (!readEnv) { readEnv = true; char v[48]{}; if (GetEnvironmentVariableA("PSE_DEBUG_HIT_CLIENT", v, sizeof(v)) && sscanf_s(v, "%d,%d", &id, &amount) != 2) id = -1; }
    if (!thread() || !s.installed || !s.client || id < 0 || !own) return;
    char file[400]{};
    if (GetEnvironmentVariableA("PSE_DEBUG_HIT_FILE", file, sizeof(file)) && GetFileAttributesA(file) == INVALID_FILE_ATTRIBUTES) return;
    const int want = id; id = -1;
    Byte* target = nullptr;
    forEachActor([&](Byte* a, int) { if (read<Byte*>(a) != s.base + AiVtable || read<int>(a, 0x180) != want) return false; target = a; return true; });
    const int before = target ? read<std::int16_t>(target, 0x17a) : 0;
    if (target) reinterpret_cast<DamageFn>(s.base + DamageRva)(target, own, 0, static_cast<short>(amount));
    note("world_debug_hit", "\"id\":%d,\"found\":%s,\"hp_before\":%d,\"hp_after\":%d,\"queued\":%zu", want, target ? "true" : "false", before, target ? static_cast<int>(read<std::int16_t>(target, 0x17a)) : 0, s.hitCount);
}
void debugKeys(Byte* player) {
    if (!thread() || !s.installed || !player || s.debugKeyBits == 0) return;
    const auto bits = s.debugKeyBits; s.debugKeyBits = 0;
    *reinterpret_cast<std::uint16_t*>(player + 0x98) = static_cast<std::uint16_t>(read<std::uint16_t>(player, 0x98) | bits);
    if (s.active) s.keysGained |= static_cast<std::uint16_t>(bits); // как подбор у этой стороны
    note("world_debug_key", "\"bits\":%u,\"keys\":%u", bits, read<std::uint16_t>(player, 0x98));
}

bool replicaActive() { return replicaOn(); }
void prepareHud() { if (thread() && s.installed) installHudSinks(); }

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
    // Актор карты, бывший в мире в прошлый сбор и пропавший теперь — у хоста мир один (переделка, этап 3): враг, разорванный взрывом и убранный в тот же
    // тик, стена или декорация, убранная скриптом, ключ, взятый хостом, — у клиента убирается тот же актор (EvEnemyGone = «актор карты исчез»,
    // любого класса). Не учитываются касаемые предметы (флаг 0x100: оружие, анкхи, артефакты каждый игрок берёт сам; шары — EvPickup; ключи и так
    // общие) и тела игроков. Учёт идёт весь мир (resetWorld).
    if (n < max) {
        const auto round = ++s.enemyRound;
        forEachActor([&](Byte* a, int) {
            const int id = read<int>(a, 0x180);
            const auto* node = read<Byte*>(a, 0x10);
            if (id < 0 || id >= 100000 || isPuppet(a) || !node || (read<Byte>(a, 0x40) && read<int>(node, 0x40) < 1)) return false;
            if ((read<unsigned>(a, 0x16c) & 0x100u) && read<Byte*>(a) != s.base + AiVtable) return false;
            s.enemySeen[id] = round;
            return false;
        });
        for (auto it = s.enemySeen.begin(); it != s.enemySeen.end();) {
            if (it->second == round || s.goneCount >= s.goneQueue.size()) { ++it; continue; } // a full queue: the rest goes next round
            s.goneQueue[(s.goneHead + s.goneCount++) % s.goneQueue.size()] = it->first;
            it = s.enemySeen.erase(it);
        }
    }
    return n;
}
int popEnemyGone() {
    if (!s.goneCount) return -1;
    const int id = s.goneQueue[s.goneHead]; s.goneHead = (s.goneHead + 1) % s.goneQueue.size(); --s.goneCount;
    static unsigned logged = 0; if (++logged <= 30) note("world_enemy_gone_sent", "\"id\":%d", id);
    return id;
}
int popPickup() {
    if (!s.pickupCount) return -1;
    const int id = s.pickupQueue[s.pickupHead]; s.pickupHead = (s.pickupHead + 1) % s.pickupQueue.size(); --s.pickupCount;
    static unsigned logged = 0; if (++logged <= 30) note("world_pickup_sent", "\"id\":%d", id);
    return id;
}

namespace {
// Состояние врага хоста на актор клиента: позиция (телепорт при смене сектора), скорость, угол, здоровье, состояние ИИ, анимация.
void steerEnemy(Byte* a, const Enemy& e) {
    if (!std::isfinite(e.x + e.y + e.z + e.yaw + e.vx + e.vy + e.vz)) return;
    const float dx = e.x - read<float>(a, 0x18), dy = e.y - read<float>(a, 0x1c), dz = e.z - read<float>(a, 0x20);
    if (dx * dx + dy * dy + dz * dz > 0.25f) {
        if (read<int>(a, 0x3c) != e.sector) { if (teleport(a, e.x, e.y, e.z, e.sector)) ++s.enemyMoves; }
        else { *reinterpret_cast<float*>(a + 0x18) = e.x; *reinterpret_cast<float*>(a + 0x1c) = e.y; *reinterpret_cast<float*>(a + 0x20) = e.z; ++s.enemyMoves; }
    }
    *reinterpret_cast<float*>(a + 0xe0) = e.vx; *reinterpret_cast<float*>(a + 0xe4) = e.vy; *reinterpret_cast<float*>(a + 0xe8) = e.vz;
    *reinterpret_cast<float*>(a + 0x38) = e.yaw;
    // The host says it is dead and here it is not: the engine's own death (Damage 0x1a1370 at hp <= 0: death animation, no longer shootable,
    // OnDeath). Writing hp alone could leave a walking "phantom" (a vase scorpion stayed alive for the client after the host killed it).
    if (e.hp <= 0 && read<std::int16_t>(a, 0x17a) > 0 && (read<unsigned>(a, 0x16c) & 0x20u)) {
        s.replicaKill = true;
        reinterpret_cast<DamageFn>(s.base + DamageRva)(a, nullptr, 0, static_cast<short>(read<std::int16_t>(a, 0x17a)));
        s.replicaKill = false;
    }
    *reinterpret_cast<std::int16_t*>(a + 0x17a) = e.hp;
    *reinterpret_cast<int*>(a + 0x1b8) = e.aiState;
    if (e.anim != 0xffff) {
        const auto* def = read<Byte*>(a, 0x170);
        Byte* target = def ? read<Byte*>(def, e.anim) : nullptr;
        if (target && read<Byte*>(a, 0x108) != target) reinterpret_cast<std::uint64_t(__fastcall*)(void*, void*)>(s.base + ChangeAnimRva)(a, target);
    }
    ++s.enemyApplies;
}
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
        if (a) steerEnemy(a, in[i]);
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

bool applyEnemyGone(int id) {
    if (!thread() || !s.installed || !s.client || !s.active || id < 0) return false;
    Byte* target = nullptr;
    // Any class (stage 3): the host's world lost this map actor. Touchable items (weapons, ankhs, artifacts) are each player's own and never sent.
    forEachActor([&](Byte* a, int) {
        if (read<int>(a, 0x180) != id || read<Byte>(a, 0x40) || isPuppet(a)) return false;
        if ((read<unsigned>(a, 0x16c) & 0x100u) && read<Byte*>(a) != s.base + AiVtable) return false;
        target = a; return true;
    });
    static unsigned logged = 0;
    if (++logged <= 60) note("world_enemy_gone_applied", "\"id\":%d,\"found\":%s,\"hp\":%d,\"ai\":%s,\"vt\":\"%llx\"", id, target ? "true" : "false", target ? static_cast<int>(read<std::int16_t>(target, 0x17a)) : 0,
        target && read<Byte*>(target) == s.base + AiVtable ? "true" : "false", target ? static_cast<unsigned long long>(read<Byte*>(target) - s.base) : 0ull);
    if (!target) return false;
    removeActor(target);
    return true;
}
bool applyPickup(int id) {
    if (!thread() || !s.installed || !s.client || !s.active || id < 0) return false;
    Byte* target = nullptr;
    forEachActor([&](Byte* a, int) { if (!sphere(a) || read<int>(a, 0x180) != id || read<Byte>(a, 0x40)) return false; target = a; return true; });
    // Шар взят по запросу ЭТОГО клиента (его игрок коснулся его здесь): здоровье/патроны уже у тела игрока на хосте и придут снимком, здесь — то,
    // что игра показывает взявшему: звук подбора, надпись, вспышка (родной consume 0x1d8360 с актором своего игрока; он же убирает шар) и для шара
    // здоровья вспышка полосы здоровья (HUD +0x30, как в OnTouch). Чужой подбор шар просто убирает.
    const auto asked = s.pickupAsked.find(id);
    const bool mine = asked != s.pickupAsked.end() && s.ticks - asked->second <= 300;
    if (asked != s.pickupAsked.end()) s.pickupAsked.erase(asked);
    static unsigned logged = 0; if (++logged <= 30) note("world_pickup_applied", "\"id\":%d,\"found\":%s,\"mine\":%s", id, target ? "true" : "false", mine ? "true" : "false");
    if (!target) return false;
    if (mine && s.ownActor && read<std::int16_t>(s.ownActor, 0x17a) > 0 && std::equal(ConsumePrefix.begin(), ConsumePrefix.end(), s.base + ConsumeRva)) {
        const bool health = read<const Byte*>(target) == s.base + HealthPickupVtable;
        reinterpret_cast<TouchFn>(s.base + ConsumeRva)(target, s.ownActor);
        if (!read<Byte>(target, 0x40)) removeActor(target);
        auto* hud = s.base + HudObjectRva; auto* vt = read<Byte**>(hud);
        if (health && vt) reinterpret_cast<void(__fastcall*)(void*)>(vt[6])(hud);
        return true;
    }
    removeActor(target);
    return true;
}
int popPickupAsk() {
    if (!s.askCount) return -1;
    const int id = s.askQueue[s.askHead]; s.askHead = (s.askHead + 1) % s.askQueue.size(); --s.askCount;
    return id;
}
// Сложность (GameInstance +0x1d8, геттер vtable +0x158 = 0x1ae6a0): от неё — полоса здоровья на HUD (100 на сложностях 1-2, иначе 200 на анкх),
// сколько даёт шар здоровья и урон. Здоровье игрока ведёт хост: при другой сложности у клиента 200 хоста рисовались двумя полосами.
constexpr std::uint32_t GameInstanceRva = 0xaf33a0, DifficultyOffset = 0x1d8;
constexpr std::array<Byte, 7> DifficultyGetter{0x8b, 0x81, 0xd8, 0x01, 0x00, 0x00, 0xc3};
int difficulty() {
    if (!s.base || !std::equal(DifficultyGetter.begin(), DifficultyGetter.end(), s.base + 0x1ae6a0)) return -1;
    return read<int>(s.base + GameInstanceRva, DifficultyOffset);
}
bool applyDifficulty(int value) {
    if (!thread() || !s.installed || !s.client || value < 0 || value > 7) return false;
    const int before = difficulty();
    if (before < 0 || before == value) return false;
    *reinterpret_cast<int*>(s.base + GameInstanceRva + DifficultyOffset) = value;
    note("world_difficulty_adopted", "\"host\":%d,\"before\":%d", value, before);
    return true;
}
namespace {
// Порождённые типы (defs/defines.txt): враги, шары/карта/сила и вспышки появления шаров (Fx_HealthSpawn/AmmoSpawn/PowerSpawn — их клиент только убирает).
constexpr std::array<std::uint16_t, 25> DynTypes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 1107, 1109, 40, 41, 51, 52, 144, 145, 63, 64, 67};
constexpr std::uint32_t DynTag = 0x60000000u; // метка отражения в Actor+0x180 (у порождённых актёров там -1)
std::array<ProjDef, DynTypes.size()> dynDefs{};
int dynDefCount = -1;
void buildDynDefs() {
    if (dynDefCount >= 0) return;
    dynDefCount = 0;
    if (!projProfile()) return;
    void* table = read<void*>(s.base, DefTableRva);
    if (!table) { dynDefCount = -1; return; }
    for (const auto type : DynTypes) {
        const int key = type;
        auto* node = reinterpret_cast<Byte*(__fastcall*)(void*, const int*)>(s.base + DefLookupRva)(table, &key);
        if (node) dynDefs[dynDefCount++] = {node + 8, type};
    }
    note("world_dynamic_defs", "\"found\":%d", dynDefCount);
}
int dynTypeOf(const Byte* a) {
    const void* def = read<void*>(a, 0x170);
    for (int i = 0; i < dynDefCount; ++i) if (dynDefs[i].def == def) return dynDefs[i].type;
    return -1;
}
bool aiType(int t) { return t < 40 || t >= 1100; }
bool flashType(int t) { return t == 63 || t == 64 || t == 67; }
std::unordered_map<Byte*, std::uint32_t> hostDynIds; std::uint32_t nextDynId = 1;
std::unordered_map<std::uint32_t, std::uint64_t> dynSeen;   // клиент: id хоста -> тик, когда хост последний раз его прислал
std::unordered_map<std::uint32_t, std::uint64_t> dynMine;   // клиент: отражения, поставленные здесь (id -> тик постановки)
std::unordered_map<std::uint32_t, std::uint64_t> dynHold;   // клиент: отражение пропало здесь (шар подобран) — до этого тика заново не ставить
}
int collectDynamic(Dynamic* out, int max) {
    int n = 0;
    if (!thread() || !s.installed) return 0;
    buildDynDefs();
    if (dynDefCount <= 0) return 0;
    std::unordered_map<Byte*, std::uint32_t> seen;
    forEachActor([&](Byte* a, int) {
        if (n >= max) return true;
        if (read<int>(a, 0x180) >= 0 || read<Byte>(a, 0x40)) return false; // актёр карты (у них свои пути) или уже убран
        const int type = dynTypeOf(a);
        if (type < 0 || flashType(type) || !read<Byte*>(a, 0x10)) return false;
        auto it = hostDynIds.find(a);
        const std::uint32_t id = it != hostDynIds.end() ? it->second : (nextDynId++ & 0x00ffffffu);
        seen[a] = id;
        Dynamic& d = out[n++];
        d = Dynamic{};
        d.id = id; d.type = static_cast<std::uint16_t>(type);
        d.x = read<float>(a, 0x18); d.y = read<float>(a, 0x1c); d.z = read<float>(a, 0x20);
        d.vx = read<float>(a, 0xe0); d.vy = read<float>(a, 0xe4); d.vz = read<float>(a, 0xe8);
        d.yaw = read<float>(a, 0x38); d.scale = read<float>(a, 0x124); d.sector = static_cast<std::int16_t>(read<int>(a, 0x3c));
        d.hp = read<std::int16_t>(a, 0x17a); d.frame = static_cast<std::uint16_t>(read<int>(a, 0x178));
        if (aiType(type)) { d.aiState = static_cast<std::uint16_t>(read<int>(a, 0x1b8)); const int off = animOffset(a); d.anim = off >= 0 ? static_cast<std::uint16_t>(off) : 0xffff; }
        return false;
    });
    hostDynIds.swap(seen);
    return n;
}
static_assert(DynTag == SphereMirrorTag, "the mirror tag of a host dynamic sphere is its wire id prefix");
int takePickup(int id, Byte* body) {
    if (!thread() || !s.installed || s.client || !s.active || !body || id < 0 || read<std::int16_t>(body, 0x17a) <= 0) return 0;
    Byte* target = nullptr;
    const bool dynamic = (static_cast<std::uint32_t>(id) & 0xff000000u) == SphereMirrorTag;
    Byte* wanted = nullptr;
    if (dynamic) for (const auto& [a, dyn] : hostDynIds) if (dyn == (static_cast<std::uint32_t>(id) & 0x00ffffffu)) { wanted = a; break; }
    if (dynamic && !wanted) return 0;
    // Only a sphere still in the world (the pointer of a dynamic one is checked against the sector lists before any read of it).
    forEachActor([&](Byte* a, int) {
        if (dynamic ? a != wanted : read<int>(a, 0x180) != id) return false;
        if (!sphere(a) || read<Byte>(a, 0x40)) return false;
        target = a; return true;
    });
    if (!target) { if (++s.takeLogged <= 40) note("world_pickup_take", "\"id\":%d,\"result\":\"gone\"", id); return 0; }
    // The body follows its player's pose with a little delay: the touch happened there, not necessarily here. Anything farther is refused.
    const float dx = read<float>(body, 0x18) - read<float>(target, 0x18), dy = read<float>(body, 0x1c) - read<float>(target, 0x1c), dz = read<float>(body, 0x20) - read<float>(target, 0x20);
    if (!(dx * dx + dy * dy <= 448.0f * 448.0f && std::fabs(dz) <= 320.0f)) {
        if (++s.takeLogged <= 40) note("world_pickup_take", "\"id\":%d,\"result\":\"far\",\"dist\":%.0f,\"dz\":%.0f", id, std::sqrt(dx * dx + dy * dy), dz);
        return 0;
    }
    const bool health = read<const Byte*>(target) == s.base + HealthPickupVtable;
    const int hp = read<std::int16_t>(body, 0x17a);
    {   // Through the hooked entry: P14 gives it to the Player of this body (Player() getter scope) and swaps in the weapon its player holds.
        const int before = s.hudKind; s.hudKind = 2;
        reinterpret_cast<TouchFn>(s.base + (health ? HealthTouchRva : AmmoTouchRva))(target, body);
        s.hudKind = before;
    }
    const bool taken = read<Byte>(target, 0x40) != 0;
    if (++s.takeLogged <= 40) note("world_pickup_take", "\"id\":%d,\"result\":\"%s\",\"health\":%s,\"hp_before\":%d,\"hp_after\":%d", id, taken ? "taken" : "refused",
        health ? "true" : "false", hp, static_cast<int>(read<std::int16_t>(body, 0x17a)));
    return taken ? (dynamic ? 2 : 1) : 0;
}
void debugTouch(Byte* own) {
    static bool fired = false; char spec[128]{}, file[MAX_PATH]{};
    if (fired || !thread() || !s.installed || !s.client || !s.active || !own) return;
    if (!GetEnvironmentVariableA("PSE_DEBUG_TOUCH_CLIENT", spec, sizeof(spec)) || !GetEnvironmentVariableA("PSE_DEBUG_TOUCH_FILE", file, sizeof(file)) ||
        GetFileAttributesA(file) == INVALID_FILE_ATTRIBUTES) return;
    fired = true;
    for (char* p = spec; p && *p;) {
        int kind = -1, id = -1;
        if (sscanf_s(p, "%d,%d", &kind, &id) == 2 && kind >= 0 && kind < TouchKinds) {
            static constexpr std::uint32_t vt[TouchKinds]{HealthPickupVtable, AmmoPickupVtable, KeyVtable, WeaponVtable, CheckpointVtable};
            static constexpr std::uint32_t rva[TouchKinds]{HealthTouchRva, AmmoTouchRva, KeyTouchRva, WeaponTouchRva, CheckpointTouchRva};
            Byte* target = nullptr;
            forEachActor([&](Byte* a, int) { if (read<Byte*>(a) != s.base + vt[kind] || read<int>(a, 0x180) != id) return false; target = a; return true; });
            note("world_debug_touch", "\"kind\":%d,\"id\":%d,\"found\":%s", kind, id, target ? "true" : "false");
            if (target) reinterpret_cast<TouchFn>(s.base + rva[kind])(target, own); // the hooked native entry: exactly what the engine's touch scan calls
        }
        p = std::strchr(p, ';'); if (p) ++p;
    }
}
void debugRemove() {
    static bool fired = false; char spec[128]{}, file[MAX_PATH]{};
    if (fired || !thread() || !s.installed || s.client) return;
    if (!GetEnvironmentVariableA("PSE_DEBUG_REMOVE", spec, sizeof(spec)) || !GetEnvironmentVariableA("PSE_DEBUG_REMOVE_FILE", file, sizeof(file)) ||
        GetFileAttributesA(file) == INVALID_FILE_ATTRIBUTES) return;
    fired = true;
    for (char* p = spec; p && *p;) {
        const int id = std::atoi(p);
        Byte* target = nullptr;
        forEachActor([&](Byte* a, int) { if (isPuppet(a) || read<int>(a, 0x180) != id || read<Byte>(a, 0x40)) return false; target = a; return true; });
        note("world_debug_remove", "\"id\":%d,\"found\":%s,\"vt\":\"%llx\",\"flags\":\"%x\"", id, target ? "true" : "false",
            target ? static_cast<unsigned long long>(read<Byte*>(target) - s.base) : 0ull, target ? read<unsigned>(target, 0x16c) : 0u);
        if (target) removeActor(target);
        p = std::strchr(p, ','); if (p) ++p;
    }
}
bool popTouch(Touch& out) {
    if (!s.touchCount) return false;
    out = s.touchQueue[s.touchHead]; s.touchHead = (s.touchHead + 1) % s.touchQueue.size(); --s.touchCount;
    return true;
}
// Хост: то же касание, что игрок клиента сделал у себя, — нативным OnTouch телом этого игрока (через хуки: шары -> P14 этому игроку,
// оружие -> P16, ключ -> общий набор ключей). Что сделал свой игрок, решает его клиент: ключ и оружие отдаются без проверки расстояния
// (тело идёт за позой игрока с задержкой); шар — с прежней проверкой takePickup (его может взять и другой игрок).
int applyTouch(const Touch& t, Byte* body) {
    if (!thread() || !s.installed || s.client || !s.active || !body || t.id < 0 || read<std::int16_t>(body, 0x17a) <= 0) return 0;
    if (t.kind == TouchHealth || t.kind == TouchAmmo) return takePickup(t.id, body);
    if (t.kind != TouchKey && t.kind != TouchWeapon) return 0;
    const bool key = t.kind == TouchKey;
    Byte* target = nullptr;
    forEachActor([&](Byte* a, int) {
        if (read<Byte*>(a) != s.base + (key ? KeyVtable : WeaponVtable) || read<int>(a, 0x180) != t.id || !(read<unsigned>(a, 0x16c) & 0x100u)) return false;
        target = a; return true;
    });
    const char* result = "gone";
    if (target) {
        const int before = s.hudKind; s.hudKind = 2;
        reinterpret_cast<TouchFn>(s.base + (key ? KeyTouchRva : WeaponTouchRva))(target, body);
        s.hudKind = before; result = "touched";
    }
    if (++s.touchAppliedLogged <= 60) note("world_touch_applied", "\"kind\":%u,\"id\":%d,\"result\":\"%s\"", static_cast<unsigned>(t.kind), t.id, result);
    return std::strcmp(result, "touched") == 0 ? 1 : 0;
}
bool clampHealth(Byte* actor, Byte* player, int floor) {
    if (!thread() || !s.installed || !actor || !player) return false;
    const int hp = read<std::int16_t>(actor, 0x17a), cap = (std::max)(healthCap(player), floor);
    if (hp <= cap || cap <= 0) return false;
    *reinterpret_cast<std::int16_t*>(actor + 0x17a) = static_cast<std::int16_t>(cap);
    if (++s.capLogged <= 40) note("world_health_capped", "\"before\":%d,\"cap\":%d,\"difficulty\":%d,\"ankhs\":%d", hp, cap, difficulty(), static_cast<int>(read<std::int16_t>(player, 0x84)));
    return true;
}
bool popSelfDamage(int& amount, int& kind) {
    if (!s.selfCount) return false;
    const auto h = s.selfQueue[s.selfHead]; s.selfHead = (s.selfHead + 1) % s.selfQueue.size(); --s.selfCount;
    amount = h.amount; kind = h.kind; return true;
}
bool applySelfDamage(Byte* body, int amount, int kind) {
    if (!thread() || !s.installed || s.client || !body || amount <= 0 || read<std::int16_t>(body, 0x17a) <= 0) return false;
    const int before = read<std::int16_t>(body, 0x17a);
    s.selfDamage = true;
    reinterpret_cast<DamageFn>(s.base + DamageRva)(body, nullptr, kind & 0xff, static_cast<short>(std::min(amount, 4000)));
    s.selfDamage = false;
    static unsigned logged = 0; if (++logged <= 30) note("world_self_damage_applied", "\"amount\":%d,\"kind\":%d,\"hp_before\":%d,\"hp_after\":%d", amount, kind, before, static_cast<int>(read<std::int16_t>(body, 0x17a)));
    return true;
}
// Тест (клиент): PSE_DEBUG_SELF_DAMAGE_FILE — когда файл появится, свой игрок получает урон без источника (как утопление), один раз.
void debugSelfDamage(Byte* own) {
    static bool done = false; char file[400]{};
    if (done || !thread() || !s.installed || !s.client || !own || !GetEnvironmentVariableA("PSE_DEBUG_SELF_DAMAGE_FILE", file, sizeof(file)) ||
        GetFileAttributesA(file) == INVALID_FILE_ATTRIBUTES) return;
    done = true;
    reinterpret_cast<DamageFn>(s.base + DamageRva)(own, nullptr, 0, 30);
    note("world_debug_self_damage", "\"amount\":30,\"queued\":%zu", s.selfCount);
}
int popCheckpoint() {
    if (!s.cpCount) return -1;
    const int id = s.cpQueue[s.cpHead]; s.cpHead = (s.cpHead + 1) % s.cpQueue.size(); --s.cpCount;
    return id;
}
bool checkpointPlace(int id, float& x, float& y, float& z, float& yaw, int& sector) {
    if (!thread() || !s.installed || id < 0) return false;
    Byte* found = nullptr;
    forEachActor([&](Byte* a, int) { if (read<Byte*>(a) != s.base + CheckpointVtable || read<int>(a, 0x180) != id) return false; found = a; return true; });
    if (!found) return false;
    x = read<float>(found, 0x18); y = read<float>(found, 0x1c); z = read<float>(found, 0x20); yaw = read<float>(found, 0x38); sector = read<int>(found, 0x3c);
    return std::isfinite(x + y + z + yaw) && sector >= 0;
}
bool popHit(int& id, int& amount, int& kind) {
    if (!s.hitCount) return false;
    const auto h = s.hitQueue[s.hitHead]; s.hitHead = (s.hitHead + 1) % s.hitQueue.size(); --s.hitCount;
    id = h.id; amount = h.amount; kind = h.kind; ++s.hitsSent;
    return true;
}
bool applyHit(int id, int amount, int kind, Byte* body) {
    if (!thread() || !s.installed || s.client || !s.active || id < 0 || amount <= 0) return false;
    const bool dynamic = (static_cast<std::uint32_t>(id) & 0xff000000u) == SphereMirrorTag;
    Byte* wanted = nullptr;
    if (dynamic) { for (const auto& [a, dyn] : hostDynIds) if (dyn == (static_cast<std::uint32_t>(id) & 0x00ffffffu)) { wanted = a; break; } if (!wanted) return false; }
    Byte* target = nullptr;
    forEachActor([&](Byte* a, int) {
        if (dynamic ? a != wanted : read<int>(a, 0x180) != id) return false;
        if (read<Byte*>(a) != s.base + AiVtable || read<Byte>(a, 0x40)) return false;
        target = a; return true;
    });
    const int hp = target ? read<std::int16_t>(target, 0x17a) : 0;
    bool done = false;
    if (target && hp > 0 && (read<unsigned>(target, 0x16c) & 0x20u)) {
        // The same native Damage the client's own shot would have made, from that player's body (the enemy turns to it, a kill is its kill).
        s.clientHit = true;
        reinterpret_cast<DamageFn>(s.base + DamageRva)(target, body && read<std::int16_t>(body, 0x17a) > 0 ? body : nullptr, kind & 0xff, static_cast<short>(std::min(amount, 4000)));
        s.clientHit = false; done = true;
    }
    if (++s.hitAppliedLogged <= 40) note("world_hit_applied", "\"id\":%d,\"amount\":%d,\"kind\":%d,\"found\":%s,\"hp_before\":%d,\"hp_after\":%d", id, amount, kind,
        target ? "true" : "false", hp, target ? static_cast<int>(read<std::int16_t>(target, 0x17a)) : 0);
    return done;
}
void applyDynamic(const Dynamic* in, int count) {
    if (!thread() || !s.installed || !s.client || !s.active) return;
    buildDynDefs();
    if (dynDefCount <= 0) return;
    std::unordered_map<std::uint32_t, Byte*> local;
    forEachActor([&](Byte* a, int) {
        const auto tag = static_cast<std::uint32_t>(read<int>(a, 0x180));
        if ((tag & 0xff000000u) == DynTag && !read<Byte>(a, 0x40)) local[tag & 0x00ffffffu] = a;
        return false;
    });
    for (int i = 0; i < count; ++i) {
        const Dynamic& d = in[i];
        if (!std::isfinite(d.x + d.y + d.z + d.vx + d.vy + d.vz + d.yaw + d.scale) || d.sector < 0) continue;
        dynSeen[d.id] = s.ticks;
        auto it = local.find(d.id);
        Byte* a = it != local.end() ? it->second : nullptr;
        if (!a) {
            if (dynMine.erase(d.id)) dynHold[d.id] = s.ticks + 90; // было отражение и пропало здесь (шар взят): хост ещё не узнал, подождать
            if (auto h = dynHold.find(d.id); h != dynHold.end()) { if (s.ticks < h->second) continue; dynHold.erase(h); }
            const float pos[3]{d.x, d.y, d.z};
            a = static_cast<Byte*>(reinterpret_cast<void*(__fastcall*)(void*, int, const float*, float, int)>(s.base + SpawnActorRva)(s.base + FactoryRva, d.type, pos, d.yaw, d.sector));
            if (!a) continue;
            *reinterpret_cast<int*>(a + 0x180) = static_cast<int>(DynTag | (d.id & 0x00ffffffu));
            dynMine[d.id] = s.ticks; local[d.id] = a;
            static unsigned logged = 0; if (++logged <= 40) note("world_dynamic_spawned", "\"id\":%u,\"type\":%u,\"pos\":[%.0f,%.0f,%.0f]", d.id, d.type, d.x, d.y, d.z);
        }
        if (d.scale > 0 && d.scale < 16) *reinterpret_cast<float*>(a + 0x124) = d.scale; // шар двойного размера (A_RandomItem: Scale 2) — и больше даёт
        if (aiType(d.type)) {
            Enemy e; e.x = d.x; e.y = d.y; e.z = d.z; e.vx = d.vx; e.vy = d.vy; e.vz = d.vz; e.yaw = d.yaw; e.hp = d.hp; e.sector = d.sector; e.aiState = d.aiState; e.anim = d.anim; e.frame = d.frame;
            steerEnemy(a, e);
        } else {
            const float dx = d.x - read<float>(a, 0x18), dy = d.y - read<float>(a, 0x1c), dz = d.z - read<float>(a, 0x20);
            if (dx * dx + dy * dy + dz * dz > 4.0f) {
                if (read<int>(a, 0x3c) != d.sector) teleport(a, d.x, d.y, d.z, d.sector);
                else { *reinterpret_cast<float*>(a + 0x18) = d.x; *reinterpret_cast<float*>(a + 0x1c) = d.y; *reinterpret_cast<float*>(a + 0x20) = d.z; }
            }
        }
    }
    // Хост перестал присылать (враг убран, шар подобран кем-то): убрать отражение. 30 тиков = 10 снимков врагов подряд.
    for (auto& [id, a] : local) {
        auto seenIt = dynSeen.find(id);
        if (seenIt == dynSeen.end() || s.ticks - seenIt->second > 30) { removeActor(a); dynMine.erase(id); if (seenIt != dynSeen.end()) dynSeen.erase(seenIt); }
    }
    for (auto it = dynSeen.begin(); it != dynSeen.end();) { if (s.ticks - it->second > 300) it = dynSeen.erase(it); else ++it; }
    for (auto it = dynHold.begin(); it != dynHold.end();) { if (s.ticks > it->second + 300) it = dynHold.erase(it); else ++it; }
}
void cullLocalDynamic() {
    if (!thread() || !s.installed || !s.client || !s.active || !replicaOn()) return;
    buildDynDefs();
    if (dynDefCount <= 0) return;
    unsigned culled = 0;
    forEachActor([&](Byte* a, int) {
        if (read<int>(a, 0x180) >= 0 || read<Byte>(a, 0x40) || dynTypeOf(a) < 0) return false; // карта, отражение (метка > 0) или уже убран
        removeActor(a); ++culled;
        return false;
    });
    static unsigned logged = 0; if (culled && ++logged <= 30) note("world_dynamic_culled", "\"count\":%u", culled);
}
bool popSwitch(int& face, int& event) {
    if (!s.switchCount) return false;
    const auto v = s.switchQueue[s.switchHead]; s.switchHead = (s.switchHead + 1) % s.switchQueue.size(); --s.switchCount;
    face = v.first; event = v.second;
    return true;
}
bool applySwitch(int faceIdx, int evIdx) {
    if (!thread() || !s.installed || !s.client || !s.active || !s.remoteTrigger || faceIdx < 0 || evIdx < 0) return false;
    auto* faces = read<Byte*>(s.base, FacesRva); auto* evs = read<Byte*>(s.base, EventsRva);
    if (!faces || !evs || static_cast<std::uint64_t>(faceIdx) >= read<std::uint64_t>(s.base, FaceCountRva) ||
        static_cast<std::uint64_t>(evIdx) >= read<std::uint64_t>(s.base, EventCountRva)) return false;
    auto* face = faces + static_cast<std::size_t>(faceIdx) * FaceStride; auto* ev = evs + static_cast<std::size_t>(evIdx) * 8;
    const int type = read<std::int16_t>(ev);
    if (read<std::int16_t>(face, 0x20) != evIdx || type < 200 || type > 203) { note("world_switch_refused", "\"face\":%d,\"event\":%d,\"type\":%d", faceIdx, evIdx, type); return false; }
    static const float zero[3]{};
    ForeignScope foreign; s.switchReplay = true;
    const bool changed = reinterpret_cast<UseSwitchFn>(s.base + UseSwitchRva)(face, ev, zero) != 0;
    s.switchReplay = false;
    note("world_switch_applied", "\"face\":%d,\"event\":%d,\"changed\":%s", faceIdx, evIdx, changed ? "true" : "false");
    return changed;
}
int collectPlatforms(Platform* out, int max) {
    if (!thread() || !s.installed || s.client) return 0;
    scanPlatforms();
    int n = 0;
    for (const int sec : s.floatSectors) if (n < max) if (auto* p = sectorThinker(sec, FloatingVtable)) out[n++] = {static_cast<std::int16_t>(sec), 1, static_cast<std::uint32_t>(read<int>(p, 0xe8))};
    for (const int sec : s.padSectors) if (n < max) if (auto* p = sectorThinker(sec, DropPadVtable)) out[n++] = {static_cast<std::int16_t>(sec), 2, static_cast<std::uint32_t>(read<int>(p, 0xf0))};
    return n;
}
void applyPlatforms(const Platform* in, int count) {
    if (!thread() || !s.installed || !s.client || !s.active) return;
    static unsigned logged = 0;
    for (int i = 0; i < count; ++i) {
        const auto& p = in[i];
        if (p.kind == 1) {
            auto* fp = sectorThinker(p.sector, FloatingVtable);
            if (!fp) continue;
            const int local = read<int>(fp, 0xe8), host = static_cast<int>(p.value);
            if (std::abs(local - host) <= 6) continue;
            *reinterpret_cast<int*>(fp + 0xe8) = host + 3; // ~ полпути сети; следующий тик платформы сам передвинет сектор на разницу
            if (++logged <= 40) note("world_platform_synced", "\"sector\":%d,\"local_tick\":%d,\"host_tick\":%d", p.sector, local, host);
        } else if (p.kind == 2) {
            auto* pad = sectorThinker(p.sector, DropPadVtable);
            if (!pad) continue;
            const int local = read<int>(pad, 0xf0), host = static_cast<int>(p.value);
            if (local == 0 && (host == 4 || host == 1 || host == 2)) padMove(pad, 4, 8);      // у хоста площадка пошла вниз: запустить (отсчёт и падение — сама)
            else if (local == 2 && (host == 3 || host == 0)) padMove(pad, 3, 6);            // у хоста поднята рычагом: поднять
            else continue;
            if (++logged <= 40) note("world_drop_pad_synced", "\"sector\":%d,\"local_state\":%d,\"host_state\":%d", p.sector, local, host);
        }
    }
}
void resetWorld() {
    if (!thread()) return;
    s.switchHead = s.switchCount = 0; s.platformsScanned = false; s.floatSectors.clear(); s.padSectors.clear();
    s.enemySeen.clear(); s.goneHead = s.goneCount = 0; s.pickupHead = s.pickupCount = 0;
    hostDynIds.clear(); dynSeen.clear(); dynMine.clear(); dynHold.clear();
    for (auto& l : s.lifts) l.sector = -1;
    s.doors.clear(); s.reopen.clear();
    s.pickupAsked.clear(); s.askHead = s.askCount = 0; s.touchAsked.clear(); s.touchHead = s.touchCount = 0; s.hitHead = s.hitCount = 0; s.selfHead = s.selfCount = 0;
    s.lastCheckpoint = -1; s.cpHead = s.cpCount = 0;
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
