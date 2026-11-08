# DOC-86 — независимая приёмка П23

2026-11-06. **PASS, режим В: П23 / S8.1, S8.2, S8.3 приняты в проверенных ниже границах.**
Проверяющий — отдельный агент Codex `p23_independent_review`, в реализации не
участвовал. Решение пользователя D13 разрешает отдельную v1.13 и приёмку В.
Блокирующих замечаний к frozen native-кандидату 05 в этой области не осталось.
Этот вердикт заменяет предварительный REVIEW настоящего документа.

## 1. Объект и границы

Требования — [ROADMAP](ROADMAP.md), [DOC-15](15_COOP_2015_FEATURES.md) и
[DOC-12](12_AUTOMAP_ALLIES.md). Реализация — [DOC-84](84_P23_UI_LOBBY_CHAT_AUTOMAP.md)
и [DOC-85](85_P23_NATIVE_MENU_CHAT_AUTOMAP.md). Объект —
[PRE_NATIVE05](evidence/m1/P23_NATIVE/PRE_NATIVE05.json), его исходники, DLL/PDB
и завершённые `P23_HOST_05` / `P23_CLIENT_05`.

EXE SHA-256: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`.
Host DLL: `06622530d2911b837920a97e55ccc6a522c8e174c92a43c2cdf547c28eeef513`.
Client DLL: `7cbe2c17cb868004ed803bb5ea65a14d10955a70fc7c416edc920bca930db2b5`.
Все 273 frozen source copies и четыре DLL/PDB независимо сверены с manifest и
текущими файлами. Код во время run05 не менялся. v1.10–v1.12 сохранены.

Runtime-приёмка относится к **двум процессам на одном ПК, D3D11, TOMB,
одной native жизни и overlay-автокарте mode1**. Создание/подключение происходят
через собственную форму в реальном окне KEX; роль выбирается отдельной host/client
диагностической DLL. Native capacity 3–8, два физических ПК/Radmin, Vulkan HUD,
respawn/rearm и новая session после StateExit в том же процессе не аттестованы.
Это ограничения охвата; они не выдаются за выполненные проверки П23.

## 2. Независимые проверки

[Supplement RESULT](evidence/m1/P23_NATIVE/ACCEPTANCE_86/RESULT.json): **58/58 PASS**;
[скрипт](evidence/m1/P23_NATIVE/ACCEPTANCE_86/audit86.py) и
[stdout](evidence/m1/P23_NATIVE/ACCEPTANCE_86/audit86.stdout) сохранены.
Это дополнительные predicates поверх raw evidence, а не повтор игрового опыта.
Проверяющий новых KEX/REA-сессий не запускал и игровые тесты не повторял.

Обе native DLL независимо пересобраны один раз в `b86r`: **7/7 masked PE sections**,
exports и имена секций совпали с frozen05. Raw SHA различается из-за build metadata;
побайтовое совпадение DLL не заявляется. Сохранены
[host comparison](evidence/m1/P23_NATIVE/ACCEPTANCE_86/HOST_BUILD_EQUIVALENCE.json),
[client comparison](evidence/m1/P23_NATIVE/ACCEPTANCE_86/CLIENT_BUILD_EQUIVALENCE.json),
[configure](evidence/m1/P23_NATIVE/ACCEPTANCE_86/configure.log) и
[build](evidence/m1/P23_NATIVE/ACCEPTANCE_86/build.log).

Сохранённый fresh [OFFLINE_FINAL07](evidence/m1/P23_NATIVE/OFFLINE_FINAL07/RESULT.json)
проверен: **35/35 CTest**, UI/codec **574/0**, real ENet **83/0**, own-window WARP
**18/0**. Восемь endpoint-участников, generation/reset/UTF-8/limits/replay и
отрицательные wire/channel/flags случаи относятся к этим backend-тестам.
[NATIVE_FINAL05](evidence/m1/P23_NATIVE/NATIVE_FINAL05.json) — **36/36** checks
замороженного вспомогательного checker; эти числа отдельно от независимых 58/58.
Старый independent backend run 510/0 + 76/0 относится к прежним SHA и не подменяет
приёмку native05.

## 3. Доказанное выполнение S8.1–S8.3

| Задача | Итог и подтверждение |
|---|---|
| S8.1 | **PASS.** Обе читаемые title-формы CREATE/CONNECT с именами «Хост23»/«Друг23», адресом и портом реально нарисованы в KEX. Submit создаёт bounded intent; сеть/native admission происходят после full isolation и native map gates. Порядок guard → witness → admission проверен по UTC, а title intent предшествует admission. |
| S8.2 | **PASS.** Реальная таблица показывает P1 «Хост23» HOST / P2 «Друг23», Alive и ping. Во всех chat/replay records точные Unicode строки «Привет от Друг23» и «Ответ от Хост23», без лишнего opener; третий client record — replay seq2. Join/leave, roster, reliable chat и валидация проверены также real ENet тестами. Held W перед composer подтверждён native sample; все 3835 captured client samples нейтральны. Focus loss закрывает capture. Attested held attack bit этим опытом не заявляется. |
| S8.3 | **PASS.** На настоящей автокарте видны отдельные союзные стрелки: cyan на host, white на client. Все logged extra draws — host 41/41, client 14/14 — сопоставлены по XY/yaw float bits с queued host native snapshots, правильными slot/generation/epoch, caller и Sim TID. |

Независимо просмотрены сохранённые PNG run05:

- [Host title](evidence/m1/P23_NATIVE/P23_HOST_05/window_20261106_165355_197470.png)
  и [client title](evidence/m1/P23_NATIVE/P23_CLIENT_05/window_20261106_165433_855032.png):
  поля, footer/status читаемы, старого перекрытия нет.
- [Client map](evidence/m1/P23_NATIVE/P23_CLIENT_05/window_20261106_165752_198287.png),
  [host map](evidence/m1/P23_NATIVE/P23_HOST_05/window_20261106_165826_513205.png)
  и [host roster](evidence/m1/P23_NATIVE/P23_HOST_05/window_20261106_165839_135530.png).
- [Composer](evidence/m1/P23_NATIVE/P23_CLIENT_05/window_20261106_165806_579620.png)
  и [host end title](evidence/m1/P23_NATIVE/P23_HOST_05/window_20261106_165949_110466.png):
  после StateExit форма честно сообщает `Session ended. Restart game for a new session`.

У fresh client не было stock bindings в минимальном testcopy cfg; перед проверкой
автокарты/held W исполнитель восстановил принятые `tab`/`w`/`mouse_left` bindings
обычными консольными командами из host backup. Это изменение тестовой конфигурации
отражено в raw evidence и не выдано за сетевой ввод или запись игровых структур.

## 4. Безопасность, завершение и integrity

Структурно проверены отдельные public-interface/native-gameplay permits, bounded
очереди и scalar DTO между SIM/NETWORK, atomic publication callbacks/trampolines,
точный SDL window/HWND и Sim TID, release input lease при focus loss/StateExit.
Context/trampolines остаются pinned до exit. Arrow ABI исправлен: yaw float в XMM2,
radius пятый stack argument; extra draw ограничен return RVA `1a3271` и актуальным
native admission tuple. Старый uint/R8D пример DOC-12 доказательством не является.

Immediate-context preservation опирается на
[ExecuteCommandList(TRUE)](https://learn.microsoft.com/en-us/windows/desktop/api/d3d11/nf-d3d11-id3d11devicecontext-executecommandlist)
и WARP probe с pixels/выбранными getters/resize/restores. Полная независимая
аттестация каждого внутреннего KEX render state не заявляется. Все захваченные
UI observations run05 имеют errors0 и правильный render/Sim TID.

У обоих процессов восстановлены **пять различных UI hook sites exact40**
(`entry[-8..+31]`, включая patchAbove). Host имеет четыре различных control sites
exact16, client — три. Повторы control receipts при Quit не посчитаны отдельными
sites. Нет native fault/block/foreign-access/graphics-failure evidence.
StateExit закрывает native admission; DirectQuit завершает оставшийся SDK teardown.

У обоих exit0 подтверждён retained OS identity, primary/duplicate wait0, правильными
PID/path/creation и `no_emergency`; observer handles закрыты после signaled exit.
REA_FINAL содержит host130/client128 сообщений без truncation/ошибок скрипта.
REA_CLOSE05 содержит **30 успешных close-call receipts** с `target_state: unknown`;
буквальное состояние `closed` из них не выдумывается. Свежий OS query не нашёл
оставшихся относящихся к опыту процессов.

Независимо пересчитаны **77 оригинальных файлов**, обе отдельные тестовые копии
и **все пять фактически защищённых файлов**: before = after = current. Копии не
являются hardlinks оригиналов, loader stage не остался. RHI helper восстанавливает
точные owned path/hash только после доказанного exit: host extra cfg восстановлен,
client extra cfg отсутствует. [INTEGRITY_FINAL05](evidence/m1/P23_NATIVE/INTEGRITY_FINAL05/RESULT.json)
и независимые 58 predicates подтверждают это. Retained audit85/85 + supplement19/19
не выдаются за новые run05 UI hook receipts.

## 5. Остаточные ограничения

Pose0x63 использует unreliable-sequenced channel1/flags0; chat/roster reliable0.
Client TTL основан на локальном receivedMs, host freshness — на собственном clock.
Generation/epoch/sequence защищают от старой identity, но end-to-end age между
физическими ПК без clock sync остаётся UNKNOWN.

Неблокирующее замечание N к OpeningText: после T keyup 32ms tail подавляет одну
queued TEXT; второй TEXT в том же tail без нового physical key может пройти.
Обычный single opener и held-before-keyup покрыты unit tests и native05; общее
поведение произвольных IME/queued bursts не заявляется. Исправления лишней «е»
и перекрытия layout из run04 подтверждены именно в run05.

**Итог: независимая приёмка В завершена, П23 и S8.1–S8.3 — PASS в указанном
runtime/offline охвате.** Можно синхронизировать статусы ROADMAP, PROGRESS,
CURRENT_STATE, DOC-85 и EVIDENCE_INDEX со ссылкой на этот документ.
