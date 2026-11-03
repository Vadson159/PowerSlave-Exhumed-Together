# PowerSlave EX 2015: игрок, сущности, цикл, физика и оружие

Дата: 2026-10-31. Это анализ локального референсного исходного кода, не Exhumed 2022. Корень: `S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerslaveEX-master-source-code\kex3_anubis\source`.

README дерева описывает неофициальный ремейк PSX на Kex Engine 3.0 и собственные collision/AI systems. Название каталога не подтверждает коммит, дату сборки или соответствие EXE. Каждый используемый файл закреплён SHA-256 в Evidence. Референс не изменялся, не собирался и не запускался.

**VERIFIED** в этом документе означает только наблюдение в указанном локальном исходнике; **INFERRED** — вывод; **UNKNOWN** — не установлено. Ни одно VERIFIED здесь не доказывает соответствие KEX 2022 или поведение работающей игры.

## Evidence и воспроизводимость

- [REF2015_PLAYER_LIFECYCLE.json](../evidence/REF2015_PLAYER_LIFECYCLE.json): controller/puppet, поля, camera, одиночные связи, pickup.
- [REF2015_LOOP_ACTORS.json](../evidence/REF2015_LOOP_ACTORS.json): цикл, actor creation/removal, factory/RTTI, world, RNG.
- [REF2015_PHYSICS_WEAPONS.json](../evidence/REF2015_PHYSICS_WEAPONS.json): input, движение, collision scratch state, weapon/actions/projectiles/damage.
- [capture_ref2015.py](capture_ref2015.py): скрипт читает исходные байты и ограниченные диапазоны строк. Запуск из мода: `C:\Python314\python.exe docs\research\capture_ref2015.py`. При совпадении source hashes/ranges переиспользует Evidence, при изменении требует нового ID.

Общие метаданные **каждой** перечисленной функции: `authority=local-reference-source`; бинарный SHA-256, VA, RVA, file offset, x64 ABI, регистровые аргументы, сигнатура байтов и соответствие Exhumed 2022 = **UNKNOWN/null**. Известны исходные C++ имена/объявления и прочитанные связи. Source hashes закреплены в JSON и таблице в конце. Псевдокод Ghidra здесь не использован.

## Главный результат для M0

**VERIFIED REF2015:** `kexGameLocal` выделяет один `new kexPlayer` в `game.cpp:489`, хранит одиночный указатель в `game.h:269`. Factory для `AT_PLAYER` выбирает `kexPuppet` (`actorFactory.cpp:110–112`).

**VERIFIED REF2015:** `kexPuppet::Spawn` (`player.cpp:1061–1083`) безусловно выполняет:

~~~cpp
owner = kexGame::cLocal->Player();
kexGame::cLocal->Player()->SetActor(this);
~~~

**INFERRED REF2015:** второе обычное создание puppet через этот путь перезапишет actor единственного controller; оба puppet получат одинаковый owner и прочитают одни команды. Это конкретная single-player связь, а не принципиальный запрет на N игроков.

**VERIFIED REF2015:** общий список gameObjects и sector actorList содержит множество объектов; MoveActor/Trace/damage принимают конкретный actor. Следовательно глобальный player pointer не отменяет общей actor-модели.

**UNKNOWN KEX2022:** сохранились ли эти классы, разделение player/puppet, owner, factory и порядок обновлений. Эти сведения — ориентиры независимого поиска в текущем EXE, не восстановленная структура 2022.

## Игрок и жизненный цикл

Наблюдаемая **VERIFIED source model**:

~~~text
kexGameLocal::player -> kexPlayer
                         | actor
                         v
                      kexPuppet : kexActor : kexGameObject : kexObject
                         | owner
                         +---------------------> kexPlayer
~~~

У puppet owner закрытый, публичного setter в player.h не объявлено; SetActor/ClearActor меняют только сторону controller.

