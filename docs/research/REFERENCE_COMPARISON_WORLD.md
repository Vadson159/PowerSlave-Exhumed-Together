# Референсы: происхождение, мир, AI, события и сохранения

Срез локальных материалов: 2026-10-31. Все входные файлы прочитаны без изменения. Ничего из референсов не компилировалось и не запускалось. `capture_reference_world.py` — инструмент чтения и фиксации Evidence, а не код игрового движка.

Базовый каталог ссылок на исходники: `S:/_Vibe_Coding/PowerSlave_Exhumed_Together/`. Каждый Evidence JSON содержит абсолютный путь, размер и SHA-256 **полного исходного файла**, точные номера строк и ограниченные выдержки. Идентификаторы REFWORLD созданы исследователем; это результаты Python/PowerShell, не REA Evidence. В этом отчёте VERIFIED означает проверенный текст указанного локального файла. Любой перенос вывода на EXE Exhumed 2022 остаётся INFERRED или UNKNOWN до независимого анализа целевого артефакта.

## Что действительно находится в каталогах

| Референс | VERIFIED: наблюдаемый материал | Польза для 2022, INFERRED | Граница применения |
|---|---|---|---|
| `PowerslaveEX-master-source-code` | README:4 описывает неофициальный PSX remake на первой итерации Kex Engine 3.0. Там же сказано, что collision и AI написаны заново; формат уровней и спрайтов изменён. README:24–31 перечисляет SDL2, AngelScript, FFmpeg, OpenAL, Vorbis, PNG, zlib. | Ближайший из имеющихся нативных исходных референсов для поиска понятий player/puppet/actor, factory, map events и campaign. | Не исходники Exhumed 2022; строки README не доказывают побайтовое происхождение позднего EXE. Нельзя переносить C++ layout, ABI, адреса или формат save. |
| `SlaveDriver-Engine-master` | README:1 — Sega Saturn SlaveDriver; README:24–25 говорит об отсутствии игровых данных. README:19 заявляет GPL-3.0-or-later; это наблюдение о тексте лицензии. | Семантика секторных событий, флагов кампании, целей AI и словарь исходного консольного движка. | Сатурновские C/assembler, fixed point, внутренние форматы и API не дают Windows x64 offsets. Не путать с KEX. |
| `PSEPSX-main` | README:2 — мод и builder для PowerSlave Exhumed, приближающий игру к PlayStation. `PSEPSX.py`:49–50 задаёт `PowerslaveEX.kpf` и ожидаемый CRC32 `F93FFF7D`; :436 открывает ZIP; :442–446 применяет patches/data/preprocessing/scripts и пакует результат. | Наиболее полезный референс **модификации данных 2022**: реальные текстовые имена API и пути defs/scripts/mapInfo, которые можно искать в установленном KPF/EXE. | Не исходники PSX и не исходники KEX 2022. Код builder содержит операции записи/удаления; в исследовании не запускался. CRC32 внутри builder — ожидаемая автором версия, не измеренный хеш установленного KPF. |
| `Raze-master` | README:5 — fork Build games на технологиях GZDoom; :13 — Exhumed/PowerSlave модуль основан на PCExhumed. | Пример декомпозиции сериализации мира и разделения player-specific state/local presentation. | Другая ветвь игры, PC/Build. Наличие multiplayer-полей не подтверждает M0 в KEX. |
| `NBlood-master` | README:1–2 — reverse-engineered порты Build games; :42–51 — PCExhumed для **PC retail Exhumed/PowerSlave**, на EDuke32. | Пример массива игроков и различия local player/симуляция, event channels, модульной сериализации. | DOS/Build версия не совпадает с консольной кампанией и движком 2022. Данные `STUFF.DAT` и структуры Build не являются форматом KPF/KEX. |
| `PowerSlave_EX_-_CO-OP_Edition` | Локальная поставка состоит ровно из `anubis.exe`, `README.txt`, `mods/john.kpf`. README требует public beta 2015. | Отдельный артефакт для анализа того, как его автор реализовал coop в старом EX; полезен как сравнение после установления исходного build baseline. | В этой поставке нет C/C++ исходников. Открытость исходников модификации неизвестна. Функциональность не запускалась. Прямую применимость к 2022 не считать установленной. |

