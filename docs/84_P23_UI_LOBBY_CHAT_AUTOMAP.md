# DOC-84 — П23: лобби/чат v1.13 и UI-модель; native gates открыты

> Исторический backend-этап. Native продолжение и актуальный статус пакета —
> [DOC-85](85_P23_NATIVE_MENU_CHAT_AUTOMAP.md), независимая приёмка —
> [DOC-86](86_P23_INDEPENDENT_ACCEPTANCE.md). Ниже сохранён итог прежнего этапа.

2026-11-06. **Пакет: REVIEW / PARTIAL, режим В (поднят с Б). Целевой PASS не
достигнут.** Исполнитель — Codex по назначению пользователя; независимая проверка —
отдельный агент Codex `p23_independent_review`. Backend принят в проверенных
границах, весь пакет не принят: [независимый итог](evidence/m1/P23_UI/INDEPENDENT_REVIEW.md).

## 1. Требования и решение

Объём: только П23/S8.1–S8.3. Опоры: [DOC-15](15_COOP_2015_FEATURES.md),
[DOC-12](12_AUTOMAP_ALLIES.md), ROADMAP §1/§3/§4. Изменять принятые native lifetime
contracts, Steam, достижения, user saves, П7 gameplay и старые протоколы не требовалось.

В принятом wire нет чата. Пользователь явно принял **отдельную v1.13 поверх
v1.12 и режим В с независимой приёмкой**, [точный вопрос/ответ](evidence/m1/P23_UI/DECISION_V113.md),
[предложение](evidence/m1/P23_UI/PROTOCOL_PROPOSAL.md). Это разрешение на реализацию,
не runtime PASS. Решение учтено как D13 в ROADMAP. Host-authoritative архитектура,
identity-only v1.11 и scope v1.12 сохраняются.

Target EXE SHA-256:
`b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`.
Обе копии проверены fresh offline runner и текущими integrity receipts. **Новых
игровых runs, hooks, DLL-loader calls, Actor reads/writes и native UI не было.**
REA использована только для `list_frida_devices` (local device доступен), без
создания/attach/spawn/закрытия сессий. Навык `reverse-engineer-anything` применён
для оценки native опоры; новый широкий реверс не выполнялся.

## 2. Что реализовано

Код: [UI backend/контракт/воспроизведение](../experiments/pse_together/src/ui/README.md),
[изолированный fork](../experiments/pse_together/src/wire113/README.md).

| Задача | Реализовано | Не сделано для «Готово, когда» |
|---|---|---|
| S8.1 | Модель формы host/join: имя, IPv4/bind, порт, token; настоящие ENet create/connect, до8 участников, auth/отказ/реконнект/очистка | Меню/кнопки в KEX, потоки/команды и связь с native gameplay admission |
| S8.2 | Typed roster, Unicode names/chat, authenticated attribution, join/leave notices, bounded history64, rate/replay controls, ping | Native таблица и HUD, ввод T/Enter/Esc и backslash, отрисовка чата/уведомлений, индикация native health/dead/wait |
| S8.3 | Value-only marker builder: local slot, цвета8, grey dead, высота/alpha, finite/generation/epoch/order/stale filters | Host-authoritative frame binder, native позы/состояния, marshalling, реальная отрисовка на автокарте |

Все endpoint-слоты пока **Waiting**. Backend не тикает mock WorldSimulation и
не выдаёт её координаты/health за KEX. `Model::pose` в тестах получает явно
созданные contract inputs; это не наблюдение игры и не wire-передача native поз.

Новый `pse_core_p23` использует собственные headers и изменённые копии трёх `.cpp`.
Остальные исходники наследуются из v1.12; native DLL-цели этот core не выбирают.
Канонические принятые DLL/лаборатории не заменялись. v1.10 header равен принятому
SHA, полные wire111/wire112 деревья равны accepted P7 RUN03 source snapshot:
[source checks8/8 + source freeze](evidence/m1/P23_UI/FINAL_CHECKS02/SOURCE_CHECKS.json).

## 3. Проверенная сетевая граница

Новые сообщения0x60 Request /0x61 Broadcast /0x62 Roster, только reliable
channel0 и правильное направление. Сервер принимает автора только из
authenticated peer ledger и сверяет connection generation. Клиент получает
подтверждённые имя/slot/gen, не авторство из произвольного текста. Чат ≤90 Unicode
scalar values/360 UTF-8 bytes, имя≤31 bytes; malformed UTF-8, C0/C1 и явно
перечисленные bidi/format controls отвергаются до сериализации, не обрезаются.
Это не полная Unicode Cf-классификация. Дедупликация,2msg/sec/gen, atomic
roster и bounded64 FIFO. **Late-join history ограничена текущими поколениями:**
строки ушедших или сменивших generation участников повторно не выдаются.

WELCOME v1.13 добавляет явный byte наличия native map identity после mapName.
Отсутствие структуры разрешено только для `LOBBY`; этот stage не означает
MapReady/Spawn. Native stage сохраняет обязательную структуру. Downgrade нет.
Проверяются version/session/slot/gen/epoch/rates/key до authentication.

Независимый reviewer нашёл и исполнитель исправил F1/F2/F3/F5/F6/F7:
localhost-only bind; повторный pending Connect; отсутствие late-join history;
повторный HELLO, занимавший несколько слотов одним peer; утечку reconnect key при
смене endpoint/participant; старые slots/session после restart. Также исправлены
cleanup terminal transport **после** receive-loop и баланс ENet initialization
на failure paths. [Таблица исправлений и границы](evidence/m1/P23_UI/INDEPENDENT_REVIEW.md).