| Источник | Наблюдаемые поля | Значение для поиска |
|---|---|---|
| `gameObject.h:45–73` | origin kexVec3; yaw/pitch/roll kexAngle; target pointer; intrusive link; timestamp/refcount/objID/bStale | Пространство, ссылки, отложенное удаление |
| `actor.h:148–185` | float radius/height/scale/stepHeight/fallHeight/friction/gravity; int16 health; velocity/movement; sectorLink/bounds/sector/mapActor; animations/frameID/ticks/gameTicks/flags/prevOrigin | Физика и damageable actor |
| `player.h:54–86` | puppet.owner; playerFlags; byte jumpTicks; oldMovement; lavaTicks/slimeTicks | Исполнитель движения и связь с cmd/inventory |
| `player.h` static puppet state | mapDestinationPosition, bScheduleNextMapWarp | Общий destination между картами |
| `player.h:174–219` | cmd; ankahs/flags; bob/view/air/lock/shake; встроенный weapon; current/pending weapon; weapons[]/ammo[]; artifacts/keys/questItems/abilities/teamDolls; actor pointer | Controller, inventory, input, view |
| `playerCmd.h:68–85` | buttons; buttonHeldTime[]; angles[2]; movement[2]; turnx/y; joystick fields | Cmd логически принадлежит controller |
| `pWeapon.cpp:26–50` | weapon owner controller; anim/state/bob | Отдельный weapon state machine |

Порядок C++ деклараций не доказывает binary layout даже для 2015 EXE. sizeof/padding/vtable/смещения не вычислялись; переносить их в x64 2022 нельзя.

### Создание

1. **VERIFIED:** `kexGameLocal::kexGameLocal` (`game.cpp:473–497`) создаёт player, playLoop, cmodel и менеджеры. Destructor удаляет их (`503–513`).
2. **VERIFIED:** `kexWorld::SpawnMapActor` (`world.cpp:644–668`) переводит map x/y/z в float, угол через Deg2Rad((360-angle)+90), передаёт type/position/sector фабрике.
3. **VERIFIED:** `kexActorFactory::Construct` (`actorFactory.cpp:46–76`) вызывает ConstructObject(className), задаёт def/origin/yaw/type/sector, вызывает CallSpawn.
4. **VERIFIED:** `kexObject::CallSpawn/ExecSpawnFunction` (`framework/object.cpp:155–180`) выполняет Spawn по RTTI-цепочке от base к derived; это не простой одиночный виртуальный Spawn. Macro `framework/object.h:34–37` сохраняет member-function pointer.
5. **VERIFIED:** `kexGameObject::Spawn` (`gameObject.cpp:249–255`) присваивает timeStamp, objID=++id и добавляет объект в общий список. Actor Spawn/SetSector обеспечивают actor-часть, затем puppet Spawn задаёт owner.
6. **VERIFIED:** puppet Spawn задаёт radius=95.25, height=160, stepHeight=48, health=200, friction=0.9375, AF_SOLID|AF_SHOOTABLE и bounds. Это константы этого source-only референса, не 2022.
7. **VERIFIED:** `kexPlayLoop::Start` (`195–237`) проверяет один Player.Actor, связывает HUD, запускает level script с этим actor, вызывает Ready, настраивает render/music/water.
8. **VERIFIED:** `kexPlayer::Ready` (`1160–1190`) сбрасывает view/air/keys/lock/shake, поднимает weapon, копирует controller.health в actor; при static warp переносит actor и ищет сектор.

**INFERRED:** M0 аналогичной архитектуры должен проверить обе стороны owner↔actor, world/sector/list registration, отдельный cmd и один physics update за тик. Создание второго sprite этого не доказывает.

### Reset, death, respawn, removal