Источники таблицы: [REFWORLD_PROVENANCE](../evidence/REFWORLD_PROVENANCE.json), [REFWORLD_PSEPSX](../evidence/REFWORLD_PSEPSX.json), [REFWORLD_COOP_INVENTORY](../evidence/REFWORLD_COOP_INVENTORY.json). Юридическая совместимость лицензий, допустимость распространения оригинальных данных и правовой статус модификации не анализировались.

## Карта функций мира в PowerSlave EX 2015

Для всех строк ниже: адрес VA/RVA/file offset = **не применимо к исходному тексту**; хеш целевого EXE = **UNKNOWN**; SHA-256 исследованного source-файла и выдержка — [REFWORLD_EX2015_WORLD](../evidence/REFWORLD_EX2015_WORLD.json). Имена функций взяты из исходников референса, не присвоены функциям EXE 2022. Полный нативный calling convention не выводился из C++ текста.

| ID | Source и строки | VERIFIED: назначение и связи | Значение для coop / следующий вопрос |
|---|---|---|---|
| R15-W01 | `kex3_anubis/source/game/world.cpp:462–487`, `kexWorld::ReadActors` | Читает массив mapActor; при count=0 выдаёт сообщение о необходимости хотя бы player actor; для каждого actor с sector>=0 вызывает SpawnMapActor. | INFERRED: важен порядок player bind/actor spawn. Сам текст ошибки не доказывает ограничение «ровно один». |
| R15-W02 | `world.cpp:644–668`, `SpawnMapActor(mapActor_t*)` | Вычисляет координаты и угол, вызывает `cActorFactory->Spawn(type,x,y,z,angle,sector)`, затем `SetMapActor`. | INFERRED: factory и карта происхождения actor — узлы будущей репликации spawn. Нужен отдельный runtime/net ID, не адрес указателя. |
| R15-W03 | `world.cpp:674–720`, `LoadMap(const char*)` | Читает counts текстур, vertices/sectors/faces/polys/texcoords/events/actors; выделяет память, строит spatial data, настраивает CModel, затем читает/создаёт actors. | INFERRED: world-ready barrier должен учитывать завершение создания объектов и скриптов, а не один факт открытия файла. Формат 2022 UNKNOWN. |
| R15-W04 | `world.cpp:727–738`, `UnloadMap()` | `RemoveAllGameObjects`, `Player()->ClearActor`, `areaNodes.Destroy`, `CModel()->Reset`, purge world heap. | VERIFIED singleton cleanup в референсе. INFERRED: опасность dangling remote-player handles и необходимость invalidation по level epoch. |
| R15-W05 | `world.cpp:1098–1166`, `UseWallSpecial(kexPlayer*,mapFace_t*)`, `UseLockedDoor(kexPlayer*,mapEvent_t*)` | Выбирает тип события; ключ проверяет **переданный** player через CheckKey, затем создаёт door/lift mover. | VERIFIED: часть интерфейса принимает явного игрока, несмотря на singleton в другом коде. INFERRED: решать shared-key policy до hook, а событие открытия исполнять один раз у host. |
| R15-W06 | `world.cpp:1172–1206`, `UseWallSwitch(kexPlayer*,mapFace_t*,mapEvent_t*)` | Меняет текстуры полигонов переключателя, воспроизводит звук от actor переданного player и посылает remote trigger. | INFERRED: важна идемпотентность. Повторный сетевой запрос нельзя повторно исполнять как новое нажатие. |
| R15-W07 | `world.cpp:1291–1429`, `MoveScriptedSector`, `TriggerEvent`, `EnterSectorSpecial(kexActor*,mapSector_t*)` | Tag связывает events/movers; типы событий создают door/floor/lift, запускают delayed map scripts с actor-instigator. Одноразовый тип 52 очищает `sector->event=-1`; тип 63 не очищает. | INFERRED: trigger completion, delayed script state и mover progress — состояние мира. Синхронизации только позиций actor недостаточно. |
| R15-W08 | `world.cpp:1503–1534`, `TeleportEvent(kexActor*,mapEvent_t*)` | По совпадающему tag находит destination event, меняет origin/sector **переданного** actor. Для puppet вызывает singleton PlayLoop TeleportFlash. | VERIFIED: actor parameter и локальный эффект смешаны. INFERRED: перенос удалённого игрока не должен выдавать локальную вспышку host. |
| R15-W09 | `world.cpp:1540–1606`, `ArtifactEvent(kexActor*,mapEvent_t*)` | Проверяет что actor — puppet, получает player через `Owner()`, проверяет бит артефакта; ищет подходящий mapActor template и изменяет inventory/world. | INFERRED: Owner relation — полезная опора независимости персонажей. Нельзя делать global Player swap без проверки владения. |
| R15-W10 | `world.cpp:1780–1870`, `CheckActorsForRadialBlast` / начало `RadialDamage` | Перебирает затронутые actors/сектора, видимость и дистанцию, вызывает actor damage. | INFERRED: урон и взрыв должен рассчитывать host. На клиентах проигрывать подтверждённые эффекты, не повторять damage. |
| R15-A01 | `ai.cpp:126–173`, `kexAI::Tick()` | State machine idle/chase/pain/melee; умершую target сбрасывает; вызывает actor Tick. | INFERRED: ai state/target/таймеры нужно сохранить на authority; клиентские AI ticks могут породить расхождение. |
| R15-A02 | `ai.cpp:473–497`, `LookForTarget()` | Выбирает только `cLocal->Player()->Actor()`, проверяет death и sight. | VERIFIED single-player **выбор цели** в 2015. Это локальное предположение, не доказательство невозможности множества player actors. Для 2022 нужна своя xref/декомпиляция. |
| R15-A03 | `ai.cpp:198–242`, `OnDamage(kexActor*)` | Может назначить target от instigator; для projectile рассматривает owner через Target(), допускает AI/puppet. | VERIFIED: хранение target не ограничено одним фиксированным player. INFERRED: заменить target acquisition недостаточно без проверки re-targeting/death/visibility. |
| R15-P01 | `pickup.cpp:50–89`, `kexPickup::OnTouch` | Выключает solid/touchable, Remove, локальные сообщение/flash. | INFERRED: единственный pickup commit + отдельное local feedback; duplicate touch не должен выдать предмет дважды. |
| R15-P02 | `pickup.cpp:731–790`, `kexTeamDollPickup::OnTouch` / `Spawn` | OnTouch берёт `Owner()` из instigator, пишет TeamDolls/abilities; Spawn удаляет собранную doll, сверяясь с singleton Player. | VERIFIED mixed ownership в референсе. INFERRED: shared progress требует согласованного правила respawn/visibility для всех игроков. |
| R15-L01 | `game.cpp:1085–1118`, `ChangeMap(const char*)`, `LoadNewMap()` | Запрос выставляет GS_CHANGELEVEL и pendingMap; loader ищет activeMap, при restart восстанавливает persistentData, вызывает world LoadMap, затем GS_LEVEL. | INFERRED: переход должен быть transaction host с закрытием старого epoch и готовностью всех клиентов. |
| R15-L02 | `travelObject.cpp:62–108`, `kexTravelObject::OnTouch(kexActor*)` | Проверяет puppet-instigator, RequestExit/ending/menu; ScheduleWarpForNextMap идёт через instigator, выдача giveArtifacts — через **singleton** Player; возможен SaveGame. | VERIFIED: разные части перехода используют разные каналы владения. INFERRED: дублирование actor само по себе не делает переходы независимыми. |
| R15-L03 | `menu.cpp:1617–1649`, обработчик travel | `SavePersistentData`, SelectedMap, unlock текущей/следующей карт, SaveGame, GS_OVERWORLD. | INFERRED: world/campaign authoritative state, сетевое голосование — надстройка, не запуск этого обработчика на каждом клиенте. |
| R15-S01 | `game.cpp:1305–1345`, `SavePersistentData` / `RestorePersistentData` | Оружие, ammo, ankhs, artifacts, quest, dolls, abilities, health, currentWeapon берутся из одного player. | VERIFIED: snapshot одного campaign player в 2015. INFERRED: coop needs per-participant persistent state плюс shared campaign state. |
| R15-S02 | `game.cpp:1351–1405`, `SaveGame(int)` | Пишет version/subversion, inventory/ammo/health одного player, SelectedMap и unlock list; padding до 16 байт значением FF. | VERIFIED в этой функции нет обхода live actors, mover progress или script state. **Не переносить этот формат на 2022 checkpoint/save**. |
| R15-S03 | `game.cpp:1425–1535`, `LoadPersistentData`, `LoadGame(int)` | Читает зеркальный набор; отбрасывает несовпадающую version/subversion; LoadGame читает unlock array. | INFERRED: save schema/version guard важны для handshake. UNKNOWN: формат и полнота 2022 serialisation. |

