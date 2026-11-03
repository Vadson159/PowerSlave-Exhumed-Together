# PowerSlave Exhumed Together — handoff для Opus 5.5

## Статус и актуальная задача

**Самое позднее дополнение — C05:** [отчёт](evidence/m0/C05/RESULT.md), [сводка](evidence/m0/C05/RESULT_SUMMARY.json), [актуальный M0](M0_EXPERIMENTS.md). Реальное P2 melee→Anubis hit100→80/source/owner=P2 подтверждено; NativeMelee199710→Damage1a1370, return site1999e9. Для воспроизводимости нужен Default-desktop запуск через существующий REA provider и user-active карта с измеренным tick, не просто state3. Причина старого C01/C4 не установлена однозначно. Полный M0 не принят: после боя removed enemy.target удерживал P2 node(refs1), holder enemy node UNKNOWN; contexts pinned до normal exit0. P1 сохранил identity и нормальный ввод, но получил штатный AI damage200→160. Изоляция C05 неполна для двух native Steam auxiliary log/cache paths; source fix ещё требует runtime acceptance. Прежде чем расширять M0, принять эту изоляцию и проверить native reference release без raw pointer writes. Нижние C01 «weapon hit UNKNOWN» относятся к историческому срезу.

**Дополнение после разрешённого runtime M0:** [M0_EXPERIMENTS](M0_EXPERIMENTS.md), [C01](evidence/m0/C01/RESULT.md). Два native Player contexts реально выполняли отдельные controller/physics/weapon paths. Полный M0 НЕ принят: actual weapon hit и полная collision/lifetime/death/map проверка UNKNOWN. Все тестовые процессы завершены. Статические разделы ниже сохраняют исторический scope; их runtime UNKNOWN читать с этим дополнением.

VERIFIED ниже относится к статике. Исходная установка/пользовательские сохранения по хешам не изменены. Frida/native CModule code hooks выполнялись только в test copy; DLL на диске не создавалась. A01/A02 записали ранние sandbox-account логи вне workspace; нарушение изоляции сохранено явно, последующие наблюдаемые пути перенаправлены. Не утверждать, что вообще никаких внешних записей не было.

### Runtime gate для следующего агента

- A03 подтвердил ABI factory и singleton rebinding. Второй Puppet сам по себе не независим.
- C01 constructor witness подтвердил408-byte начальное состояние. Новый owned context создаётся без live memcpy. RVA1ae110 нельзя повторно вызывать: hardcoded globals/callbacks.
- Ready refs обязательны: Player+168→self+13c, +180→self+134. Без них native Reset1dfa70 дал m_pRefValue assert. Затем native inventory reset1dfb40; presentation registry не дублируется.
- Scoped GetPlayer должен реально выбрать P2 внутри native Spawn; node/refcount/owner пишет native код. JS/replaceFast варианты не прошли; рабочий diagnostic5-byte redirect имеет строгий prefix/simulation-thread safe point/restore. Production concurrency/unload не доказаны.
- Native controller1dfe30 выполнялся последовательно для P1/P2; synthetic seams1e34a0/1e2ad0 дали420 calls каждому, раздельные movement/yaw/sectors/health. Hardware13-button loop пока выполняется дважды — реальный remote-input adapter UNKNOWN.
- Native weapon-ready20f9e0(P2+e0,0) устранил null animation. Обе melee actions199710 получили собственные Puppet/owner. Урон через native1a1370 подтверждён отдельно; weapon hit НЕ подтверждён.
- Три construction/Remove/Reset/retire cycles: node payload=null/refs0, guards и original graph целы. Contexts pinned до normal exit0; полная slot deallocation, death/respawn/map teardown UNKNOWN.
- C4 hit experiment не дошёл до Spawn: state3, но tick не наблюдался, primary SleepEx. Foreground=false/null, synthetic activation не помогла. Exact root cause UNKNOWN; background/desktop ограничение INFERRED. Следующий опыт — fresh interactive test run с подтверждённым tick и одним probe. Не форсировать gameplay с worker thread.

Runtime усиливает приоритет Full Player; восемь игроков/кампания не доказаны. Proxy не устраняет scheduling и всё ещё требует owner-dependent contracts; M0-D сейчас не обоснован. Источники/ошибки/reproduction/acceptance — M0_EXPERIMENTS/C01. Не повторять общий reverse и не начинать сеть/ENet/Steam changes.