- **VERIFIED:** `kexPlayer::Reset` (`1114–1154`) обнуляет actor/cmd, сбрасывает health и inventory/keys/abilities, выдаёт persistent weapons из defs и ammo максимумами. Reset не равнозначен сохранению прогресса при respawn.
- **VERIFIED:** `kexActor::InflictDamage` (`actor.cpp:493–535`) проверяет AF_SHOOTABLE, вычитает amount, вызывает OnDamage, при переходе через 0 выбирает death anim и OnDeath; puppet отдельно сохраняет shootable flag.
- **VERIFIED:** `kexPuppet::OnDamage` (`140–248`) при health<=0 ставит PF_DEAD, обрабатывает god, вызывает глобальный DamageFlash и owner.HoldsterWeapon. Смерть не уничтожает actor сразу.
- **VERIFIED:** puppet Tick при смерти вызывает DeadMove; controller Tick уменьшает viewZ и обновляет weapon.
- **VERIFIED:** `kexPlayLoop::Tick` (`311–317`) по смерти одного global local actor после countdown вызывает RestartLevel. `RestartLevel` (`504–513`) запросит текущую карту.
- **VERIFIED:** Actor.Remove отключает flags и скрывает; GameObject.Remove ставит bStale. Removing требует bStale и refcount<=0.
- **VERIFIED:** RemoveGameObject вызывает OnRemove, проверяет refcount, удаляет через delete. Actor OnRemove разрывает spatial/sector links, базовый OnRemove — object link и target.
- **VERIFIED:** RemoveAllGameObjects сначала обнуляет targets, затем удаляет; World.UnloadMap очищает общий список, **один** controller.actor, spatial index, CModel и world heap.
- **UNKNOWN:** самостоятельный безопасный puppet-destroy с очисткой controller/owner, отдельный полноценный respawn API, дополнительные cleanup-пути вне прочитанных областей. Пустые destructor player/puppet не доказывают отсутствия других механизмов.

**INFERRED:** death, disconnect, unload и recreation требуют разных правил; ссылки должны завершиться до уничтожения. Network ID нельзя превращать в сырой указатель.

## Цикл и порядок систем

**VERIFIED REF2015** (`session.cpp`, `game.cpp`, `playLoop.cpp`):

~~~text
Session.RunGame
  Game.Start
  while loop:
    ProcessEvents -> RunFrame -> UpdateTicks (один/несколько раз)
    DrawFrame
    GetNextTickCount / ForceSingleFrame
    Mem_GC -> Sound.Update

RunFrame
  Console.Tick
  GameLocal.Tick
    pending state:
      StopSounds -> old loop.Stop -> select loop/map load
      RNG seed -> new loop.Start
    player.Cmd.BuildCommands
    gameLoop.Tick / activeMenu.Update
    player.Cmd.Reset -> buttonEvent=0
    UpdateSounds -> ticks++

PlayLoop.Tick (ticks>4, нет pause/inventory/fade-return)
  clear lights / bufferUpdateList
  World.UpdateAnimPics
  GameLocal.UpdateGameObjects
    each object.Tick -> Removing? RemoveGameObject
      puppet.Tick -> physics -> EnterSectorSpecial
      actor.Tick -> sprite actions -> movement
      projectile.Tick -> actor.Tick -> homing/aim
  Player.Tick
    air/shake/lock/weapon change/use/bob/weapon.Update
  HUD.Update
  UpdateWater -> WaterBubbles
  ScriptManager.UpdateLevelScripts
~~~

**VERIFIED:** Draw отдельно: SetupFromPlayer -> DrawView -> weapon.Draw -> automap -> HUD -> fade/stats.

**INFERRED:** нельзя дублировать puppet physics дополнительным controller loop: puppet уже обновляется в world list. Remote commands должны прожить до puppet/weapon/use consumers, а не быть обнулены преждевременно. Draw hook не является simulation tick.

**UNKNOWN KEX2022:** частота, порядок, потоки, pause/menu/fade semantics, анимационные callbacks. Source clockspeed 60 не доказывает tickrate актуальной игры.

### Время и RNG

