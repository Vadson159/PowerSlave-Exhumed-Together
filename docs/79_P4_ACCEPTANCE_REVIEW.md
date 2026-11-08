# DOC-79 — П4: повторная приёмка S1.3–S1.5

2026-11-04. Проверяющий — Codex, отдельный чат проверки по прямому назначению
пользователя. Исполнение — [DOC-72](72_P4_REMEDIATION_AND_RUNTIME.md), первоначальный
возврат — [DOC-67](67_P4_ACCEPTANCE_CHECK.md).

**Вердикт: П4 — PASS (bounded), режим В.** Приняты anchor spawn, corridor portal
с native lerp, jitter-буфер и фоновый тик клиента в границах задания пользователя.
Блокирующих дефектов в проверенном коде и evidence не выявлено. Нативный код при
этой проверке не исправлялся; самоприёмки нового нативного кандидата нет.

Exact EXE SHA-256 всех переиспользованных native-наблюдений:
`b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`.
Игра, Frida/REA и загрузчик не запускались. Принятые исследования и игровые runs
не повторялись, новых ABI или адресов не восстанавливалось. `b60` не изменялся;
свежая сборка проверяющего — `b79`.

## 1. Объём приёмки

Прочитаны AGENTS, CURRENT_STATE, ROADMAP §1/§3/П4/S1.3–S1.5, PROGRESS,
EVIDENCE_INDEX, опоры DOC-58B.1/58A/53/56/54, native handoff и DOC-65/67/72.
Проверены production-пути host anchor, geometry/LinkSector, playout,
background controls, codec/DTO/worker/Session v1.11 и изоляция целей сборки.
Историческая статика exact EXE переиспользована из опор; аналитические имена
и ABI-ограничения остаются по [DOC-65 §5](65_P4_PRESENTATION_AND_SPAWN.md#5-координаты-и-воспроизведение).

| Задача | Принимается | Статус и предел |
|---|---|---|
| S1.3 | Сохранённый native anchor; отсутствие fallback к текущей P1; проверка поддержки и actor AABB до Reset/Spawn; ограничение ожидания 1800 eligible ticks | **PASS (bounded)**: TOMB, один P2, одна host binding; положительный stationary-witness — run03 |
| S1.4 | Взаимный копланарный corridor, переход membership нативным LinkSector, native lerp до фактической sprite submission | **PASS (bounded corridor)**; исходная широкая формулировка «любые порталы» остаётся **PARTIAL** |
| S1.5 | Буфер64, delay160 ms, rolling offset/slew/refill, long-pause discontinuity; opt-in фон с живым proxy и neutral input; Esc/resume; host comparison native имени/структуры по отдельной identity-only v1.11 | **PASS (bounded)**: один ПК/два KEX, доверенный клиент; LAN/Radmin/два ПК — П5 |

П4 не закрывает весь S1, rearm/смену мира хостом, смерть → рестарт, следующую
карту или объединение кандидатов П3/П4. Пакеты П3, П6, П8, П14 и П24 этой
приёмкой не изменены.

## 2. Воспроизводимость и исходники — VERIFIED

[OFFLINE/RESULT.json](evidence/m1/P4_ACCEPTANCE_CHECK_79/OFFLINE/RESULT.json):
свежие configure, `pse_all --parallel 4` и CTest **30/30**, с проверкой закреплённых
строк каждого набора. П4 presentation **310/0**, real loopback ENet identity
**75/0**. Это offline-проверки, не игровой sign-off.

[REBUILD.json](evidence/m1/P4_ACCEPTANCE_CHECK_79/REBUILD.json): отдельная
перелинковка в `b79` с PDBALTPATH соответствующего эталона, принятый
`dll_compare.py` с нормализацией build identity; **7/7 секций у всех трёх целей**:

| Цель | Эталон | Результат |
|---|---|---|
| `pse_dll_host_p4` | Замороженная Host DLL финального gate04 | 7/7 равны |
| `pse_dll_client_p4` | Замороженная Client DLL финального gate04 | 7/7 равны |
| `pse_dll_client_proxy` | Принятая Client DLL П1 `P1_CLIENT_03` | 7/7 равны; DOC-67 F1 закрыт для П4 |

Финальный host SHA `28735135d707a58d942aad6695b9a103facecbdbbb32f26b1397f4e1b6aad053`,
client SHA `2156d9cd37e1413ad3688a3ea981a2f9d31fb5f0f254f7e4be6b39d90fb0e53d`.
Проверены хеши DLL/PDB и **169/169** копий исходников gate04, соответствие
`NATIVE_ARTIFACT.json` реально загруженным DLL в каждой из шести ролей.
Отличия текущего дерева от snapshot04 относятся к README и П3/его expected summary;
production-исходники П4 не отличаются, что дополнительно подтверждает пересборка.
Все **32/32** исходных файла wire v1.10 равны baseline до fork.

Между snapshot02 (runs02/03) и snapshot04 изменены только `native_proxy.cpp`,
`presentation_buffer.hpp` и presentation-тест. Просмотр diff подтверждает:
native_proxy получил переиспользование vector capacities и измерение геометрии/
discontinuity telemetry; алгоритм portal/LinkSector не менялся. Host anchor,
клиентские background hooks, Session и wire111 одинаковы. Поэтому run03 —
применимое доказательство anchor и 259→123, но **не** исправления long-pause.
Последнее принимается только по финальному коду и run04.

## 3. Сырые журналы — RUNTIME OBSERVED в сохранённых runs

Новый [review79.py](evidence/m1/P4_ACCEPTANCE_CHECK_79/review79.py) независимо
читает исходные журналы, а не сводку `RUNTIME_CHECKS.json` исполнителя.
[RAW_REVIEW.json](evidence/m1/P4_ACCEPTANCE_CHECK_79/RAW_REVIEW.json) — **91/91**;
это проверки согласованности имеющихся опытов, не 91 игровой тест.
[RAW_DETAILS.json](evidence/m1/P4_ACCEPTANCE_CHECK_79/RAW_DETAILS.json) содержит
выборки, [INPUT_HASHES.json](evidence/m1/P4_ACCEPTANCE_CHECK_79/INPUT_HASHES.json) —
хеши прочитанных входов.

- **Anchor.** Во всех трёх host runs ровно один initial Reset и Spawn по
  сохранённому anchor; READY v1.11 предшествует Spawn. В run03 stationary P1
  находится вне anchor AABB до и после вызова. Код проверяет всю перепись акторов
  на том же Sim thread до инициализации/фабрики; expiration не вызывает fallback.
- **Порталы.** Run03: 259→123→176; run04: 123→176. Для каждого из трёх пересечений
  есть sprite с новой sector membership и промежуточным native output в пределах
  100 ms после LinkSector. Проверены actual output bits отдельного native lerp
  и actual sprite entry следующего render pass, revision, frame, angles, flags,
  scale и полный census с ровно одним собственным node, без двойников/unknown.
  Run03: 474 корреляции из 237 записанных lerp; run04: 326 из 164. Два renderer
  вызова одного pass не объявляются независимыми lerp.
- **Буфер.** В run04 после long pause первая секундная запись восстановления
  уже имеет backlog8,425212 ticks (~140,4 ms), reanchors1/discontinuities1;
  последующие записи не накапливают многотысячный долг. Дрейф ±100/±500 ppm,
  stalls и 100 s gap проверены также численно; 200 из310 assertions — producer
  loop, а не 200 разных сценариев. Старый run03 с дефектом long pause остаётся
  отрицательным evidence этого случая.
- **Фон.** В финальном run04 есть local_this_tick1 с нулевыми buttons/held
  на Sim thread между Spawn и Remove живого proxy; focus lease возвращает byte1.
  **Esc/resume — run02**: девять записанных paused samples при живом proxy,
  остановленный счётчик local_updates и последующее возобновление. Не приписывается
  финальному run04; применимость обеспечена неизменностью background/control кода.
- **Lifetime/выход.** Во всех трёх клиентах один Spawn/Remove/retirement;
  после Remove собственный payload null, refs0, in_use0, sector occurrences0,
  слабые указатели null. Три наблюдения retirement не заменяют широкий unload gate.
  Нативные действия П4 — на соответствующих Sim TID; faults/blocked/controller stop0,
  проверки P1 EQUAL, все записанные restores exact16. Все шесть retained и duplicate
  OS handles signaled, exit0; protected_before/after совпадают.

## 4. Закрытие DOC-67 и неблокирующие замечания

| Находка DOC-67 | Вывод |
|---|---|
| F1 | П4 не меняет код принятого клиента П1, новая сверка7/7. Хостовая находка П14 остаётся за П14 |
| F2 | Rolling clock/refill и long-pause correction подтверждены final code/tests/run04 |
| F3/F4 | Отдельные archive outputs, private build; неизменяемые DLL/PDB/sources до runtime и хеши каждой роли проверены |
| F5 | 1800 eligible ticks и отказ без fallback; лимит проверен offline, постоянная занятость в KEX не разыгрывалась |
| F6 | Несмежный teleport, смена caches/water и неоднозначные apertures остаются отказом; принимается только corridor |
| F7 | Переиспользование capacities проверено по коду; чтение270 секторов измерено в run04; это не FPS-профиль всех карт |
| F8 | Actual lerp → renderer после native LinkSector подтверждён в runs03/04; журнал sector и Stopped исправлены |
| F9 | Добавлены содержательные clock/stall/long-pause fixtures; число checks не выдано за число экспериментов |

**N1 — точность DOC-72.** В run04 P1 **не stationary**: brackets вокруг Spawn
16:53:55.820Z различаются по координатам (перед — y−6915,1313, после — y−6929,9731;
anchor y−6775). Они подтверждают удаление от anchor, но не измеряют точную позицию
между samples. Положительный stationary-witness — run03; host-код у runs одинаков.
DOC-72 и текущие статусы уточнены. Run02-фон заканчивается до proxy Spawn, а его
observer не записал коррелированные intermediate sprite submissions; эти свойства
принимаются по runs03/04, не по run02. Его 69 native lerp не считаются такими sprites.

**N2 — сохранённые пределы.** TOMB/270 секторов, один passive Actor1007, один
reciprocal coplanar portal за применённый шаг, равные floor/ceiling caches и dry
water state. Произвольные карты, water/cache-changing, split apertures, несколько
порталов за шаг и несмежные teleport не приняты. Положительные held Attack/mouse,
отдельная Pause-key и native timeout1800 остаются UNKNOWN. Неизменённые
диагностические инварианты P1 и ограничения lifetime базового П1 сохранены.

**N3 — identity-only v1.11.** Решение D9 выполнено: host сравнивает native имя,
psmVersion/countA/countB до promotion/baseline/eligibility; authenticated generation
назначается peer. Mismatch текущего peer отзывает право до reconnect; чужая
generation не может его отозвать. Legacy v1.10 не расширен, downgrade отсутствует.
Это проверка честного несовпадения структуры, не content identity и не защита от
лжи клиента. Поля П6 остаются будущей v1.12 по D10.

## 5. Целостность, ошибки стенда и воспроизведение

Новый read-only аудит подтвердил original **77/77**, отсутствие лишних файлов,
совпадение защищённых профилей с baseline и exact EXE обеих копий; данные есть в
RAW_REVIEW. Сохранены 63 уникальных REA close receipts без isError; их ответ
`target_state:unknown` не выдаётся за новую live-перепись REA. Свежая проверка
процессов — [PROCESS_CHECK.json](evidence/m1/P4_ACCEPTANCE_CHECK_79/PROCESS_CHECK.json).
В этой проверке игровых процессов и REA-сессий не создавалось.

Первый вариант checker79 дал85/89 из-за четырёх неверных предпосылок проверяющего:
run02 вместо04 для live background, ожидание intermediate sprite rows в observer02,
stationary run04 и Esc в04 вместо02. Его код/результат сохранены как
[review79_first.py](evidence/m1/P4_ACCEPTANCE_CHECK_79/review79_first.py) и
[RAW_REVIEW_FIRST.json](evidence/m1/P4_ACCEPTANCE_CHECK_79/RAW_REVIEW_FIRST.json).
Исправлены привязки к фактическим runs и явно сохранён предел moving brackets;
ни игровые логи, ни runtime assertions не редактировались. Два служебных KeyError
при разборе host seq/client record и session_id/id описаны в
[HARNESS_ERRORS.md](evidence/m1/P4_ACCEPTANCE_CHECK_79/HARNESS_ERRORS.md).

```powershell
python -X utf8 -B experiments/pse_together/tools/run_offline.py --build-dir b79 --out <NEW_OUTPUT>
python -X utf8 -B docs/evidence/m1/P4_ACCEPTANCE_CHECK_79/rebuild79.py
python -X utf8 -B docs/evidence/m1/P4_ACCEPTANCE_CHECK_79/review79.py
```

`rebuild79.py` меняет только собственный `b79`; новая offline-папка обязательна.
Проверка links/статусов и итог —
[ACCEPTANCE.json](evidence/m1/P4_ACCEPTANCE_CHECK_79/ACCEPTANCE.json).

**П4 принят в указанной ограниченной области. S1.3 и S1.5 — PASS (bounded);
S1.4 — PASS (bounded corridor), широкая задача — PARTIAL. S1 в целом не закрыт.**