Цель пользователя: **2–8 игроков включая хоста**, host-authoritative **listen server**, LAN/Radmin VPN, **ENet предпочтителен**; оригинальная кампания, индивидуальное состояние игроков и общий сюжетный прогресс. Без STUN/TURN, NAT traversal, публичных серверов и matchmaking. Будущие ruleset options: shared/personal items и Power-Ups, Friendly Fire, respawn, difficulty scaling; подробно сейчас не проектировать. Все **20 оригинальных Steam-достижений** должны иметь Shared Achievement Progress по правилам участия ниже.

Не продолжать широкий реверс KEX/референсов и не исправлять REA MCP ради этого этапа. Сначала M0: независимый второй gameplay player. Если архитектуру нельзя выбрать статически, провести узкие эксперименты, описанные здесь и в [09](09_POC_PLAN.md).

## Идентичность и повторное использование Evidence

- Игра: `S:\Games\SteamLibrary\steamapps\common\PowerSlave Exhumed\anubis_Shipping_Steam_x64.exe`.
- **SHA-256: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`**, AMD64 PE32+, preferred base `0x140000000`.
- KPF SHA-256: `d34b63551713bf46c4215632d9b5f92b452791023937fe7d22a196bff9ff4bbd`.
- Работа: `S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod`.
- VA в документах — preferred VA; runtime address = actual module base + **RVA**. File offsets приведены отдельно в [07](07_HOOK_CANDIDATES.md), не подставлять RVA как смещение файла. Заголовок main EXE не выставляет DYNAMIC_BASE; адреса всё равно привязывать к module base и SHA.

[Манифест](01_BINARY_MANIFEST.md), [индекс Evidence](EVIDENCE_INDEX.md), [каталог функций](research/FUNCTION_CATALOG.md) дают пути, размеры, хеши, координаты и links. Каждый G_FUNC JSON содержит исходный binary SHA, Ghidra version/profile, inclusive body ranges, assembly, non-source pseudocode, xrefs/callees и ограничения. LAB callbacks не всегда распознаны как отдельные функции; G_INSPECT сохраняет фактические инструкции без вымышленной границы функции.

MCP REA 6.7.0 отвечает, но его Ghidra provider не запустился: в MCP отсутствовало окружение, а установленная Ghidra **12.2** не допускается guard REA для 12.1.x. Прямая Ghidra 12.2/JDK25 **реально завершила анализ и декомпиляцию**. Сохранён проект `docs/research/ghidra_projects/PSE_2022_cpu2`; первый запуск 4G heap упал, успешный использовал 8G и cpu.core.limit=2. [Точные блокеры/логи](research/TOOLCHAIN_BLOCKERS.md). Не повторять import/декомпиляции, если сохранённого dossier достаточно.

При конкретном пробеле использовать сохранённый проект без auto-analysis:

```powershell
& 'C:\Python314\python.exe' '.\docs\research\invoke_ghidra.py' function <фактически установленный VA>
```

Сначала проверить SHA и наличие G_FUNC. Это исследовательский exporter, не исходники движка. PowerShell .ps1 policy не менялась; launcher на Python направляет проект/JVM caches/temp/logs внутрь мода.

## Ключевые подтверждённые находки

| Роль / статус | Preferred VA / RVA | Evidence |
|---|---|---|
| Game/state tick candidate; guards и порядок calls VERIFIED | `0x1401b6ec0` / `0x1b6ec0` | [tick](evidence/ghidra_2022/G_FUNC_1401b6ec0.json) |
| Player/controller update, role INFERRED | `0x1401dfe30` / `0x1dfe30` | [player update](evidence/ghidra_2022/G_FUNC_1401dfe30.json) |
| Factory type0 → Puppet, class check, link/spawn VERIFIED | `0x1401d4780` / `0x1d4780` | [factory](evidence/ghidra_2022/G_FUNC_1401d4780.json) |
| Puppet allocation `0x1e0` bytes and vtable VERIFIED | `0x1401db790` / `0x1db790` | [allocator](evidence/ghidra_2022/G_FUNC_1401db790.json) |
| Puppet Spawn singleton rebinding VERIFIED | `0x1401dbc60` / `0x1dbc60` | [Spawn](evidence/ghidra_2022/G_FUNC_1401dbc60.json) |
| GameInstance Player getter returns this+0x40 VERIFIED | `0x1401b5520` / `0x1b5520` | [getter](evidence/ghidra_2022/G_FUNC_1401b5520.json) |
| Puppet update depends on owner; movement calls VERIFIED | `0x1401db8a0` / `0x1db8a0` | [Puppet tick](evidence/ghidra_2022/G_FUNC_1401db8a0.json) |
| Collision/move trace candidate; scratch/callbacks VERIFIED | `0x1401a4990` / `0x1a4990` | [collision](evidence/ghidra_2022/G_FUNC_1401a4990.json) |
| InflictDamage script binding + health write VERIFIED | `0x1401a1370` / `0x1a1370` | [damage](evidence/ghidra_2022/G_FUNC_1401a1370.json) |
| Weapon ChangeAnim executes frame actions immediately VERIFIED | `0x14020f760` / `0x20f760` | [animation/actions](evidence/ghidra_2022/G_FUNC_14020f760.json) |
| Node callback / deferred retirement VERIFIED | `0x1401d3230`, `0x1401b6360` / `0x1d3230`, `0x1b6360` | [callback](evidence/ghidra_2022/G_FUNC_1401d3230.json), [retirement](evidence/ghidra_2022/G_FUNC_1401b6360.json) |
| LoadMap identity corroborated; transition behavior not executed | `0x1401c7b70` / `0x1c7b70` | [LoadMap](evidence/ghidra_2022/G_FUNC_1401c7b70.json) |

Наблюдаемый цикл: state dispatcher → player update → guarded world/scripts/level/node callbacks → HUD/lifetime/audio listener. Его частота, threading и wall-clock accumulator UNKNOWN. Actor nodes update через callback с per-tick stamp guard и virtual+0x20; sector relink — `0x1401d3d70`. Remove, deferred retirement, pool return и deleting destructor — разные этапы. Не вызывать из сетевого потока; не использовать recycled C++ pointer как entity ID. [Архитектура](02_ENGINE_ARCHITECTURE.md).

### Singleton Player/Puppet: точное следствие

VERIFIED в Spawn `0x1dbc60`: base actor Spawn; global GameInstance по preferred VA `0x140af33a0`; vcall+0xa8 получает `Player = GameInstance+0x40`; **Player+0xd8 заменяется на Puppet+0x10 node** с refcount operations; **Puppet+0x1b8 получает Player**. Штатный второй Spawn не создаёт второго controller.

```text
GameInstance+0x40 = embedded Player context
Player+0xd8 -> ObjectNode
ObjectNode+0x28 -> checked Puppet payload
Puppet+0x10 -> its ObjectNode
Puppet+0x1b8 -> owner Player
```

Это не raw Player→Puppet pointer. [GetActor callback](evidence/ghidra_2022/G_INSPECT_140194b30.json) загружает node и tail-jumps в [checked helper](evidence/ghidra_2022/G_FUNC_140197ee0.json). Простое восстановление старого handle после второго Spawn не решает новый owner, refcounts, input и lifetime.

VERIFIED getters/инструкции: Actor/Puppet origin+0x18, rotation+0x30 (factory yaw+0x38), sector+0x3c, velocity+0xe0, health `int16`+0x17a; Player buttons+0x00, artifacts+0x90, abilities+0x94, keys+0x98, questItems+0x9a, teamDolls+0xa4. Ankh helper дополнительно читает/пишет Player+0x84/+0x86. [Player lifecycle](03_PLAYER_LIFECYCLE.md), [API/asm map](research/SCRIPT_API_2022.md). Не превращать частичный layout в готовый header; один offset имеет разный смысл у разных классов (AI+0x1b8 — state, не Puppet owner).

VERIFIED KPF: `Obj_Player` использует kexSlavePuppet; AI и map scripts содержат singleton Player() calls. `p_enemy:824–827` наносит boss damage глобальному player; `p_misc:316` назначает projectile sourceOwner через Player().GetActor(); TOMB/TOMBEND используют buttons/locks/quest progress. [E010](evidence/E010_KPF_SELECTED_TEXT.json). Только native GetPlayer fix недостаточен для всей кампании.

## Full Player и Player Proxy

| Проверка | Full Player: отдельный Player context у каждого участника | Player Proxy: один локальный Player + дополнительные entities/sidecars |
|---|---|---|
| Положительное основание | Native Puppet movement/damage/artifact pickup уже используют owner Player; есть instance getters и embedded context | Общие Actor lifecycle/health/collision существуют и принимают actor/context arguments |
| Главный блокер | Штатный Spawn всегда выбирает singleton; корректный отдельный Player constructor/weapon lifecycle и remote input path UNKNOWN | Stock Puppet Spawn тоже выбирает singleton; Puppet Tick/Damage разыменовывают owner. Null owner или только синхронизация transform не подходят |
| Pickups | Artifact handler берёт owner touching Puppet, что потенциально поддерживает раздельный state | Artifact/ankh handlers требуют Puppet runtime type; обычный Actor proxy может не пройти проверки |
| Оружие/физика | Потенциально сохраняет штатный gameplay, но presentation/global input надо отделить | Нужны доказанные remote movement/weapon/instigator adapters; возможное повторное создание большей части player logic |
| Глобальные системы | Script Player(), AI fallback, HUD/listener/campaign и ankh path требуют policy/context boundary | Те же проблемы; один local Player не решает world targeting, collector keys или достижения remote participants |
| Восемь игроков | 8 контекстов с независимыми command/weapon/node/owner; управляемое local presentation | 7 remote sidecars с равными gameplay contracts и безопасной type identity; их число не ограничивать маской 4 |
| Что статически не доказано | Reentrancy/init/teardown нескольких Player, native tick для remote context | Возможность полноценного proxy без отдельного native Player, collision/pickup/weapon equivalence |

**Рабочий кандидат первого эксперимента — Full Player**, поскольку owner-specific штатные пути дают более прямую возможность сохранить gameplay. Это INFERRED приоритет проверки, **окончательный выбор UNKNOWN**. Proxy — резервный эксперимент, если native Player невозможно корректно изолировать; автоматически более простой или работоспособный путь не установлен. Если Proxy требует полноценного Player owner для штатного Puppet, он теряет предполагаемое преимущество.

Минимально инвазивный DLL-подход должен отделить presentation и закрепить узкие context/lifecycle seams, сохраняя EXE на диске. C01 подтвердил первые такие seams через Frida/native adapter, но не production DLL и не весь M0. Глубокая модификация может явно протащить player identity через input/physics/AI/events/scripts/save, но потребует существенно больше adaptation; исходников2022 нет. Скриптовый API сам по себе не предоставляет доказанный независимый Player construction; текущий owned initialization adapter версионный и экспериментальный.

## Shared Achievement Progress — обязательные требования

VERIFIED: из встроенной JSON-строки по VA `0x140529230` извлечено **20** achievement definitions с ordinal 0–19, и найдены все 20 отдельные `ACH_*` keys. [E018](evidence/E018_ACHIEVEMENT_DEFINITIONS.json), [E017 strings/xrefs](evidence/E017_ACHIEVEMENT_ANCHORS.json). Это инвентаризация идентификаторов, не проверка каждого условия/работы Steam. Орфографию native `ACH_*_ANKAHS` сохранять точно.

Требование пользователя для всех 20: выполненное командой/участником условие получает каждый **участник соответствующего события**, независимо от collector/last hit/activator, при сохранении ограничений достижения. Хост — единственный источник earned eligibility. Каждый процесс применяет только награды своего пользователя через существующий Steam service. Хост не может считать вызов своего local unlock выдачей другим Steam accounts.

### Штатная Steam цепочка

| Адрес / RVA | VERIFIED static observation / значение |
|---|---|
| `0x14018ed40` / `0x18ed40` | SocialManager Init вызывает импорт SteamAPI_Init и регистрирует callbacks. [dossier](evidence/ghidra_2022/G_FUNC_14018ed40.json) |
| `0x140182cc0` / `0x182cc0` | SteamAPI_GetHSteamUser → FindOrCreateUserInterface с **STEAMUSERSTATS_INTERFACE_VERSION012**. [dossier](evidence/ghidra_2022/G_FUNC_140182cc0.json) |
| `0x140184a90` / `0x184a90` | OnUserStatsReceived проверяет app identity/result=1, ставит readiness service+0x11c и заполняет cache. [dossier](evidence/ghidra_2022/G_FUNC_140184a90.json) |
| `0x140184110` / `0x184110` | SetAchievement diagnostic; initialized/enabled/cheat gates, lookup/already-achieved checks, Steam vcall+0x38, затем+0x50. [dossier](evidence/ghidra_2022/G_FUNC_140184110.json) |
| `0x1405257b0` / `0x5257b0` | kexSocialAchievementsSteam vftable; +0x10 → 0x140184110, +0x28 → progress 0x140184640. [symbols](evidence/ghidra_2022/G_ACHIEVEMENT_SYMBOLS.json), [memory](evidence/ghidra_2022/G_MEMORY_1405257b0.json) |
| `0x14018fbf0` / `0x18fbf0` | Existing social update вызывает SteamAPI_RunCallbacks при enabled flag. [dossier](evidence/ghidra_2022/G_FUNC_14018fbf0.json) |

Gameplay paths вызывают `DAT_140aeaf80` virtual+0x10 с key и дополнительным flag=0; конкретный runtime object/type/this adjustment и значение flag нужно проверить. Интерпретация Steam slots+0x38/+0x50 как SetAchievement/StoreStats — INFERRED, согласуется с diagnostic и v012 interface; не переносить эти offsets в иную SDK/сборку. [Steamworks primary reference](https://partner.steamgames.com/doc/api/ISteamUserStats) описывает local achievement storage/callbacks; target Evidence доказывает только найденные calls. Не заменять существующую интеграцию собственной SDK и не обходить gates.

Cheat gate corroborated: [0x140107820](evidence/ghidra_2022/G_FUNC_140107820.json) печатает сообщение отключения достижений и ставит bit0 по service `DAT_140aeaf70+8`; SetAchievement проверяет этот флаг. `DAT_140ae90f4` и часть остальных gates не классифицированы. Изменение их для успешного теста не является корректной реализацией.

### Проверенные характерные условия

| Путь / RVA | Какие данные используются | Следствие для shared progress |
|---|---|---|
| Artifact pickup `0x1d8ac0` | Touching object должен быть Puppet и живым; `owner=Puppet+0x1b8`, bit из actor-def+0x144, mask Player+0x90; emits шесть artifact keys по уже имеющимся bits | Событие нового конкретного артефакта и participant set выделять **до** выдачи. Не пересылать слепо все повторные SetAchievement calls |
| Ankh pickup `0x1d8870` + helper `0x1e0910` | Глобальный Player; count Player+0x84, bitmask+0x86; condition count>4 → ACH_ALL_ANKAHS. Helper сначала увеличивает count, потом проверяет bit/limit и может поставить count=8 | Нативный helper **не считать идемпотентным**: дедупликация до его вызова. Командный учёт пяти уникальных pickups, а не пять сетевых сообщений |
| Boss death `0x195050` | AI health<=0, actor definition type и boss keys SET/SELKIS/QUEEN; caller instigator/sourceOwner участвует в AI logic | Host death event один раз; награды всем участникам боя, не только последнему стрелявшему |
| Melee `0x199710` | Trace → InflictDamage → actor-def type=5, dead health → ACH_KILL_MANTIS_MACHETE | Сохранить условие оружия/способа убийства, не превращать в любую смерть mantis |
| Finish `0x1b5850` | GameInstance+0x1d8=1/2 → HARD/PHARAOH; `int16 GameInstance+0xc4==0` → NO_ANKAHS (тот же embedded Player+0x84) | Нужен исторический командный флаг нарушений и per-user qualifying participation, текущего поля одного Player недостаточно |

Evidence: [artifact](evidence/ghidra_2022/G_FUNC_1401d8ac0.json), [ankh](evidence/ghidra_2022/G_FUNC_1401d8870.json), [helper](evidence/ghidra_2022/G_FUNC_1401e0910.json), [boss](evidence/ghidra_2022/G_FUNC_140195050.json), [melee](evidence/ghidra_2022/G_FUNC_140199710.json), [finish](evidence/ghidra_2022/G_FUNC_1401b5850.json). Вход case6/7 state handler также выдаёт BAD/GOOD_ENDING. Другие условия всех 20 отдельно не исследовались по ограничению пользователя.

Две конкретные ловушки: artifact pickup повторно посылает ключи **всех** установленных bits — такой low-level вызов может ошибочно наградить late join за старый артефакт. Повторный ankh helper может изменить count даже при уже установленном bit. Поэтому authoritative событие/переход состояния должен предшествовать relay/local unlock; перехват одного SetAchievement недостаточен.

В coop необходимо также контролировать **самостоятельные штатные unlock/progress calls**, а не только доставлять дополнительные награды. Source condition — наблюдение, выдача — отдельное разрешённое host ledger решение: иначе host/client успеет получить не соответствующую его участию награду до сетевой проверки. Будущая integration boundary должна откладывать/фильтровать неавторизованные coop awards, сохраняя readiness/cheat gates и обычное single-player поведение. Native SetProgress не должен обходить командные ограничения через локальный счётчик. Без этой границы shared policy не доказана.

Минимальные зависимости будущей системы, без полного протокола:

- Host ledger: run/campaign ID, unique source event ID, mapEpoch/entity generation, actual condition/cause, participant identity set, aggregate team state, monotonic violation flags, per-user earned/pending/confirmed delivery. Не использовать display name/socket как постоянную identity.
- Мгновенный pickup: admitted участники этого coop world на событии, без lobby/waiting новых peers. Boss: участники соответствующего боя, включая погибшего участника, если он участвовал; границы battle participation определить явно и тестировать. Отсутствующий участник не получает новые события, но сохраняет ранее заработанную pending награду.
- Team ankh/power-up/artifact conditions отделены от personal/shared gameplay inventory. Любой ankh любого участника делает team no-ankhs недопустимым до конца соответствующего run; смерть/respawn/выход/новая карта не сбрасывают нарушение.
- Campaign finish: хранить квалифицирующую сложность/покрытие прохождения для каждого участника. Mid-campaign newcomer получает будущие event rewards, но не получает прошлые pickups/bosses и не получает full-run award лишь за присутствие на финале. Reconnect того же участника восстанавливает его доказанное участие. Консервативный первый критерий full-run — все требуемые участки qualifying run; точные границы требуют product rule при реализации.
- Reconnect/dedup: полученный повторно earned event не повторяет pickup/condition computation. Можно доставить сохранённую награду за событие, в котором этот пользователь участвовал; отправка ему всех host unlocked keys недопустима. Local Steam readiness/persistence/error/offline retry отделены от network ACK. Уже разблокированная награда не требует clear/reset.

Динамическая проверка Steam: реальные отдельные accounts, валидный Steam app context, callback readiness, actual service object в DAT_140aeaf80 и ABI, успешный новый award и подтверждение stats storage; затем duplicate/reconnect/offline delay/late join. Логирование без выдачи сначала. Никаких test unlock/reset достижений на текущем этапе не делалось.

## Первая конкретная задача реализации

1. Прочитать AGENTS, [03](03_PLAYER_LIFECYCLE.md), [07](07_HOOK_CANDIDATES.md), [09](09_POC_PLAN.md), этот handoff; проверить SHA. Не делать новую общую декомпиляцию.
2. В будущей disposable runtime среде измерить tick thread/order, фактический singleton/Player/Puppet/node graph. Только observation/logging, без сети/save/achievements.
3. Проверить обычный второй Spawn и его singleton effects; затем Full Player lifecycle/context experiment. Обеспечить корректные ссылки и отделить input/local presentation; не memcpy Player/Puppet и не подставлять null owner.
4. Две независимые команды, movement/collision, одно оружие/damage/death и spawn/remove/transition cycles. Пройти критерии M0 из 09. Если Full Player не проходит — ограниченный Proxy эксперимент с теми же gameplay критериями, затем выбрать путь по результатам.
5. После M1 — ENet на двух машинах, затем authority мира, кампания/shared achievements и только затем 8-player acceptance. Уже на M0 sidecars/IDs/arrays проектировать под 8, не фиксировать masks=2/4.

Критические UNKNOWN: отдельный native Player constructor/weapon state lifecycle; remote command injection без shared hardware reads; полнота owner-dependent paths; допустимость proxy типа для pickups/AI/collision; suppression client animation actions; полная world baseline для mid-level join; exact ABI/threading/unwind любого detour; устойчивые account↔participant identity и Steam completion semantics. Сохранённые данные достаточны для проверки этих гипотез узкими прототипами; они **не закрывают** сами гипотезы.

## Критерий передачи

Handoff содержит build identity, конкретные точки/смещения/Evidence, главный отказ обычного Spawn, обе архитектуры с рисками, M0 experiments, network scope и проверенные Steam condition/unlock paths. Поэтому следующий шаг — экспериментальная реализация M0 в отдельно разрешённой среде, а не дальнейший широкий реверс. Успех кооператива, Proxy, Full Player или Shared Steam unlock не заявляется.