- **VERIFIED:** clockspeed через FrameSec(60.0f) (`session.cpp:24`); render FPS cvar ограничен 1–60. GetNextTickCount использует accumulator remainder; если ticsToRun>60 — ставит 1. Его выражение Sec2MSec(deltaTime)<=clockspeed зафиксировано как написано, без исправления единиц.
- **VERIFIED:** session/gameLocal/playLoop/actor имеют разные counters. GameObject timeStamp берётся из session GetTime; objID растёт глобально, не является доказанным постоянным network ID.
- **VERIFIED:** `math/random.cpp`: static unsigned seed; SetSeed=(randSeed*2+1)*69069ul; Int/Byte используют seed*1664525ul+221297ul. SysRand вызывает C rand(), отдельный источник.
- **VERIFIED:** state transition reseed использует Timer.GetTicks()>>4 (`game.cpp:834`). Shot spread/pain/jump/shake расходуют RNG; actor randomization использует SysRand.
- **INFERRED:** одного общего seed недостаточно для lockstep: число игроков, cosmetic calls, порядок событий и второй RNG меняют последовательности. Authoritative replication — исходная рабочая гипотеза, пока детерминизм не доказан отдельно.

## Физика и взаимодействие

**VERIFIED:** puppet Tick читает owner.Cmd, выбирает Dead/Fly/Water/GroundMove; lock может остановить работу. GroundMove (`433–620`) меняет yaw/pitch/roll по cmd, friction/Jump/gravity/artifact movement, acceleration, floor/ceiling bounds, вызывает MoveActor(this). Углы проходят kexAngle/rad conversions; единицы координат не приравнивались метрам.

**VERIFIED:** `kexCModel::MoveActor` (`1393–1428`) получает actor pointer, сохраняет actor/radius/height/movement/start в **поля CModel**, делает collision/sector advancement, записывает actor Origin=end, Movement=moveDir, CheckActorPosition и LinkArea. Trace (`1434+`) повторно использует scratch fields и возвращает contactActor/Face/Sector/fraction.

**INFERRED:** один CModel допускает последовательный вызов для N actors; singleton не доказывает one-player-only. Но hook/reentry/multithreading/prediction, использующие его одновременно, создадут риск потери scratch results. Реальная reentrancy не проверялась.

**VERIFIED:** TraceActorsInSector (`573–699`) проходит actorList, исключает source; projectile исключает moveActor.Target (shooter), AF_TOUCHABLE вызывает OnTouch(moveActor), solid/shootable выставляют contact.

**VERIFIED:** TryUse (`1341–1358`) trace из actor position+ViewZ по его yaw/pitch; передаёт **this player** в World.UseWallSpecial. Это owner-aware интерфейс в исходнике, но полная trigger semantics не исследована.

**VERIFIED:** weapon/key OnTouch берёт player из puppet.Owner (`pickup.cpp:130–152;501–517`); weapon Spawn проверяет global Player.WeaponOwned (`158–170`), artifact Spawn также содержит global ownership checks.

**INFERRED:** одновременно могут существовать owner-aware callbacks и single-player initialization. Нужно отдельно решить личный pickup, общий ключ/артефакт, удаление объекта один раз и проверку «уже собран» при входе на карту.

### Камера и ввод

- **VERIFIED:** PlayLoop.Draw выбирает global Player; SetupFromPlayer(kexPlayer*) принимает явный context (`renderView.cpp:140–163`), копирует actor pose и прибавляет viewZ/bob/land/step/shake.
- **VERIFIED:** renderer пропускает actor global Player, Anim==NULL и AF_HIDDEN (`renderScene.cpp:1457–1464`).
- **INFERRED:** local observer не обязан совпадать с единственной физической сущностью; remote avatar требует подходящих anim/sprite и корректного local actor skip. DamageFlash/observer/listener нужно отделить от simulation.
- **VERIFIED:** kexPlayerCmd — поле controller, но BuildButtons читает общий Actions; BuildTurning содержит **static history[4][2], turnLerp[2], historyCount** (`playerCmd.cpp:99–103`).
- **INFERRED:** два cmd объекта сами по себе не дают независимые local devices: static smoothing и общий Actions являются точками перекрёстного ввода. Для M1 нужны отдельные input streams/контексты либо готовые cmd.