Уверенность в прочитанных вызовах и полях source — высокая; уверенность в их соответствии конкретному адресу EXE 2022 отсутствует. Данные имён и путей пригодны как search anchors, а не как готовые адреса hook.

## SlaveDriver: что следует искать по смыслу

[REFWORLD_SATURN](../evidence/REFWORLD_SATURN.json) фиксирует следующий текст:

- VERIFIED: `OBJECT.C:197–205` ищет первый `OT_PLAYER`, вызывает constructPlayer, присваивает `camera=player->sprite` и прерывает поиск. `:231–233` во втором проходе пропускает параметры OT_PLAYER. `AI.C:40–57` создаёт PlayerObject через pool/free-object API. Это конкретная политика загрузки Saturn, а не предел общей object pool.
- VERIFIED: `AICOMMON.C:131–139`, `findPlayer(Sprite*,int)` проверяет distance/visibility исключительно глобального `player->sprite`. При этом `OBJECT.H:85–104` содержит per-monster `enemy`. INFERRED: потенциальное расширение списка кандидатов и существующего target reference — разные изменения; одно не гарантирует второе в KEX 2022.
- VERIFIED: `GAMESTAT.H:54–67`, SaveState хранит inventory/dolls/bowls/health/gameFlags/ammo/levFlags/currentLevel/desiredWeapon/time stamp, глобальная currentState. `GAMEFLAG_KILLEDSET` и `GAMEFLAG_KILLEDSELKIS` присутствуют в `:43–44`, флаги пирамид/доступа — в `:32–34`. Это словарь прогресса оригинального движка, но значения битов 2022 UNKNOWN.
- VERIFIED: `BUP.C:158–179` копирует SaveState из слота и обратно; `LEVEL.C:32–64` читает Saturn arrays sectors/walls/vertices/faces/objects и params. INFERRED: полезен для сопоставления семантики level-state, но не для Windows binary parser.

