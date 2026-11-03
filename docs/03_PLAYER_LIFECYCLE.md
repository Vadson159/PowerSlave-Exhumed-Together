# Игрок: жизненный цикл и возможность нескольких игроков

**Актуализация runtime:** [C01](evidence/m0/C01/RESULT.md) создал отдельный Player context и native Puppet с сохранностью первого graph. Три ограниченных lifecycle cycles и separate native controller calls наблюдались; полный M0 всё ещё НЕ принят. [M0_EXPERIMENTS](M0_EXPERIMENTS.md) содержит новые ready fields, adapter и ограничения. Статические данные ниже сохраняются: штатный Spawn по-прежнему singleton-dependent, что A03 подтвердил runtime.

Идентичность всех VA: основной x64 EXE SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`, base `0x140000000`. Значения ниже относятся к этой сборке. Полные asm/pseudocode и xrefs — в [каталоге функций](research/FUNCTION_CATALOG.md). Getter callbacks LAB не везде распознаны Ghidra как самостоятельные функции: instruction-window Evidence сохраняет это явно; адрес callback нельзя автоматически называть entry с гарантированной функцией/границей.

VERIFIED цепочка создания:

1. [E010 KPF](evidence/E010_KPF_SELECTED_TEXT.json), `defs/actors/misc.txt:3–21`: `Actor Obj_Player`, `classname "kexSlavePuppet"`; `scripts/actorTypes.txt:20`: `AT_PLAYER = 0`.
2. [Factory from map](evidence/ghidra_2022/G_FUNC_1401d41f0.json) `0x1401d41f0` и [factory by type](evidence/ghidra_2022/G_FUNC_1401d4780.json) `0x1401d4780` выбирают тип 0 → kexSlavePuppet и отвергают player actor другого класса. Пулы классов/constructor, origin/angle/sector, затем virtual +0x38 (Spawn). Map record stride `0x14` наблюдается в loader/factory, это не Player layout.
3. [Allocator](evidence/ghidra_2022/G_FUNC_1401db790.json) `0x1401db790` выделяет и обнуляет `0x1e0` байт, вызывает actor constructor, устанавливает `kexSlavePuppet::vftable` (`0x14052d198`). Это фактический размер выделения этого класса; копирование этих байтов не эквивалентно корректному клонированию.
4. [Puppet Spawn](evidence/ghidra_2022/G_FUNC_1401dbc60.json) `0x1401dbc60` сначала вызывает базовый actor Spawn, затем получает Player через singleton game instance (`0x140af33a0`, slot +0xa8). [Getter](evidence/ghidra_2022/G_FUNC_1401b5520.json) `0x1401b5520` возвращает `this+0x40`.
5. Spawn заменяет `Player+0xd8` на `Puppet+0x10` (node), увеличивает refcount нового/уменьшает старого, записывает `Puppet+0x1b8=Player`. Выбора player index/аргумента другого controller в этом теле нет.

```text
GameInstance singleton (preferred VA 0x140af33a0)
  +0x40 -> embedded Player context
