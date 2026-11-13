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
// reopen: дверь у хоста пошла обратно вверх (под ней стоял игрок или враг): получатель, у которого она ещё закрывается, откроет её снова, как только
// она закроется (поймать чужой мовер на ходу нельзя; новый мовер на занятом секторе — второй мыслитель на том же секторе).
struct Mover { std::uint8_t cls{}, type{}; std::int16_t sector{}; bool reopen{}; };
constexpr std::uint32_t MoverReopen = 0x8000; // бит в eventValue (cls<<8|type)
inline Mover moverFromWire(std::uint32_t value, std::uint32_t sector) {
    return {static_cast<std::uint8_t>((value >> 8) & 0x7f), static_cast<std::uint8_t>(value), static_cast<std::int16_t>(sector), (value & MoverReopen) != 0};
}
inline std::uint32_t moverToWire(const Mover& m) { return (static_cast<std::uint32_t>(m.cls) << 8) | m.type | (m.reopen ? MoverReopen : 0u); }

// Провод. Хост -> клиент: BridgeWorldEvent (MSG_WORLD_EVENT_CONFIRM). Клиент -> хост: BridgeInteractionRequest
// (MSG_INTERACTION_REQUEST), interactionType = Req*, поля как ниже.
constexpr std::uint16_t EvMover = 1;      // EVENT_DOOR_STATE_CHANGE: target=sector, eventValue=cls<<8|type
constexpr std::uint16_t EvKeys = 3;       // EVENT_PICKUP_COLLECTED: eventValue=маска ключей команды (биты 0..3)
constexpr std::uint8_t ReqMover = 0x11;   // target=sector, generation=cls<<8|type
// 0x12 (ReqKeys) — снят на этапе 5 переделки: ключ своего игрока идёт через ReqTouch (TouchKey).
// Веха 4: общие достижения Steam. Любое срабатывание нативного SetAchievement (0x184110) на одной стороне идёт второй (ординал 0..19
// по docs/63_S6_1_ACHIEVEMENT_SPEC.md), та повторяет его у себя нативно: достижение получают все, за действие любого игрока.
constexpr std::uint16_t EvAchievement = 4; // host -> client: eventValue=ординал
constexpr std::uint8_t ReqAchievement = 0x13; // client -> host: target=ординал
// Веха 4: сюжетные сцены уровня. Корневой запуск скрипта уровня триггером карты (CallDelayedMapScript 0x1f4a60 из диспетчера событий:
// пересечение сектора, рычаг, E на стене) идёт второй стороне; та запускает тот же скрипт у себя с инициатором = свой игрок. Цепочки
// скрипта (script 1 -> 2, 4, 5 ...) каждая сторона достраивает сама; скрипты уровня — по их собственным флагам (bScript1Triggered).
constexpr std::uint16_t EvScript = 5;      // host -> client: eventValue=номер скрипта
constexpr std::uint8_t ReqScript = 0x14;   // client -> host: target=номер скрипта
// Любой игрок, дошедший до выхода, переводит на следующий уровень всех: клиент, чья игра начала смену карты (ChangeMap) или концовку, сообщает хосту
// имя карты без "maps/" и ".MAP" (до 8 символов A-Z0-9_ в двух u32: target = первые 4 байта, generation = следующие 4) или "STATE6"/"STATE7";
// хост вызывает то же нативное действие и приказом (MapChange) ведёт за собой остальных клиентов.
constexpr std::uint8_t ReqMap = 0x15;
// Разрушаемые предметы (урны/вазы, из которых бывают и взрывные, открывающие стены): тот, кто разбил, сообщает номер актора карты (+0x180); остальные
// добивают у себя тот же актор нативным уроном — осколки, взрыв, события карты и стены получаются их собственной логикой.
constexpr std::uint16_t EvBreak = 6;       // host -> client: eventValue=номер актора карты
constexpr std::uint8_t ReqBreak = 0x16;    // client -> host: target=номер актора карты
// Звуки игрока слышны всем. Все звуки тела и оружия (прыжок pjump01..05, боль, смерть, всплеск, выстрел, взмах и удар мачете, поднять/опустить
// оружие, огнемёт по кругу) игра играет нативным PlaySound на акторе СВОЕГО игрока — у себя, где его оружие и прыжок и считаются. Каждая
// машина пересылает звуки своего игрока номером из таблицы PlayerSounds (world_sync.cpp): клиент -> хост ReqSound, хост -> всем EvSound
// (source = слот); получатель играет тот же звук у прокси этого игрока (позиционно). Родные звуки тел клиентов на хосте глушатся (их заменяет
// звук самого клиента). sound = номер | SoundLoop (зацикленный, огнемёт) | SoundStop (остановить зацикленные).
constexpr std::uint16_t EvSound = 8;       // host -> client: source=слот, eventValue=sound
constexpr std::uint8_t ReqSound = 0x17;    // client -> host: target=sound
constexpr std::uint32_t SoundLoop = 0x100, SoundStop = 0x200;
std::uint32_t popSound();                  // звук своего игрока, сыгранный здесь с прошлого вызова; 0xffffffff — нет
void playPlayerSound(unsigned char* actor, std::uint32_t sound); // сыграть пересланный звук у актора (прокси другого игрока)
// Враг карты, исчезнувший у хоста (разорван взрывом, убран скриптом): иначе у клиента он стоял бы живым. Хост сообщает его номер, клиент убирает.
constexpr std::uint16_t EvEnemyGone = 9;   // host -> client: eventValue=номер актора карты
// Шар здоровья/патронов карты, подобранный у хоста любым игроком: у остальных он исчезает (иначе у них он оставался и брался «впустую»).
constexpr std::uint16_t EvPickup = 10;     // host -> client: eventValue=номер актора карты
// Враг погиб у хоста: клиент сразу проигрывает ту же родную смерть (обычную или «разрывную» — после неё актор убирает себя сам). Раньше смерть
// приходила только снимком здоровья, а разорванный враг исчезал у хоста в тот же тик и снимка не было: у клиента он замирал и пропадал.
constexpr std::uint16_t EvEnemyDeath = 13; // host -> client: eventValue=номер врага на проводе (как у ReqHit), target=1 — «разрывная» смерть
bool popEnemyDeath(int& id, bool& extreme); // хост: враг, погибший здесь с прошлого вызова
bool applyEnemyDeath(int id, bool extreme); // клиент: родная смерть своего отражения этого врага; false — его нет / уже мёртв
int popEnemyGone();                        // хост: номер врага карты, пропавшего из мира с прошлого сбора; -1 — нет
bool applyEnemyGone(int id);               // клиент: убрать у себя врага карты с этим номером
int popPickup();                           // хост: номер подобранного шара карты; -1 — нет
bool applyPickup(int id);                  // клиент: убрать у себя шар карты с этим номером (свой запрос: с откликом подбора)
// Шары здоровья/патронов игрока клиента берёт хост. Касание своего игрока у клиента шар не берёт, а просит хост; хост отдаёт его телу этого игрока
// нативным OnTouch (через P14: здоровье/патроны именно этому игроку) и убирает у всех EvPickup (порождённый шар — значением метки отражения
// 0x60000000|номер), взявший клиент по EvPickup на свой запрос показывает подбор. Тела клиентов на хосте сами шаров не берут.
// 0x18 (ReqPickup) — снят на этапе 5 переделки: шары идут через ReqTouch (TouchHealth/TouchAmmo).
int takePickup(int id, unsigned char* body); // хост: отдать шар телу игрока; 0 — нет (уже нет / далеко / не нужен), 1 — шар карты, 2 — порождённый
// Сложность хоста: от неё полоса здоровья HUD (100 или 200 на анкх) и сколько даёт шар. Хост сообщает свою, клиент ставит её у себя.
constexpr std::uint16_t EvDifficulty = 12; // host -> client: eventValue=сложность (GameInstance +0x1d8)
int difficulty();                          // -1 — профиль EXE не совпал
// Потолок здоровья игрока (GiveHealth 0x1e0850): (анкхи Player+0x84 + 1) * (100 на сложностях 1-2, иначе 200). Тело, появившееся через мод
// (респаун P16, тело клиента у хоста, перенос между картами), получало 200 и на «Фараоне»: две полосы на HUD, шары здоровья не брались.
int healthCap(const unsigned char* player);
// Выше потолка -> потолок (без урона); floor — потолок не ниже этого (клиент: здоровье от хоста — его анкхи могут быть не те). true — изменено.
bool clampHealth(unsigned char* actor, unsigned char* player, int floor = 0);
// Попадания игрока клиента по врагам хоста: клиент у себя врагу урон не наносит (иначе враг умирал только у него), а сообщает хосту;
// хост наносит тот же нативный урон от тела этого игрока, а попадания самого тела (и его снарядов) по врагам у хоста не считает.
constexpr std::uint8_t ReqHit = 0x19;      // client -> host: target=номер врага (карты или 0x60000000|номер порождённого), generation=урон | тип<<16
bool popHit(int& id, int& amount, int& kind);                    // клиент
// Урон окружения своему игроку клиента (утопление, лава, падение: Damage без источника) считает клиент, хост наносит его телу этого игрока,
// а свой такой урон телам клиентов не начисляет (тело ставится в позу клиента и само не тонуло).
constexpr std::uint8_t ReqSelfDamage = 0x1a; // client -> host: target=урон, generation=тип
bool popSelfDamage(int& amount, int& kind);               // клиент
bool applySelfDamage(unsigned char* body, int amount, int kind); // хост (и отладочная смерть тела клиента)
void debugSelfDamage(unsigned char* own); // тест (клиент): PSE_DEBUG_SELF_DAMAGE_FILE — урон без источника своему игроку
// Скарабеи: у каждого игрока свой последний скарабей, у него игрок и появляется после смерти (респаун P16).
// 0x1b (ReqCheckpoint) — снят на этапе 5 переделки: скарабей своего игрока идёт через ReqTouch (TouchScarab).
// Переделка, этап 2: касания своего игрока у клиента — ОДИН канал. Клиент сообщает, чего коснулся его игрок (вид + номер актора на
// проводе: номер карты или SphereMirrorTag|номер порождённого у хоста), хост повторяет то же касание нативным OnTouch телом этого игрока.
constexpr std::uint8_t ReqTouch = 0x1c;      // client -> host: target=номер актора, generation=вид касания
enum TouchKind : std::uint8_t { TouchHealth = 0, TouchAmmo = 1, TouchKey = 2, TouchWeapon = 3, TouchScarab = 4, TouchKinds = 5 };
struct Touch { int id{-1}; std::uint8_t kind{}; };
bool popTouch(Touch& out);                   // клиент: касание своего игрока, ещё не отправленное; false — нет
int applyTouch(const Touch& t, unsigned char* body); // хост: тело игрока касается актора нативно (не скарабей); 0 — нет, 1 — да, 2 — порождённый шар
void debugTouch(unsigned char* own);
void debugRemove(); // тест (хост): PSE_DEBUG_REMOVE="номер[,номер]" + PSE_DEBUG_REMOVE_FILE — убрать акторы карты родным Remove (как скрипт/взрыв) // тест (клиент): PSE_DEBUG_TOUCH_CLIENT="вид,номер[;вид,номер]" + PSE_DEBUG_TOUCH_FILE — свой игрок касается актора карты родным OnTouch
int popCheckpoint();                        // свой игрок коснулся скарабея: его номер; -1 — нет
bool checkpointPlace(int id, float& x, float& y, float& z, float& yaw, int& sector); // где стоит скарабей карты с этим номером
bool applyHit(int id, int amount, int kind, unsigned char* body); // хост
bool applyDifficulty(int value);           // клиент
void resetWorld();                         // новый мир (смена карты, Restart Level): забыть учёт врагов, шаров и отражений прошлого
// Рычаг (стенной выключатель, события 200..203), нажатый у хоста (хостом или телом клиента): у остальных меняется только его текстура (родной
// UseWallSwitch с нулевой точкой: все полигоны грани с текстурой «выключен»), а SendRemoteTrigger на это время подавлен — двери/лифты рычага
// приходят своими событиями. Нажавший клиент уже нажал его у себя: у него совпадений нет, ничего не меняется.
constexpr std::uint16_t EvSwitch = 11;     // host -> client: target=номер события карты, eventValue=номер грани
bool popSwitch(int& face, int& event);     // хост: рычаг, нажатый здесь (не повтором) с прошлого вызова
bool applySwitch(int face, int event);     // клиент: поставить рычагу текстуру «нажат»
// Плавающие платформы (kexSlaveMoverFloatingPlatform) идут по счётчику тиков с загрузки карты — у каждой машины свой, платформы были в разной фазе.
// Падающие площадки (kexSlaveMoverDropPad) падают, когда на них стоит СВОЙ игрок (Puppet), другой игрок — прокси, и у получателя площадка не падала.
// Хост рассылает каждые 15 тиков счётчик каждой платформы и состояние каждой площадки (снимок мира, тип 4), клиент подгоняет: счётчик при
// расхождении больше 6 тиков (двигает платформу сам движок), площадку запускает или поднимает тем же кодом, что движок (поздно вошедший тоже).
struct Platform { std::int16_t sector{}; std::uint8_t kind{}; std::uint32_t value{}; }; // kind 1 = платформа (value = тик), 2 = площадка (value = состояние)
int collectPlatforms(Platform* out, int max);       // хост
// Переделка, этап 4: высоты секторов хоста (пол/потолок). Хост: каждый сектор, который в этом мире хоть раз двигался (moving = только те, что
// двигаются сейчас); клиент доводит свой сектор до высот хоста родным сдвигом плоскости, если его собственный механизм в секторе не работал
// последние 1,5 с (локальную анимацию двери/лифта, запущенную тем же действием, не перебивает).
struct SectorState { std::int16_t sector{}; float floor{}, ceiling{}; };
int collectSectors(SectorState* out, int max, bool movingOnly); // хост
void applySectors(const SectorState* in, int count);            // клиент
void applyPlatforms(const Platform* in, int count); // клиент
bool install(unsigned char* base, std::uint32_t simTid, Logger log, bool host); // bootstrap APC на Sim-потоке, MH_Initialize уже сделан
// Каждый тик: true — идёт согласованный геймплей (сессия готова). false очищает очереди и выключает перехват.
void setActive(bool on);
void prepareHud(); // HUD-перехваты один раз, как только игра идёт (у хоста — ещё до входа клиента, чтобы вход не стоил тика)
int popBreak();                      // номер актора карты, разбитого здесь (не репликой); -1 — нет
bool applyBreak(int id);             // добить у себя актор карты с этим номером; false — уже было / не найден
// Хост: проверка «это актор тела другого игрока» (не P1): рамка урона и сообщения о подборе чужих тел на HUD хоста подавляются.
void setRemoteBodyTest(bool (*test)(const void* actor));
// Всплывающие надписи/вспышки HUD (сообщение, рамка урона, вспышка подбора) принадлежат тому игроку, чьё действие их вызвало. Пока выполняется чужое
// действие (обновление тела другого игрока, актор такого тела, повтор чужого события) HUD-вызовы пропускаются. kind: 0 нет, 3 = чужое действие.
int setHudKind(int kind);   // возвращает прежний
void setLocalActorGetter(unsigned char* (*get)()); // хост: актор своего игрока (P1) — для сюжетных сцен
bool popMover(Mover& out);          // мовер, запущенный игрой здесь (не репликой)
bool applyMover(const Mover& m);    // повторить нативно; false — сектор занят/неверен
// Повторить подбор ключей для игрока (Player*, Puppet*): нативный OnTouch по актору-ключу, иначе прямая запись бита.
bool applyKeys(unsigned char* player, unsigned char* puppet, std::uint16_t mask);
// Достижения: маска ординалов, сработавших здесь (не репликой) с прошлого вызова; повтор уже виденного ординала не возвращается.
std::uint32_t popAchievements();
// Скрипт уровня, запущенный здесь триггером карты (не репликой и не цепочкой): номер и место, где стоял запустивший игрок.
struct ScriptCall { int num{-1}; float x{}, y{}, z{}; bool pos{}; };
ScriptCall popScript();                       // num < 0 — нет
// Положение запустившего в двух u32 (int16 на координату, единица мира): по сети идёт вместе с номером скрипта.
inline std::uint32_t packXY(float x, float y) { return (static_cast<std::uint32_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(x))) << 16) | static_cast<std::uint16_t>(static_cast<std::int16_t>(y)); }
inline std::uint16_t packZ(float z) { return static_cast<std::uint16_t>(static_cast<std::int16_t>(z)); }
inline ScriptCall unpackScript(int num, std::uint32_t xy, std::uint16_t z) {
    ScriptCall c; c.num = num; c.pos = xy != 0;
    c.x = static_cast<float>(static_cast<std::int16_t>(xy >> 16)); c.y = static_cast<float>(static_cast<std::int16_t>(xy & 0xffff)); c.z = static_cast<float>(static_cast<std::int16_t>(z));
    return c;
}
// Запустить тот же скрипт у себя; false — уже запускался здесь в последние 3 с / нет перехвата / сцену уже играли в этом мире / не удалось собрать игрока у места сцены.
// Сюжетная сцена (Рамзес, TOMB и TOMBEND, скрипт 1) двигает игрока-инициатора к своей точке без перелинковки сектора: игрок вдали от сцены видел бы чёрный экран.
// Поэтому у такой сцены игрока сначала переносят туда, где стоял запустивший (родная перелинковка), и играют один раз за мир.
bool applyScript(int num, unsigned char* instigator, const ScriptCall* from = nullptr);
void debugScript(unsigned char* instigator); // тест: PSE_DEBUG_SCRIPT[_CLIENT]=<номер> — как если бы игрок пересёк триггер этого скрипта
bool applyAchievement(unsigned ordinal);    // повторить у себя нативным SetAchievement (идемпотентно); false — неизвестный ординал/уже было
void debugAchievement();                    // тест: PSE_DEBUG_ACH=<ординал> — один раз вызвать SetAchievement через хук (с PSE_ACH_DRYRUN=1 Steam не трогается)
void census(const char* why);        // диагностика: события карты и все акторы мира в журнал (классы, позиции, здоровье)
// Тестовые хуки смоук-прогона (в игре не используются): PSE_DEBUG_MOVER[_CLIENT]="класс,type,сектор" | "auto" | "auto2" — один
// вызов SpawnMover через хук (auto = первая дверь type 1, auto2 = вторая); PSE_DEBUG_KEY[_CLIENT]=биты — подобрать ключ без актора.
void debugSpawn();
void debugKeys(unsigned char* player);
void debugKillEnemy(); // тест (хост): PSE_DEBUG_KILL=<spawn id> — убить врага вызовом движка
void debugHit(unsigned char* own); // тест (клиент): PSE_DEBUG_HIT_CLIENT="id,урон" + PSE_DEBUG_HIT_FILE — попадание своего игрока во врага карты
// Снаряды врагов (Веха 3): хост рассылает их (тип по дефиниции актора), клиент ставит у себя отражение с меткой и убирает пропавшие.
// owner: враг, выпустивший снаряд (номер на проводе, как у ReqHit: номер карты или 0x60000000|номер порождённого; -1 — ловушка/никто): отражение у
// клиента получает того же владельца и, как у хоста, не сталкивается со стрелком.
struct Projectile { std::uint32_t id{}; std::uint16_t type{}, anim{0xffff}; float x{}, y{}, z{}, vx{}, vy{}, vz{}, yaw{}; std::int16_t sector{}; std::int32_t owner{-1}; };
int collectProjectiles(Projectile* out, int max);        // хост: снаряды врагов в мире
void applyProjectiles(const Projectile* in, int count);  // клиент: применить (вызывать на каждый пакет, count может быть 0)
void cullLocalProjectiles();                             // клиент: удалить снаряды врагов, выпущенные локальным (заглушённым) ИИ
// Порождённые во время игры враги и шары: ваза (A_RandomVaseItem: скорпион, оса, шар здоровья/патронов, карта, сила), смерть врага (A_RandomItem:
// шар здоровья/патронов), Селкис (скорпионы). Случайность у каждой машины своя, поэтому у клиента такие актёры — только отражения хоста: свои он
// убирает, хостовы ставит по типу (как снаряды) с меткой в +0x180 и ведёт снимками; пропавшее у хоста убирается.
struct Dynamic { std::uint32_t id{}; std::uint16_t type{}, anim{0xffff}, aiState{}, frame{}; float x{}, y{}, z{}, vx{}, vy{}, vz{}, yaw{}, scale{1}; std::int16_t hp{}, sector{}; };
int collectDynamic(Dynamic* out, int max);       // хост: порождённые враги и шары (не актёры карты)
void applyDynamic(const Dynamic* in, int count); // клиент: применить (на каждый пакет снимка, count может быть 0)
void cullLocalDynamic();                         // клиент: убрать порождённых здесь врагов, шары и их вспышки появления
void checkEnemies();
void debugSpawnAi(unsigned char* anchor); // тест: PSE_DEBUG_SPAWN_AI[_CLIENT]="число" + PSE_DEBUG_SPAWN_AI_FILE — порождённые враги во время игры                             // клиент, раз в 2 с: сверка врагов со снимками хоста (world_enemy_mismatch)
void debugFireball(unsigned char* target);               // тест (хост): PSE_DEBUG_FIREBALL="дистанция,скорость" + PSE_DEBUG_FIREBALL_FILE — огненный шар Анубиса в цель; PSE_DEBUG_FIREBALL_OWNER=<номер врага> — шар «выпускает» этот враг (появляется в нём, он владелец)
void debugTrapThrash();                                  // тест (хост): PSE_DEBUG_TRAP_THRASH=<номер ловушки> + PSE_DEBUG_TRAP_FILE — каждый тик выключать и включать ловушку карты (как двое игроков в секторах включения и выключения)
void debugCensusOnFile(); // тест: PSE_DEBUG_CENSUS_FILE=<путь> — переписать акторы мира, когда файл появится (один раз)
void debugPullEnemy(unsigned char* anchor); // тест (хост): PSE_DEBUG_PULL_ENEMY="id,дистанция" — поставить врага перед актором anchor (по его взгляду)
// Нативный телепорт актора (как делает прокси): позиция, LinkSector при смене сектора, кэш пола/потолка, сброс интерполянтов.
// Проверяет, что точка лежит в секторе. false — ничего не изменено.
bool teleport(unsigned char* actor, float x, float y, float z, int sector);
int findSector(float x, float y, float z); // первый сектор, содержащий точку (-1 — нет)
float floorAt(float x, float y, float z, int sector);
// Тест: PSE_DEBUG_TELEPORT[_CLIENT]="x,y,yaw" — один раз поставить локального игрока в точку на полу (сектор находится сам).
void debugTeleport(unsigned char* actor);
void debugGoWeapon(unsigned char* actor, unsigned long long tick); // test: PSE_DEBUG_GO_WEAPON(_CLIENT)=<world tick> - own player onto an unowned map weapon
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