Нельзя считать Saturn header C-структурами для прямого memory cast в Windows x64: иной компилятор/архитектура, числовая модель и граф владения. Отсутствие сетевой логики в выбранных выдержках не доказывает её отсутствие во всём проекте.

## PSEPSX: ближайшие anchors к скриптам 2022

[REFWORLD_PSEPSX](../evidence/REFWORLD_PSEPSX.json) содержит реальные файлы builder и patch. Проверена только их локальная текстовая форма; применимость к установленному KPF требует сравнения имен и содержимого.

| Source | VERIFIED: наблюдаемый интерфейс | Что проверить в установленной игре |
|---|---|---|
| `Data/scripts/map/psx/post_processing.txt:39–67` | ProcessDolls получает `kexSlaveActor@` через `GetActorFromMapActorIdx`, меняет `GetOrigin()`/`GetSector()`, вызывает `ObjectFactory().SpawnActor(type,origin,yaw,sector)`. | Нативный spawn wrapper, Owner/controller binding для Obj_Player, побочные эффекты спауна; сам факт SpawnActor не доказывает независимость игрока. |
| `Data/scripts/map/psx/generic_script.txt:1–6` | `#include` post_processing и `$script 0` запускают PSXPostProcess. | Точка map startup; порядок до/после штатного player creation и actor iteration. |
| `Patches/PSEPSX.diff:1292–1340` | SETARENA script использует `Player().GetArtifacts()`, mapActor index и delayed level script; comment говорит о reload checkpoint. | Настоящая структура boss/checkpoint logic из установленного KPF; текст comment не является доказательством того, как работает checkpoint. |
| `Patches/PSEPSX.diff:1736–1755` | Добавленный A_BombIgnite перечисляет actor indices, пропускает Obj_Player, проверяет CanSeeLocation и создаёт ignitor. | Состав actor API, ограничения actor enumeration и тип health/damage. Это добавленный скрипт мода, не штатный native код. |
| `Patches/PSEPSX.diff:1660–1685` | В context/drop logic используются `Player().GetArtifacts()` и `kexSlaveRandomizer::UInt8()`. | Global-player dependency у drop RNG; shared progression policy. Имена RNG не устанавливают алгоритм или seed. |
| `PSEPSX.py:436–501` | Чтение ZIP и текстовый patch над извлечёнными файлами, затем упаковка mod KPF. | INFERRED: data/scripts route может сократить нативную инвазивность будущего PoC; проверить, что штатный script API допускает нужный controller state. |

