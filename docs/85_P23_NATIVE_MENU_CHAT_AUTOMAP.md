# DOC-85 — П23: меню, HUD, чат и нативная автокарта

2026-11-06. **PASS в проверенных границах, режим В по D13.** Независимая
приёмка завершена: [DOC-86](86_P23_INDEPENDENT_ACCEPTANCE.md),58/58 predicates,
обе DLL по7/7 masked PE sections и exports. П23 и S8.1–S8.3 приняты.
Продолжение [DOC-84](84_P23_UI_LOBBY_CHAT_AUTOMAP.md). Объём только S8.1–S8.3.
EXE SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`.

## Реализация

Изолированные `pse_dll_host_p23`, `pse_dll_client_p23` используют v1.13. Принятые
DLL и исходники v1.10–v1.12 не заменяются. Новый worker сохраняет NETWORK owner
ENet/Model; Sim получает копию через mutex, исходящие сообщения идут через очередь16.
Позиции, yaw, здоровье/Alive/Dead берутся из принятого post-world native snapshot
хоста. Координаты mock simulation в интерфейс не поступают.

- F8: создание/подключение, имя UTF-8, IPv4, порт, token; Enter или кнопка
  отправляют один bounded intent. Роль определяет диагностическая DLL.
- T/Enter/Esc: Unicode чат; удержание backslash — таблица с именами, host,
  Alive/Dead/Waiting и ping. Join/leave и сообщения видны в HUD.
- При вводе текста применён принятый scoped Commands/Turning neutral gate.
  Key-up/mouse-up доходят игре; фокус закрывает capture и возвращает lease SDL.
- HUD рисуется в настоящем окне KEX собственным GDI bitmap через D3D11 deferred
  command list и ExecuteCommandList(TRUE). Нативные игровые font/Player структуры
  не изменяются. D3D11/SDL detour context, trampolines и ресурсы удерживаются до exit.
- На автокарте дополнительный вызов оригинального Arrow следует за оригинальной
  локальной стрелкой. Только Sim TID, return RVA1a3271, открытые native permit и
  MapReady, совпадающие epoch/local slot/generation, конечные свежие позы.

Public SDK/interface permit разрешён только после guard-loaded/profile protection
и exact PID/DLL binding. Он не разрешает native gameplay. Полный isolation gate,
реальный Sim tick, M11PermitTest и native MapReady остаются обязательны.
StateExit закрывает native admission, очищает UI-сессию, восстанавливает Arrow;
public UI показывает завершение сессии до DirectQuit. Следующая сессия требует
перезапуска в рамках принятого one-world-life native adapter.

## Wire и часы

D13 разрешил отдельную v1.13; Request0x60/Broadcast0x61/Roster0x62 остаются reliable
channel0 с проверкой направления/флага/authentication. Новый host-only Pose0x63:
slot u8, generation/epoch/sequence u32, x/y/z/yaw float, host sampleNs u64.
Он передаётся unreliable sequenced channel1. Reliable pose на любом канале
клиент отвергает. Поколения, эпохи и порядок проверяются в Model.

Клиент использует TTL1000ms от локального получения последнего sequenced pose.
Host sampleNs не сравнивается с часами другого компьютера. Хост дополнительно
проверяет sampleNs на своих часах. Абсолютный возраст пакета от момента отправки
и clock sync не установлены; отсутствие обновлений очищает маркер по local TTL.

## ABI и границы памяти

Повторного широкого реверса нет: переиспользованы принятые static evidence DOC-12
и `docs/evidence/ghidra_2022/G_FUNC_1401a2580.json`. Исправленный ABI Arrow:
`void(void*, char, float yawRadians, const float* xy, float radius, const uint8_t* rgba)`;
yaw — XMM2, radius — пятый stack argument. Native Arrow не равен Draw:

| seam | VA | RVA | file offset |
|---|---|---|---|
| Draw | 0x1401a2580 | 0x1a2580 | 0x1a1980 |
| Arrow | 0x1401a34c0 | 0x1a34c0 | 0x1a28c0 |
| local call / return | 0x1401a326c / 0x1401a3271 | 0x1a326c / 0x1a3271 | 0x1a266c / 0x1a2671 |

Это аналитические имена. Префикс Arrow16 `488bc448895808488968104889701848`.
Вне этой точки аргументы/renderer state не считаются установленными. Ни новых
Actor, ни Remove, ни ручной правки refs/target/SourceOwner нет. Пять UI hooks
сохраняют и проверяют 40 bytes от site−8 до site+31, включая MinHook patchAbove.
Проверка чтения охватывает все VirtualQuery regions и отвергает guard/noaccess.

## Runtime evidence и найденные ошибки

Evidence: [P23_NATIVE](evidence/m1/P23_NATIVE). Runs01/02 подтвердили только
пассивную установку: Vulkan не вызывает D3D11 Present. Они завершены exit0 с
exact restores; HUD ими не принимается. Run03 не был resumed/instrumented: bind
остановился на отсутствующем optional config, точный owned suspended PID завершён
REA helper с отдельным PRESTART_ABORT receipt. Это не runtime PASS.

Run04: реальные title-формы с «Хост23»/«Друг23», отложенный intent, полноценные
isolation gates, TOMB/epoch3571446746, native FullPlayer хоста и клиентская сессия;
чат в обе стороны, roster/пинг, реальные Arrow в обоих процессах. Raw snapshots
позволяют сверить координаты стрелок с host native values. Обнаружен дефект:
символ opening T попадал в начало текста. Финальный кандидат игнорирует textinput
до release opening key. Также разделены footer/status и устранено сравнение
абсолютных host/client clocks; Pose переведён в sequenced channel.

Run04 после StateExit показал public F8 на title. DirectQuit обоих exit0; каждый
UI hook восстановлен exact40, native controls exact16. Полные REA_FINAL без
truncation, OS retained-handle identity/signaled/observer-close receipts и
REA_CLOSE04 сохранены. Временный renderer config хоста восстановлен байт-в-байт,
отсутствовавший config клиента удалён после exact staged SHA и OS exit proof.
[INTEGRITY_AFTER04](evidence/m1/P23_NATIVE/INTEGRITY_AFTER04/RESULT.json) — PASS.

## Финальный кандидат05

[PRE_NATIVE05](evidence/m1/P23_NATIVE/PRE_NATIVE05.json),13/13 checks. Frozen DLL:
host `06622530d2911b837920a97e55ccc6a522c8e174c92a43c2cdf547c28eeef513`,
client `7cbe2c17cb868004ed803bb5ea65a14d10955a70fc7c416edc920bca930db2b5`.
Source copies и PDB сохранены рядом. Во время run05 frozen sources не менялись.

[Полный offline](evidence/m1/P23_NATIVE/OFFLINE_FINAL07/RESULT.json): **35/35**,
UI **574/0**, real ENet **83/0**, D3D11 WARP **18/0**. WARP проверяет настоящие
пиксели своего backbuffer, сохранение выбранных immediate-context states,
ResizeBuffers и exact40 restores public hooks; это отдельно от KEX runtime.
Отрицательные UDP cases проверяют reliable/wrong-channel/unsequenced Pose,
latency25ms path сохраняет sequenced flags0. OpeningText cases включают обе
очередности, первый настоящий key в том же timestamp, bounded tail и wrap.

Run05: host PID31896/Sim5884, client PID33712/Sim27416; TOMB,
epoch1450378447, generation1. Обе title-формы и имена видимы; layout исправлен.
Оба isolation gate PASS до native admission. Exact chat:
«Привет от Друг23» и «Ответ от Хост23» доставлены обоим процессам без opening T.
Реальные roster/host/alive/ping и союзники на настоящей автокарте подтверждены
PNG/raw; native позиции берутся из post-world host snapshot.

Минимальный отсутствовавший client config содержал только renderer selector,
поэтому стандартные key binds отсутствовали. Внутри свежего тестового процесса
через обычную консоль восстановлены ровно три строки принятого host config:
`bind tab "automap"`, `bind w "forward"`, `bind mouse_left "attack"`.
Это ordinary OS input без обращения к памяти игры; команды и console PNG
сохранены в `CLIENT_BINDINGS05_*` и `P23_CLIENT_05`. Затем повторно проверены
automap и held W→composer→focus-loss. Mouse down/up также отправлены обычному
окну, но отдельный предшествующий native attack bit не засвидетельствован;
подавление удержанного огня этим опытом отдельно не принимается. Эти профили диагностические,
не пользовательские сохранения.

До завершения сессии observations: host15320 overlays/3672 extra markers,
client12893/1320; render TID = Sim TID, errors0. После StateExit хост показывает
F8 и читаемый статус завершённой сессии на title, ready/requested=0, Arrow уже
восстановлен. DirectQuit обоих exit0; новые five UI restores exact40 у каждого,
native controls exact16, native faults/foreign calls0. Все OS termination
receipts связаны с точными retained process handles/PID/path/creation identity.

[NATIVE_FINAL05](evidence/m1/P23_NATIVE/NATIVE_FINAL05.json) — **36/36**.
Это вспомогательный audit, не независимая приёмка: его gaps (any UI observation
вместо всех, количество controls вместо distinct sites, existence observer-close
и chronology isolation/admission) закрыты отдельным reviewer в DOC-86.
[INTEGRITY_FINAL05](evidence/m1/P23_NATIVE/INTEGRITY_FINAL05/RESULT.json) — PASS:
original77/обе копии/actual5 protected неизменны, retained85/85+19/19 переисполнены
без повторения игровых runs. Host extra cfg восстановлен побайтово, client cfg
снова отсутствует после strict staged-SHA и retained OS exit proof.
Полные REA_FINAL130/128 без truncation; [REA_CLOSE05](evidence/m1/P23_NATIVE/REA_CLOSE05.json)
содержит30 успешных close-call receipts с provider target_state=unknown.
OS audit подтверждает отсутствие тестовых игровых/диагностических процессов.

Основные PNG:

- [Host create](evidence/m1/P23_NATIVE/P23_HOST_05/window_20261106_165355_197470.png).
- [Host map/chat](evidence/m1/P23_NATIVE/P23_HOST_05/window_20261106_165826_513205.png).
- [Client map/chat](evidence/m1/P23_NATIVE/P23_CLIENT_05/window_20261106_165752_198287.png).
- [Client held input/composer](evidence/m1/P23_NATIVE/P23_CLIENT_05/window_20261106_165806_579620.png).

Независимая приёмка: [DOC-86](86_P23_INDEPENDENT_ACCEPTANCE.md).

## Проверенные границы

Native scope наследует два игровых процесса/двух native игроков, TOMB и один
world lifetime. До8 участников, replay/rate/negative transport checks проверяются
отдельным real ENet offline-набором; восемь native игроков этим не заявляются.
HUD сейчас требует D3D11. Vulkan HUD не реализован. Рендерер выбран только во
временных тестовых копиях; оригинальная установка остаётся read-only.
Между двумя физическими ПК/Radmin новый игровой run не выполнен. Прокси/мир,
респаун, переходы кампании, achievements и продуктовый loader не расширены П23.
OpeningText runtime проверен для обычного открытия чата T и одного opening
TEXTINPUT. Произвольные IME/autorepeat bursts после release не приняты: tail
отбрасывает первый queued text, второй в том же tail без нового key может пройти.