Player +0xd8 -> ObjectNode handle
ObjectNode +0x28 -> Puppet payload (checked against runtime type chain)
Puppet +0x10 -> ObjectNode
Puppet +0x1b8 -> Player owner
```

Статическая цепочка Player.GetActor: [callback 0x140194b30](evidence/ghidra_2022/G_INSPECT_140194b30.json) загружает `[RCX+0xd8]`, при ненулевом node tail-jumps в [0x140197ee0](evidence/ghidra_2022/G_FUNC_140197ee0.json). Тот загружает node+0x28, проверяет custom RTTI-chain на тип kexSlavePuppet и возвращает payload. **Player+0xd8 — не raw Puppet***. Изменение только этого поля вручную игнорировало бы refcounts и reciprocal owner.

Наблюдаемые поля, без фиктивной C++-структуры:

| Объект / offset | Тип/смысл по статике | Доказательство и ограничение |
|---|---|---|
| Actor/Puppet +0x18 | Адрес `kVec3` origin | script GetOrigin + `LEA RAX,[RCX+0x18]`: [window](evidence/ghidra_2022/G_INSPECT_140194aa0.json) |
| +0x30 | `slaveRotation_s`, компоненты по +0,+4,+8 | [rotation window](evidence/ghidra_2022/G_INSPECT_140198100.json), регистрация API; yaw по +0x38 подтверждён factory. Семантика остальных осей требует проверки. |
| +0x3c | `int&` sector index | [window](evidence/ghidra_2022/G_INSPECT_140198110.json), LinkSector |
| +0xe0 | `kVec3&` velocity | [window](evidence/ghidra_2022/G_INSPECT_140198120.json) |
| +0x17a | `int16&` actor health | [window](evidence/ghidra_2022/G_INSPECT_140194ac0.json), apply damage |
| Puppet +0x1b8 | Player owner/context pointer | Spawn write и Tick reads |
| Player +0x00 | Buttons read as uint32 | script `GetButtons` callback [0x1401f1180](evidence/ghidra_2022/G_FUNC_1401f1180.json); это часть input context, не весь input layout |
| Player +0x90 / +0x94 | artifacts / abilities enum reference | [artifacts](evidence/ghidra_2022/G_INSPECT_1401ae080.json), [abilities](evidence/ghidra_2022/G_INSPECT_1401ae090.json) + matching registration |
| Player +0x98 / +0x9a | keys / questItems `int16&` | [keys](evidence/ghidra_2022/G_INSPECT_1401ae0c0.json), [quest](evidence/ghidra_2022/G_INSPECT_1401ae0a0.json) |
| Player +0xa4 | teamDolls `uint32&` | [window](evidence/ghidra_2022/G_INSPECT_1401ae0b0.json) |
| Player +0xd8 | ObjectNode handle | GetActor, Spawn, Reset |

Полная таблица script declarations/callback aliases — [SCRIPT_API_2022](research/SCRIPT_API_2022.md), [E014](evidence/E014_SCRIPT_REGISTRATION_MAP.json). VERIFIED: RegisterObjectType получает размеры Player `0x198`, Actor `0x1b8`, Puppet `0x1e0`, AI `0x1f8`. Размер регистрации сам по себе не доказательство `sizeof`/ownership; Puppet allocator отдельно подтверждает своё `0x1e0`. Alignment, все lifetime invariants и конструктор независимого Player ещё не восстановлены. Не создавать header с padding по этой таблице.

Обновление: [Player update 0x1401dfe30](evidence/ghidra_2022/G_FUNC_1401dfe30.json) вызывается с gameInstance+0x40. В нём вызываются [input candidates 0x1401e34a0](evidence/ghidra_2022/G_FUNC_1401e34a0.json) и [0x1401e2ad0](evidence/ghidra_2022/G_FUNC_1401e2ad0.json), есть обращения к платформенным глобальным input objects. Их подписи и device routing ещё не валидированы. Puppet virtual update +0x20 → [0x1401db8a0](evidence/ghidra_2022/G_FUNC_1401db8a0.json): базовый actor update, player flags/owner fields, movement branches, sector interactions. Полный цикл описан в [02](02_ENGINE_ARCHITECTURE.md).

Здоровье/смерть: [Player.GetHealth 0x1401dfa20](evidence/ghidra_2022/G_FUNC_1401dfa20.json) читает actor+0x17a через handle, без actor возвращает 100/200 в зависимости от difficulty query. [Puppet damage callback 0x1401dbd00](evidence/ghidra_2022/G_FUNC_1401dbd00.json) выбирает pain/death sounds, holster и owner timers; [actor damage 0x1401a1370](evidence/ghidra_2022/G_FUNC_1401a1370.json) уменьшает health и вызывает virtual +0x48. Respawn/co-op death policy ещё отсутствует; не считать возможное восстановление health независимым жизненным циклом игрока.

Reset/переход: [0x1401dfa70](evidence/ghidra_2022/G_FUNC_1401dfa70.json) очищает Player handle с refcounts и ряд полей; [0x1401e04e0](evidence/ghidra_2022/G_FUNC_1401e04e0.json) переинициализирует level-local player state, weapon animation, pending position/health. Actor Remove, pool recycling и [deleting destructor](evidence/ghidra_2022/G_FUNC_1401d9370.json) — разные этапы; отсоединение второго owner должно участвовать в каждом. Смерть/respawn/смена карты без висячих node handles — отдельный критерий M0/M1.

Аудит single-player предположений:

| Подсистема | Что подтверждено для 2022 | Проверка/работа M0–M3 |
|---|---|---|
| Native Spawn | Безусловная привязка к singleton Player | Независимые Player contexts и reciprocal node/owner lifecycle |
| Global script Player() | Singleton dispatch через 0x1401f1260 | Определить context-aware policy либо оставить host campaign facade |
| Input | Global device queries; один Player update в state tick | Развести команды/turning history, не обновлять клавиатуру дважды |
| Camera/audio | State tick выводит listener из единственного actor; HUD использует один player | Локальный view отдельно от remote puppet gameplay |
| AI | Chase fallback обращается к singleton; scripts boss damage использует Player() | Выбор валидных живых игроков; проверить targeting/hate/ownership |
| Pickups/campaign | Штатные скрипты глобально читают artifacts, health, quest/buttons | Host policy и отдельный instigator для взаимодействий |
| Save/load | Однопользовательский slot pipeline и snapshot полей | Host save + per-player sidecar/protocol state, отдельный schema |

Прямые singleton обращения в **поставке 2022**: `scripts/p_enemy.txt:824–827` получает `Player().GetActor()` и наносит damage; `p_misc.txt:316` назначает projectile sourceOwner глобальному player; `TOMB/TOMBEND` читают buttons, lock/holster, questItems; `KILARENA` устанавливает artifacts. Это содержимое KPF, не наблюдение выполнения конкретной ветки. При network replay данных об одном puppet эти вызовы сами не станут адресовать нужного игрока.

2015 source полезен как карта вопросов: `kexPuppet::Spawn`, `kexPlayer`, `kexPlayerCmd` и статические smoothing history, weapon actions. Его layouts, owner pointer types и enum bits не копировать: уже наблюдаются node handles вместо простых связей и различия PF-флагов. [REF2015_PLAYER_LOOP](research/REF2015_PLAYER_LOOP.md).

Что осталось: независимый native Player constructor/lifetime; ABI/полный command layout; камера и weapon view owner; model/sprite для remote player (Obj_Player рассчитан на вид от первого лица); AI target acquisition; безопасное отделение presentation; поведение двух puppets на sector lists; повторные spawn/destroy, save/load, map transitions. Переход к реализации — по [09_POC_PLAN](09_POC_PLAN.md), без утверждения о готовности DLL-мода.
