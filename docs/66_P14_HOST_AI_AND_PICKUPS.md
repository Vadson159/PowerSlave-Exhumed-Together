# П14 — AI и подбор на хосте

2026-11-04, исполнитель Codex/Sol. **REVIEW / PARTIAL, режим В (повышен с Б)**.
Задачи: только S4.1 и S4.2, BLK-01…05. П1 принят в DOC-64.
Новая логика выбирает нативные объекты, меняет область действия GetPlayer и
вызывает native pickup consume: нужны независимая проверка и игровые gates.
Этот отчёт не закрывает S4 и не является приёмкой другой моделью.

EXE SHA-256: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`.
Прочитаны AGENTS, CURRENT_STATE, ROADMAP §1/3/4.1/4.2, DOC-17/18/20,
M0_TO_M1_HANDOFF и относящиеся к задачам сохранённые dossiers. Принятые
исследования и runs не повторялись. Широкий реверс не выполнялся.

## Реализация

Отдельная цель `pse_dll_host_p14`, флаг `PSE_FEATURE_P14` (только HOST).
[Модуль](../experiments/pse_together/src/native/gameplay_p14.cpp),
[политика](../experiments/pse_together/src/native/gameplay_policy_p14.hpp),
[профиль](../experiments/pse_together/src/native/gameplay_profile_p14.hpp).
Флаг не включён в стандартные host/client и кандидаты П4. Их игровые
алгоритмы этим пакетом не меняются; rebuilt DLL не объявляются равными
принятым DLL (см. сравнения ниже).

**S4.1, VERIFIED implementation / UNKNOWN gameplay.** Idle без target и Chase
без payload выбирают живого игрока по горизонтальному расстоянию среди
прошедших штатные sight bounds, native FOV и LOS. Равные расстояния разрешаются
по слоту. Учитывается native no-target. Действующая цель и native retaliation
сохраняются. Оригинальные Idle/Chase сами назначают цель, ведут refcount, звук,
анимацию и chase timer. Adapter не пишет target/refs и не вызывает SetTarget.
При отсутствии видимой цели singleton/assert path не вызывается. Это не
доказывает остановку ActorTick/скорости внешним AI Update — нужен runtime gate.

Реестр и селектор имеют восемь позиций. Текущий host регистрирует только
проверенные P1/P2 (Player→node→Puppet→owner, vtable, destroyed latch). Нативное
создание P3…P8 остаётся в П22. Actor pointers не кэшируются модулем между
вызовами; реестр получается заново на attested Simulation TID.

**S4.2, VERIFIED implementation / UNKNOWN gameplay.** Health/Ammo вызывают
оригинальные OnTouch, сохраняя native caps, scaling, выбор weapon slot,
fullAmmo, sound/HUD и persistence. GetPlayer меняется только на получателя
проверенного toucher и только в конкретном call site. Невалидный toucher
отвергается без fallback на P1. Scope сериализован, ограничен Simulation TID
и снимается с exact16 восстановлением leaf. Вложенный HUD/cleanup GetPlayer
не перенаправляется. После activation ошибка/потеря authority не возвращает
одиночные обработчики; fail/stop инвалидируют scope.

Анкхи — shared team, bits 0…4 по фактическим `defs/actors/items.txt`.
Оригинальный Ankh OnTouch **не вызывается**: в нём есть Steam-вызов при пятом
улучшении, D4 не принят. Вместо этого вызывается штатный `GiveAnkh` только для
ещё отсутствующего бита у живого получателя. Проверяются mask/count до
первой выдачи всем получателям, затем AL, mask, count и HP по native формуле
`(newCount+1) × (difficulty∈{1,2} ? 100 : 200)`. Повтор native helper не
вызывается: его duplicate path принудительно ставит count=8.
Dead recipients не лечатся и не воскрешаются; недостающие биты остаются
отложенными в памяти хоста. Ошибка helper фиксируется без повторной выдачи.

После успешной обработки выполняется штатный pickup consume `1d8360` с
native persistence/audio/Remove. Adapter не вызывает Remove отдельно,
не пишет flags/refs и не читает pickup после consume. Новый путь native
consume для map-owned pickup предъявляется рецензенту относительно запрета
AGENTS на ручной Remove чужих Actor; никакое исключение из AGENTS не вводилось.
Реестр анкхов живёт только в текущем host-процессе. Campaign/sidecar/save-load,
правила участия и Steam относятся к П17/П18/П21.

Все новые хуки/трамплины и storage закреплены до process exit. Перед native
unbind/StateExit/DirectQuit новые хуки отключаются с exact16 проверками;
принятый native Reset внешнего Player и lifecycle-код остаются владельцами
освобождения. Hot unload не добавлен.

## Основание адресов

VA = `0x140000000 + RVA`. File offsets и первые 16 байтов отдельно записаны
в [EXACT_PROFILE](evidence/m1/PSE_TOGETHER_P14/EXACT_PROFILE.json), полученном
из проверенной копии EXE через таблицу PE sections. Имена ниже аналитические,
не восстановленные C++ symbols; ABI — Win64, inferred по сохранённой assembly.

| Точка | RVA | ABI / источник |
|---|---|---|
| Idle | `195580` | RCX=AI, void; [dossier](evidence/ghidra_2022/G_FUNC_140195580.json) |
| Chase | `1971e0` | RCX=AI, void; [dossier](evidence/ghidra_2022/G_FUNC_1401971e0.json) |
| LOS | `195750` | RCX=AI, RDX=Puppet, AL=result; сохранённый caller Idle `1956c4` |
| AngleDelta | `ce5a0` | RCX=&yaw, XMM1=bearing, XMM0=delta; caller Idle `1956a1` |
| Health | `1d8770` | RCX=pickup/RDX=toucher; [dossier](evidence/ghidra_2022/G_FUNC_1401d8770.json); GetPlayer return `1d8807` |
| Ammo | `1d8590` | RCX=pickup/RDX=toucher; [dossier](evidence/ghidra_2022/G_FUNC_1401d8590.json); return `1d8638` |
| Ankh OnTouch | `1d8870` | RCX=pickup/RDX=toucher; [dossier](evidence/ghidra_2022/G_FUNC_1401d8870.json); original заблокирован |
| GiveAnkh | `1e0910` | RCX=Player, EDX=mask, AL=result; [dossier](evidence/ghidra_2022/G_FUNC_1401e0910.json) |
| Consume | `1d8360` | RCX=pickup/RDX=toucher; [dossier](evidence/ghidra_2022/G_FUNC_1401d8360.json) |
| Difficulty | `1ae6a0` | RCX=GI, EAX=result; GI vtable `52ad08` slot `158`, bytes `mov eax,[rcx+1d8];ret`; corroborated by helper caller |

AI GetPlayer return addresses: Idle `1955f4`, Chase `1972ca`. Прототипы LOS,
AngleDelta, difficulty и новая комбинация helper/consume пока не приняты
игровым тестом П14. Статическое основание не доказывает их runtime семантику.

## Проверки и границы

Общий запуск: `python -X utf8 -B experiments/pse_together/tools/run_offline.py
--out docs/evidence/m1/PSE_TOGETHER_P14/<новый_каталог>`.
Сборка MSVC x64 Release, собственный код `/W4 /WX`.
[OFFLINE_01](evidence/m1/PSE_TOGETHER_P14/OFFLINE_01/RESULT.json) — 26/26,
65 771 checks П14, до дополнительных исправлений.
[OFFLINE_02](evidence/m1/PSE_TOGETHER_P14/OFFLINE_02/RESULT.json) — 26/26,
после fail-closed/scope/HP fixes. Финальная
[OFFLINE_03](evidence/m1/PSE_TOGETHER_P14/OFFLINE_03/RESULT.json) — **26/26**,
**65 783 checks / 0 failures** П14, после исправления bootstrap timing и
добавления проверки virtual target в точке фактической выдачи.

[Контрактный набор](../experiments/pse_together/tests/native/p14_gameplay_contract.cpp)
компилирует тот же модуль с `PSE_P14_OFFLINE`, подставляя engine doubles:
восемь слотов, LOS/FOV, отсутствие цели, чужой toucher, узкий GetPlayer scope,
reentrant touch, fail latch, потеря authority, пять shared анкхов, повторы,
dead recipients, deferred sync, повреждённый native progress/helper failure.
Это проверки маршрутизации и оркестрации, **не KEX runtime-приёмка**.

Предварительная проверка также сверяет EXE/profiles/original и foreign-process
refusal (`M11Prepare=193`, без создания log). Выводы, хеши DLL/PDB и точное
воспроизведение сохраняются в PRE_NATIVE и run-каталогах.

Сравнения текущих пересобранных стандартных DLL с П1 run03:
[host](evidence/m1/PSE_TOGETHER_P14/P1_HOST_EQUIVALENCE.json),
[client](evidence/m1/PSE_TOGETHER_P14/P1_CLIENT_EQUIVALENCE.json) — **не равны**
после маски build identity. Клиентские исходники П14 не менял; причины всех
различий этим пакетом не устанавливались. Поэтому эти rebuilt DLL не
переобъявляются принятыми в DOC-64. Для возможного нового P14 client input
стенд привязан к замороженной Client DLL из П1 run03, без permit прокси.

## Новый native smoke (не BLK-приёмка)

`P14_HOST_01`, PID34108 / Sim35536: загрузка DLL `3d4486cb…82fa`, затем
`p14_hook_install` отказ до игровых хуков. Проверка GI vtable стояла в раннем
APC до инициализации engine globals. В титульном экране read-only probe
подтвердил GI vtable `52ad08` и difficulty target `1ae6a0`; причина раннего
отказа — INFERRED по моменту проверки, branch telemetry в той версии нет.
Процесс закрыт WM_CLOSE, retained+duplicate wait0/exit0. Этот опыт не принят.

В текущей версии bytes методов проверяются при bootstrap, а GI virtual target
проверяется перед helper. `P14_HOST_02`, PID32560 / Sim4188: новая DLL
`31ae050fa7b87cd3d418dd6fb2cce3553255526a4f3026a7adb67b905075714c`
успешно установила хуки; штатным New Game достигнут gameplay tick и
[isolation PASS](evidence/m1/PSE_TOGETHER_P14/P14_HOST_02/ISOLATION_GATE.json).
Ввод сначала был сделан до появления окна; повтор выполнялся после появления
проверенного HWND с фокусом перед каждым RETURN. Ошибки помощников сохранены.

Permit кооп-сессии не подавался, клиент/P2 не создавались. Новые feature paths
оставались dormant. При WM_CLOSE — **5/5 P14 exact16**, **4/4 control exact16**,
foreignCalls0, native fault/block0, native final exit, оба retained handles
wait0/exit0. `guards:false` относится к не созданному GuardedPlayer (его
маркеры заполняются только при Spawn); не используется как доказательство
целостности живого P2. Нативные свойства AI/pickup/анкхов этим smoke не доказаны.

Raw: [журнал](evidence/m1/PSE_TOGETHER_P14/P14_HOST_02/native.jsonl),
[REA bundle](evidence/m1/PSE_TOGETHER_P14/REA_FINAL.json),
[exit](evidence/m1/PSE_TOGETHER_P14/P14_HOST_02/OS_SIGNALED.json).
[Машинная перепроверка](../experiments/pse_together/tools/review_p14.py) —
[13/13 smoke checks](evidence/m1/PSE_TOGETHER_P14/FINAL_CHECKS.json).
Оригинальные 77 файлов, обе EXE-копии и защищённые profiles совпали с
pre-native; snapshots before/after обоих новых запусков равны. Все тестовые
процессы завершены, 5 REA sessions первого опыта и 9 второго закрыты.
Сетевой протокол, Steam и оригинальная установка не менялись.

Точные команды и привязки —
[runtime_p14.py](../experiments/pse_together/tools/runtime_p14.py),
[pre-native gate](evidence/m1/PSE_TOGETHER_P14/PRE_NATIVE_03.json),
[правила принятого launcher/loader](../experiments/pse_together/tools/RUNTIME_P1.md).
Имена `P14_HOST_01/02` уже использованы: следующий опыт требует новых IDs,
нового pre-native файла и свежего профиля. Старые bound scripts не загружать.

## Что ещё требуется для закрытия П14

1. Показать в игре P2 ближе/дальше P1, LOS/FOV, смерть текущей цели и отсутствие
   живого P1; отсутствие singleton fallback/assert; действительное native
   target ownership и release. Снять координаты/видимость/target node, не
   ограничиваться журналом выбора.
2. P1 и P2 по очереди подбирают health/ammo, включая full inventory и scaling:
   меняется только получатель, расходуется один предмет, повторы без эффекта.
3. Пять разных native анкхов и дубли: у каждого живого игрока ровно пять битов,
   count/HP согласованы, Steam не вызывается, dead Puppet не оживает.
4. Exact restores, штатный выход и целостность после активного gameplay.

Карты размещения конкретных анкхов не установлены сохранёнными evidence;
нельзя подменять карту произвольным fixture и объявлять BLK закрытым.
Текущий принятый host при смерти P2 навсегда блокируется
`secondary_dead_no_respawn_in_m11`, а snapshot contract не допускает dead P1.
Поэтому dead→alive sync и длинные lifecycle-сценарии пока проверены только
offline; их native интеграция пересекается с будущими П3/П16. П14 эти пакеты
не реализует и не закрывает. Зависимые пакеты не получают PASS на основе
данной частичной сдачи.

Нужна независимая проверка Opus high по ROADMAP, включая новые native
контракты, и недостающие игровые доказательства. Подзадача проверки исходников
в рамках исполнителя помогла найти дефекты; она не считается этой приёмкой.