## Оружие, атаки и урон

**VERIFIED:** kexPlayer constructor устанавливает weapon.owner=this. Update weapon делает UpdateBob+UpdateSprite; UpdateSprite (`pWeapon.cpp:175–320`) читает owner.Cmd/buttons/ammo, управляет WS_IDLE/RAISE/LOWER/HOLDSTER/FIRE, underwater restrictions и frame actions через Execute(owner.Actor()).

**VERIFIED:** ChangeAnim(spriteAnim_t*) (`57–77`) немедленно выполняет actions frame 0. Смена анимации может вызвать gameplay effect уже на входе; она не доказана как чисто визуальная операция.

| Исходная функция/маркер | VERIFIED связь | INFERRED значение для co-op |
|---|---|---|
| kexActionHitScan, `actionDef.cpp:213–266` | RNG spread, Owner.AutoAim, CModel.Trace, ContactActor.InflictDamage(shooter,amount) | Host решает hit/damage, shooter отдельный actor |
| kexActionPlayerMelee, `274–318` | Owner, trace, weapon frame change, InflictDamage | Gameplay callback нельзя дублировать visual replay |
| kexActionConsumeAmmo, `425–437` | Owner.ConsumeAmmo | Расход у нужного controller, один авторитетный shot |
| kexActionSpawnProjectile, `581–635` | SpawnFromActor, Velocity, SetTarget(actor), Pitch | Target projectile здесь source/shooter, не victim |
| Projectile.SetHomingTarget, `86–99` | Отдельный homingActor с refcount | Сохранить source и homing IDs раздельно |
| Projectile.UpdateMovement, `455–543` | Base move/contact/bounce/initial sector/overlap/expiry -> OnImpact | Projectile — authoritative replicated actor |
| Projectile.OnImpact, `549–586` | PF_IMPACTED guard, damage(this), death anim/Remove, clear homing | Dedup event IDs дополнительно к engine guard |
| Actor.InflictDamage, `493–535` | subtraction + OnDamage/OnDeath | Репликация голого health теряет side effects |

DECLARE_KEX_ACTION — исходниковый макрос, не доказанный экспорт/прототип EXE 2022. Конкретные weapon defs/animation assets и damage values этим исследованием не установлены.

## Single-player зависимости

| Подсистема | VERIFIED source observation | Следующая проверка |
|---|---|---|
| ownership | один new player, puppet Spawn owner/bind global | Найти и типизировать реальный bind 2022 |
| commands | один Build/Reset, shared Actions/static smoothing | Разделить input context/sequence |
| actor world | N objects/sector list, явный actor в collision | Подтвердить по бинарнику; это не M0 proof |
| player update | один Player.Tick после world objects | N controllers, без double physics |
| AI acquire | LookForTarget берёт global Player.Actor (`ai.cpp:480`) | Выбор из registry живых игроков |
| AI fallback | ChaseTarget/лавовое Ignite возвращаются к global Player (`937;976`) | Проверять fallback, не только acquire |
| HUD/view/audio | один player HUD, observer/listener, global flashes | Local presentation отдельно от host sim |
| death | смерть одного actor -> RestartLevel | Team wipe/revive/spectate |
| pickup spawn | часть callback owner-aware, Spawn global ownership | Shared/personal/persistent pickup policies |
| travel | static destination/flag | Per-player placement + level epoch |
| unload | очищается один controller.actor | Очистить N references до heap purge |
| persistence | Save/RestorePersistentData копируют одного player | Реальный 2022 save-format audit |
| rendering | исключён один local actor; нужны anim | Remote avatar presentation |

