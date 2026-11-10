// Милестоун 2: синхронизация мира (двери/лифты/платформы, общие ключи). Один модуль для обеих ролей.
//
// Идея: обе стороны крутят полный мир локально, поэтому дверь не «двигаем вершины», а повторяем то же самое нативное
// действие движка — kexActorFactory::SpawnMover(className, type, sector) (RVA 0x1d4e20, фабрика 0x5a10a0). Любой вызов,
// сделанный игрой на этой машине (E на двери, шаг на триггерный сектор, рычаг), хук складывает в очередь; сессия отправляет
// его второй стороне, та вызывает ту же функцию. Повтор «эхо» подавляется флагом replaying; уже движущийся сектор движок
// отвергает сам (SF_SPECIAL).
//
// Ключи: общие на команду. Подбор ключа (kexSlavePickupKey::OnTouch, 0x1d8a00) хук замечает по приросту битов
// Player+0x98; сторона-получатель повторяет подбор нативно на своём акторе-ключе (звук, надпись, вспышка, удаление).
//
// Всё выполняется только на Sim-потоке игры (поток, который крутит StateTick). Указатели по сети не ходят.
#pragma once
#include <cstdint>

namespace pse::world {
using Logger = void (*)(const char* event, const char* fmt, ...);

// Тип мовера на проводе: класс (индекс в kClass) + type из mapEvent_t (1, 3..6, 8, 9, 0x15, ...).
struct Mover { std::uint8_t cls{}, type{}; std::int16_t sector{}; };

// Провод. Хост -> клиент: BridgeWorldEvent (MSG_WORLD_EVENT_CONFIRM). Клиент -> хост: BridgeInteractionRequest
// (MSG_INTERACTION_REQUEST), interactionType = Req*, поля как ниже.
constexpr std::uint16_t EvMover = 1;      // EVENT_DOOR_STATE_CHANGE: target=sector, eventValue=cls<<8|type
constexpr std::uint16_t EvKeys = 3;       // EVENT_PICKUP_COLLECTED: eventValue=маска ключей команды (биты 0..3)
constexpr std::uint8_t ReqMover = 0x11;   // target=sector, generation=cls<<8|type
constexpr std::uint8_t ReqKeys = 0x12;    // target=маска ключей, подобранных клиентом

bool install(unsigned char* base, std::uint32_t simTid, Logger log, bool host); // bootstrap APC на Sim-потоке, MH_Initialize уже сделан
// Каждый тик: true — идёт согласованный геймплей (сессия готова). false очищает очереди и выключает перехват.
void setActive(bool on);
bool popMover(Mover& out);          // мовер, запущенный игрой здесь (не репликой)
std::uint16_t popKeysGained();      // ИЛИ битов ключей, подобранных здесь с прошлого вызова
bool applyMover(const Mover& m);    // повторить нативно; false — сектор занят/неверен
// Повторить подбор ключей для игрока (Player*, Puppet*): нативный OnTouch по актору-ключу, иначе прямая запись бита.
bool applyKeys(unsigned char* player, unsigned char* puppet, std::uint16_t mask);
void census(const char* why);        // диагностика: события карты и все акторы мира в журнал (классы, позиции, здоровье)
// Тестовые хуки смоук-прогона (в игре не используются): PSE_DEBUG_MOVER[_CLIENT]="класс,type,сектор" | "auto" | "auto2" — один
// вызов SpawnMover через хук (auto = первая дверь type 1, auto2 = вторая); PSE_DEBUG_KEY[_CLIENT]=биты — подобрать ключ без актора.
void debugSpawn();
void debugKeys(unsigned char* player);
void debugKillEnemy(); // тест (хост): PSE_DEBUG_KILL=<spawn id> — убить врага вызовом движка
// Снаряды врагов (Веха 3): хост рассылает их (тип по дефиниции актора), клиент ставит у себя отражение с меткой и убирает пропавшие.
struct Projectile { std::uint32_t id{}; std::uint16_t type{}, anim{0xffff}; float x{}, y{}, z{}, vx{}, vy{}, vz{}, yaw{}; std::int16_t sector{}; };
int collectProjectiles(Projectile* out, int max);        // хост: снаряды врагов в мире
void applyProjectiles(const Projectile* in, int count);  // клиент: применить (вызывать на каждый пакет, count может быть 0)
void cullLocalProjectiles();                             // клиент: удалить снаряды врагов, выпущенные локальным (заглушённым) ИИ
void debugFireball(unsigned char* target);               // тест (хост): PSE_DEBUG_FIREBALL="дистанция,скорость" + PSE_DEBUG_FIREBALL_FILE — огненный шар Анубиса в цель
void debugCensusOnFile(); // тест: PSE_DEBUG_CENSUS_FILE=<путь> — переписать акторы мира, когда файл появится (один раз)
void debugPullEnemy(unsigned char* anchor); // тест (хост): PSE_DEBUG_PULL_ENEMY="id,дистанция" — поставить врага перед актором anchor (по его взгляду)
// Нативный телепорт актора (как делает прокси): позиция, LinkSector при смене сектора, кэш пола/потолка, сброс интерполянтов.
// Проверяет, что точка лежит в секторе. false — ничего не изменено.
bool teleport(unsigned char* actor, float x, float y, float z, int sector);
int findSector(float x, float y, float z); // первый сектор, содержащий точку (-1 — нет)
float floorAt(float x, float y, float z, int sector);
// Тест: PSE_DEBUG_TELEPORT[_CLIENT]="x,y,yaw" — один раз поставить локального игрока в точку на полу (сектор находится сам).
void debugTeleport(unsigned char* actor);
// Враги (Веха 3): хост авторитетен. Враг опознаётся по spawn id актора (+0x180, порядок карты, одинаков на обеих сторонах).
// Клиент вместо ИИ тикает базовый актор (анимация/физика), реакцию ИИ на урон игнорирует, а позицию, скорость, угол, здоровье,
// состояние и анимацию берёт из снимков хоста; без свежих снимков (>1.5 с) ИИ клиента работает как обычно.
struct Enemy { std::uint32_t id{}; float x{}, y{}, z{}, vx{}, vy{}, vz{}, yaw{}; std::int16_t hp{}, sector{}; std::uint16_t anim{0xffff}, aiState{}, frame{}; };
int collectEnemies(Enemy* out, int max);           // хост: все живые ИИ-акторы мира
void applyEnemies(const Enemy* in, int count);     // клиент: применить снимок (может вызываться по батчам)
bool replicaActive();                              // клиент: идёт репликация врагов хоста
// Свой игрок клиента (Веха 3): здоровье решает хост. Локальный урон по своему актору глушится (ловушки, случайные попадания),
// а здоровье из снимка хоста применяется нативным Damage (экран краснеет, звук, анимация боли/смерти) либо прямой записью.
void setFriendlyFire(bool on);                      // по умолчанию выключен (PSE_FRIENDLY_FIRE=1 при старте включает)
void setOwnPlayer(unsigned char* actor);           // клиент, каждый тик на Sim-потоке; nullptr — не глушить
// Применить здоровье хоста: true — что-то изменилось. silent — без нативного урона (первая синхронизация).
bool applyHostHealth(unsigned char* actor, int hostHp, bool silent);
struct SectorProbe { std::uint16_t flags{}; float floorD{}, ceilingD{}; bool valid{}; };
SectorProbe probe(int sector);
}