README:59 утверждает ограничения scripting/menu. Это **утверждение автора README**, а не доказательство принципиального отсутствия расширений. Никаких предположений о готовом multiplayer API из него не следует. Во время этого исследования builder и его Python extension scripts не запускались.

## Build-порты: полезные образцы, другой движок

[REFWORLD_BUILD_PORTS](../evidence/REFWORLD_BUILD_PORTS.json):

- VERIFIED: `NBlood-master/source/exhumed/src/player.cpp:109–138` различает nLocalPlayer и `PlayerList[kMaxPlayers]`; `:663–689` и `:705–837` индексируют per-player state в InitPlayerKeys/InitPlayerInventory/RestartPlayer, есть условие nNetPlayerCount. `player.h:49–92` содержит соответствующие объявления. Это демонстрирует в **данном source-проекте** модель «локальная камера и несколько игроков», но не подтверждает работоспособность его сети и не доказывает возможность M0 в KEX.
- VERIFIED: `NBlood.../switch.cpp:100–142` связывает switch с channel и LinkMap, вызывает runlist_ChangeChannel. Это альтернативная event architecture; нельзя подставлять channel IDs вместо KEX tag/event IDs.
- VERIFIED: `NBlood.../save.cpp:83–154` пишет Build sector/wall/sprite и другие raw arrays; имя/версия формата в `:33–34`. Такой layout нельзя использовать для разбора Exhumed 2022 save.
- VERIFIED: `Raze-master/source/games/exhumed/src/save.cpp:57–79` централизованно вызывает SerializeState/Bullet/Gun/Init/Items/Move/Lighting/Objects/Player/Ra/Rand/RunList/Sequence/Snake/Switch/View/Queen/Rat. `player.cpp:2071–2138` содержит player and spawn-state serialization. INFERRED: хороший checklist полноты world snapshot и RNG state, но собственный native serializer KEX требуется исследовать отдельно.

## PowerSlave EX Co-Op Edition: границы доказательства

В [REFWORLD_COOP_INVENTORY](../evidence/REFWORLD_COOP_INVENTORY.json) сохранены SHA-256 и complete inventory supplied kit:

| Файл | Размер | SHA-256 |
|---|---:|---|
| `anubis.exe` | 3 762 176 | `488def7a7d0f676add9e2cf188639e13927a52cd6e27e58ab90fe48d4de1990f` |
| `mods/john.kpf` | 156 436 | `afdfc5544f517fa23dd80b95fbecd8b9df7533daaac6d08135d6262aa0044254` |
| `README.txt` | 2 849 | `87815398c8ad85118b94bce8f5afb2fd915031a64e160206bbeedf08e141aadc` |