Эти наблюдения **не доказывают невозможность N игроков**. Они показывают, какие взаимосвязи нужно проверить при заявлении «вторая сущность независима».

## Реестр важных функций и связи

Для всех строк: source-only VERIFIED; binary identity/address/ABI = UNKNOWN; source SHA и bounded excerpt в Evidence. Уверенность высокая в прочитанном тексте, переносимость не установлена. Для каждой требуется независимое сопоставление с 2022 и подтверждение call/data xrefs. Префиксы: PLAYER=REF2015_PLAYER_LIFECYCLE; LOOP=REF2015_LOOP_ACTORS; PHYSICS=REF2015_PHYSICS_WEAPONS; точный record ID = prefix + `::` + suffix.

| Исходное имя | Файл:строки | Evidence record suffix | Роль/связи |
|---|---|---|---|
| GameLocal constructor/destructor | `game/game.cpp:473–513` | PLAYER::controller_allocation | один player/cmodel, lifetime |
| ActorFactory.Construct | `game/actorFactory.cpp:46–76` | LOOP::factory | ConstructObject -> assign -> CallSpawn |
| ActorFactory.Spawn(int,...) | `game/actorFactory.cpp:82–121` | LOOP::factory | type/defs/class selection |
| Object.ExecSpawnFunction/CallSpawn | `framework/object.cpp:155–180` | LOOP::object_rtti_spawn | base-to-derived Spawn chain |
| GameObject.Spawn | `game/gameObject.cpp:249–255` | LOOP::game_object | timestamp/ID/list |
| Puppet.Spawn | `game/player.cpp:1061–1083` | PLAYER::puppet_initialization | owner/actor bind |
| Player.Reset/Ready | `game/player.cpp:1114–1190` | PLAYER::puppet_initialization | стартовый reset/map ready |
| Puppet.Tick | `game/player.cpp:1015–1055` | PLAYER::puppet_initialization | cmd -> physics -> sector events |
| Player.Tick | `game/player.cpp:1652–1721` | PLAYER::puppet_initialization | air/view/use/weapon |
| Puppet.OnDamage | `game/player.cpp:140–248` | PLAYER::death_damage | PF_DEAD, flash, holdster |
| Puppet.GroundMove/Jump | `game/player.cpp:333–620` | PHYSICS::jump_ground | cmd/acceleration/artifacts/collision |
| Puppet.WaterMove/FlyMove | `game/player.cpp:767–968` | PHYSICS::water_fly | alternative physics |
| Puppet.DeadMove | `game/player.cpp:969–999` | PLAYER::death_damage | corpse physics |
| Player.TryUse/AutoAim | `game/player.cpp:1341–1424` | PLAYER::inventory_use | trace/world/target selection |
| Player.GiveWeapon/ConsumeAmmo/GiveKey | `game/player.cpp:1430–1544` | PLAYER::inventory_use | inventory mutations |
| Session.RunGame/RunFrame | `framework/session.cpp:207–283;81–85` | LOOP::session | scheduling/sim entry |
| Session.GetNextTickCount | `framework/session.cpp:158–201` | LOOP::session | accumulator/clamp |
| GameLocal.Tick | `game/game.cpp:774–861` | LOOP::game_dispatch | states/input/reset/sound |
| PlayLoop.Start/Stop/Tick/Draw | `game/playLoop.cpp:195–391` | LOOP::play_lifecycle_tick | lifecycle/system order |
| GameLocal.UpdateGameObjects | `game/game.cpp:1184–1198` | LOOP::game_objects | list tick/delete |
| GameLocal.RemoveGameObject/RemoveAll | `game/game.cpp:1204–1243` | LOOP::game_objects | references/unlink/delete |
| GameObject.SetTarget | `game/gameObject.cpp:230–243` | LOOP::game_object | refcount ownership |
| Actor.Tick/Remove/OnRemove | `game/actor.cpp:78–136` | LOOP::actor_lifecycle | generic update/teardown |
| Actor.InflictDamage | `game/actor.cpp:493–535` | LOOP::actor_lifecycle | damage/death |
| CModel.MoveActor/Trace | `game/cmodel.cpp:1393–1518` | PHYSICS::cmodel_move_trace | stateful collision |
| CModel.TraceActorsInSector | `game/cmodel.cpp:573–699` | PHYSICS::cmodel_actor_collision | touch/source exclusion |
| PlayerCmd.BuildCommands/BuildTurning | `game/playerCmd.cpp:251–256;99–168` | PHYSICS::commands | input/shared smoothing |
| PlayerWeapon.ChangeAnim/UpdateSprite | `game/pWeapon.cpp:57–77;175–320` | PHYSICS::weapon | actions/state |
| HitScan/ConsumeAmmo actions | `game/actionDef.cpp:213–266;425–437` | PHYSICS::actions_hitscan_ammo | shot/owner/ammo |
| SpawnProjectile action | `game/actionDef.cpp:581–635` | PHYSICS::actions_hitscan_ammo | spawn/source |
| Projectile.UpdateMovement/OnImpact | `game/projectile.cpp:455–586` | PHYSICS::projectile | collision/impact |
| RenderView.SetupFromPlayer | `renderer/renderView.cpp:140–163` | PLAYER::camera | explicit observer context |
| AI.LookForTarget/ChaseTarget | `game/ai.cpp:473–497;969–985` | PLAYER::ai_single_target | acquire/fallback |
| World.SpawnMapActor/LoadMap/UnloadMap | `game/world.cpp:644–738` | LOOP::world_lifecycle | map lifecycle |
| GameLocal.ChangeMap/LoadNewMap | `game/game.cpp:1085–1118` | LOOP::map_change | transition/load |
| GameLocal.SavePersistentData/Restore | `game/game.cpp:1305–1345` | PLAYER::controller_allocation | single-player subset |
| Rand.SetSeed/Int/Byte/SysRand | `math/random.cpp:31–70` | LOOP::rng | custom seed + rand |