## 4. Машинная проверка и ошибки попыток

- [Fresh full offline](evidence/m1/P23_UI/OFFLINE_FINAL/RESULT.json): **34/34 suites**,
  configure/build/CTest exit0, проверены summary patterns каждого набора. Вся
  прежняя регрессия32/32 и новые2набора проходят; runtime KEX этим не доказан.
- [UI/codec raw](evidence/m1/P23_UI/OFFLINE_FINAL/logs/p23_ui_contract.log):
  **510/0**: UTF-8/границы, Welcome lobby/native, каждое усечение payload,
  trailing/malformed fields, spoof/dedup,8слотов, late/stale generation/map,
  marker colours/height/dead, clearing and bounds.
- [Real ENet raw](evidence/m1/P23_UI/OFFLINE_FINAL/logs/p23_lobby_live.log):
  **76/0**, хост+7клиентов, реальные UDP sockets; Unicode/auth/roster/chat,
  HELLO/Connect negative, forged channel/direction/reliability/gen/trailer,
  replay/flood, leave/reconnect gen2, history, bad version/token, server shutdown,
  restart/fresh session, terminal reconnect и scope key reset.
- Wildcard bind проверен через реально assigned Preferred IPv4. **Один ПК**,
  не П5 и не LAN/Radmin между двумя ПК. CMake передаёт выбранный адрес в
  `P23_TEST_LOCAL_IP`; независимый запуск проверяющего использовал192.168.56.1.
- [Самостоятельный запуск проверяющего: команды/stdout/SHA](evidence/m1/P23_UI/INDEPENDENT_RUNS.json),
  [вердикт](evidence/m1/P23_UI/INDEPENDENT_REVIEW.md):
 510/0+76/0, SHA обоих EXE совпадают с финальными файлами по source checks.

Неудачные попытки сохранены: OFFLINE01 — неверный enum-тип в test helper;
OFFLINE02 —33/34, Welcome без native structure на стадии лобби не принимался
унаследованным parser, затем test продолжал работу с отсутствующим roster и
упал. Исправлены explicit lobby stage и early test failure. `ctest04/05` —2/2.
`ctest06` —1/2: неназначенный alias127.0.0.2 не дал нужного ENet source-IP match;
это не объявлено проблемой LAN вообще. Условия изменены на assigned interface.
`rebuild07` — getenv warning-as-error, заменён GetEnvironmentVariableA;
`ctest08` —2/2, затем полный fresh34/34. Отдельная ошибочная попытка пути к VS
CMake отражена в текущем чате; использован найденный `C:/Program Files/CMake/bin`.

## 5. Integrity, exact restores, завершение

[До](evidence/m1/P23_UI/INTEGRITY_BEFORE/RESULT.json) и
[после](evidence/m1/P23_UI/INTEGRITY_AFTER/RESULT.json): **85/85+19/19**,
переисполнены сохранённые независимые audit83/supplement83 с заменой только
ROOT/OUT, predicates не менялись. Это повторное чтение retained raw, не повтор
игровых runs. Хеши аудиторов включены в receipts.

Подтверждены текущие77original files, обе independent copies, actual protected
roots (4реальных файла профиля + Steam remotecache), отсутствие изменения
сохранений и loader staging. Исторические **exact restores4host+6client+controller,
exit0 и16REA-close receipts** перепроверены в raw. Для П23 новых hook restores
нет: ни один новый hook не устанавливался. Нельзя выдавать эти исторические
receipts за проверку будущего UI/automap adapter. Игровых/диагностических процессов
после проверки нет ([финальная process enumeration](evidence/m1/P23_UI/PROCESSES_FINAL.json)); новых REA sessions0.

## 6. Исправленная native опора и следующий gate

Из существующих Ghidra12.2/Win64 Evidence и exact copied EXE, без повторного
decompile/run: [независимые подробности](evidence/m1/P23_UI/INDEPENDENT_REVIEW.md).
Аналитические имена Draw/Arrow не восстановленные символы; image base0x140000000.

| Объект | VA | RVA | File offset |
|---|---|---|---|
| Draw | 0x1401a2580 | 0x1a2580 | 0x1a1980 |
| Arrow | 0x1401a34c0 | 0x1a34c0 | 0x1a28c0 |
| Local call / return | 0x1401a326c / 0x1401a3271 | 0x1a326c / 0x1a3271 | 0x1a266c / 0x1a2671 |

**VERIFIED static:** yaw передаётся `MOVSS XMM2` и принимается
`MOVAPS XMM6,XMM2`; radius — пятый stack argument, не XMM4. Предполагаемый тип:
`void(void*,char,float,const float*,float,const uint8_t*)`. Runtime ABI **UNKNOWN**.
Local view offsets+6C/+70/+9C не raw Actor: radar Actor использует+18/+38.
this+10 сбрасывается после flush; трактовка zoom из DOC-12 не доказана.
После всей Draw render state не аттестован. Local return1a3271 — узкий кандидат,
не готовый hook contract. В DOC-12 добавлено предупреждение, исторический текст
не заменялся и не выдавался за действующий ABI.

Для PASS остаётся реализовать native меню/HUD/input и thread owner/mailboxes;
связать host-authoritative frames с текущей native map/generation; затем провести
новый изолированный игровой gate исправленного Arrow adapter, увидеть настоящие
стрелки союзников и чат двух игровых процессов, проверить выход/выгрузку/exact
restores, повторить integrity и получить независимый PASS режима В. PRE-01
из DOC-14 и прежние gates этого не заменяют. **Статусы S8.1–S8.3 и П23 до PASS
не повышены: критерии пользователя пока не выполнены.**