VERIFIED: john.kpf является читаемым ZIP с тремя members: `sprites/player/john.png`, `john_anim.txt`, `john_sprite.txt`. Сохранены names/sizes/CRC32 и хеши/текст двух небольших текстовых members. В этих данных нет нативной сетевой реализации. В полном supplied directory inventory нет исходных `.cpp/.c/.h`; неизвестно, существуют ли они у автора или опубликованы где-то отдельно.

[REFWORLD_COOP_README](../evidence/REFWORLD_COOP_README.json) фиксирует **заявления README**, не доказанное runtime-поведение: до четырёх игроков, UDP 26100, одинаковый EXE у участников, UPnP, host выбирает save/map/travel, вход со следующего уровня, spectator после смерти, рестарт если все умерли, shared keys/artifacts/dolls, no friendly fire, `.coop` sidecar, chat/voice. Содержимое README VERIFIED как текст; соответствующая работа EXE **UNKNOWN** до статического анализа и разрешённого эксперимента. Особенно не следует переносить заявления о voice/UPnP/proximity acoustics в требования готового M0.

Для доказательного сравнения нужен baseline неизменённого 2015 public-beta EXE, его hash и локализация совпадающих/новых функций. Имеющийся open-source tree сам по себе не гарантирует точное соответствие этому baseline. Не восстанавливать сетевой протокол из одного README. Сравнение адресов с Windows x64 Exhumed 2022 требует независимой карты функций обеих сборок.

## Что world-authority обязан охватывать

Следующая таблица — **INFERRED design checklist**, основанный на source-наблюдениях выше. Она не описывает уже реализованный multiplayer и не доказывает конкретный serializer KEX 2022.

| Состояние | Единственный источник решения | Что отдельно показывать локально |
|---|---|---|
| Spawn/destroy actor, target/death, RNG-решения AI | Host simulation; стабильный NetEntityId и generation/level epoch | Анимация/эффекты с подтверждённым state |
| Projectile ownership, damage, radial blast, health | Host validation/simulation | Предсказанный muzzle/shot cosmetic effect с reconciliation |
| Pickup commit, shared keys/artifacts/dolls, unlocks | Host campaign state; dedup по event ID | UI/звук тому участнику, которому относится feedback |
| Door/lift/floor/breakable, event consumed, delayed scripts/boss phase | Host world event/state timeline | Интерполяция визуального движения, local audio listener |
| Travel/ending/overworld/checkpoint/save | Host transition transaction и save owner | Loading/progress/readiness конкретного клиента |
| Per-player ammo/health/controller и personal equipment | Host owns each participant's authoritative values | Local input, camera, HUD; отдельные viewmodel actors |

Две независимые сущности игроков потребуют проверить не только фабрику actor, но и AI candidate selection, owner/controller binding, local effects, world-events instigator, travel singleton, persistentData/save, cleanup при unload. Global pointer — конкретный узел переделки; не доказательство принципиальной невозможности.

## Незакрытые вопросы и продолжение

1. UNKNOWN: какие source anchors имеют подтверждённый аналог в EXE 2022 и с каким hash/VA/RVA/offset. Основной исследователь ведёт нативный анализ отдельно.
2. UNKNOWN: допускает ли Obj_Player spawn второй controller/puppet без перезаписи первого; какие системы получают player по singleton, а какие через owner. Это обязательная проверка M0.
3. UNKNOWN: serialisation/checkpoint 2022 сохраняет ли movers/script contexts/RNG/actor references; 2015 SaveGame не подменяет ответ.
4. UNKNOWN: кто является instigator в штатных script entrypoints 2022, можно ли отличить player slot без нативного изменения API.
5. UNKNOWN: нативная реализация coop kit, точное исходное основание и наличие публичных modified sources. Kit анализировать как отдельный бинарник.
6. Для возобновления сначала сверить хеши из Evidence с файлами; при совпадении читать сохранённые выдержки, не повторять полное сканирование. `capture_reference_world.py` воспроизводит эти read-only выборки, но повторный запуск нужен только при изменении входных материалов или явно расширенной области выборки.