## Полезность референса и дальнейшие проверки

**INFERRED:** источник полезен как словарь подсистем, RTTI/console/string anchors, пример связей и чеклист single-player assumptions. Он показывает, почему одиночный player pointer не равен невозможности иметь N actors.

**UNKNOWN:** 2022 может использовать другую иерархию/физику/VM/layout/assets. Совпавшее имя недостаточно; нужны xrefs и body/dataflow в целевом SHA-256.

Дальнейший приоритет:

1. Найти controller allocation/bind/create в **2022** по фактическим строкам/RTTI/данным; сохранить Ghidra dossier.
2. Подтвердить tick/input consumers и несколько проходов controller/world.
3. Восстановить минимальные actor/owner/cmd/sector/refcount fields по reads/writes 2022, без переноса layout.
4. Подтвердить player-start consumption, spawn без перепривязки primary player и корректный destroy.
5. Проверить AI fallback, observer, inventory initialization, death/restart/travel/save, animation side effects.
6. После разрешения будущего runtime этапа — M0 с двумя контроллерами/actors и независимыми командами/позициями/health/destroy. Сейчас запуска/инъекции не было.

Статус: source questions player/loop/actor/physics/weapons раскрыты в указанных пределах. Полная AI state machine, bosses/scripts, networking, полный save format не исследованы. M0 и runtime proof — UNKNOWN.


## Хеши исходных файлов, использованных в Evidence

| Файл относительно корня референса | SHA-256 исходных байтов |
|---|---|
| kex3_anubis/source/framework/object.cpp | f273e952cac7749c915c29032e5c5394715f5e51a220143801108e6efd7566bf |
| kex3_anubis/source/framework/object.h | c1e82a4b142a456db5e5e66dd4df76c67ca5fbae0abccef7b2eb508df8e62744 |
| kex3_anubis/source/framework/session.cpp | 2bdcf49eee7c6363ec31d17418896a3121071080b00d442385aec40b4ddc931b |
| kex3_anubis/source/game/actionDef.cpp | 2a11ea1158d67bcbcb979f4bda8e216ae5fa84c6683eb904577c308fe4ab9b1f |
| kex3_anubis/source/game/actor.cpp | 21b0eb17a0d2fa71ea4c96652d28a0bbf80d782d274ae067973449dd4e5cb55a |
| kex3_anubis/source/game/actor.h | b937b677f42c64638971743ce16c042b2ff500f165f4ace75167b2d447532924 |
| kex3_anubis/source/game/actorFactory.cpp | c659bc92b17690c16ad46df86e11048775f38f37ef88740c181a7a9512a8e078 |
| kex3_anubis/source/game/ai.cpp | 0569c408da5899ee27a97af81de880fee9ae0677893614728ed13e65e0fc09ae |
| kex3_anubis/source/game/cmodel.cpp | 0c96e70815d6a820f13d148c1e2e6c82cfd52581f708842c1fbd62a44fc5a132 |
| kex3_anubis/source/game/cmodel.h | 4c1a57dce58b347fb6326ca2a8ae5494991445be371b9239a8e749a700c36317 |
| kex3_anubis/source/game/game.cpp | 26a2437c4b9c15d42552a2d8e67774e01917eb00051d58e6f2d0b75051a66a89 |
| kex3_anubis/source/game/game.h | a1987d77a7951c5b2c03230fa64ba92945bb3018fd674ca72ba45335fbba5a9e |
| kex3_anubis/source/game/gameObject.cpp | d3566411cbd6e27e3f1514609a17d63081452ba1bc37a100d7adf294953697c9 |
| kex3_anubis/source/game/gameObject.h | 43154c1271ba77ae21258f3d5622be70a774f2fa563df0c27a6053c8491bb125 |
| kex3_anubis/source/game/pWeapon.cpp | 2101e29d35ee7bafe9bb30e02a66372d207157891d7c35a1e1f7fd25cfa04a9e |
| kex3_anubis/source/game/pickup.cpp | b84e2f036ca2b99802437c3c4bf96189a75812fb1f5a9d3a9f0d0ee4cd3070ce |
| kex3_anubis/source/game/playLoop.cpp | f2e86050a50fa3bb0da60050c980e73a0bc2eea6739cc2fc61a580d9cef171f3 |
| kex3_anubis/source/game/player.cpp | f8b3de464baffb7cb4d5cc679f19d903b4efed530ceb1ff77153e4885f2dad92 |
| kex3_anubis/source/game/player.h | eb2c8984eee432df97062816c4ec4a3c674bf62d1d74a152e01c99a01731e19c |
| kex3_anubis/source/game/playerCmd.cpp | 7bdc3f2e65c96220fc769e2d6647fcf6aad844026329e7ec270bd14f9cd6f0b5 |
| kex3_anubis/source/game/playerCmd.h | 1f1436205f79cb88b6a96cc73b84fee1bfd0d166cd38bae12887eed85adf5ab2 |
| kex3_anubis/source/game/projectile.cpp | 20985ebae08747ca540fd9529fed21ddebd72b8527ffc38ec8ed25bc15b27a05 |
| kex3_anubis/source/game/world.cpp | 6940743d3efafa0477f4af715e9027cd91c259e3d421eca6581642889b48c29b |
| kex3_anubis/source/math/random.cpp | bbd6d7edcee4d4101564d71beb94398fb94bd8cd3d14161619e6d6e31d22004d |
| kex3_anubis/source/renderer/renderScene.cpp | 4e133ed81d92c7532c69198401ccbea9a42fbd0b27a79ffe34720f75abc460f6 |
| kex3_anubis/source/renderer/renderView.cpp | 6e874d4f6f3e74a8d55195df7f47310092a3518e6fbe5ebcc297214a4e3edd77 |
