# DOC-72 — П4: исправления DOC-67 и новые native-проверки

2026-11-04. Исполнитель — Codex (Sol). **Исполнение передано на REVIEW режима В; последующая приёмка — PASS (bounded), [DOC-79](79_P4_ACCEPTANCE_REVIEW.md).**
Исходные runs/evidence сохранены. Уточнения приёмки: stationary anchor-witness — run03; P1 в run04 ещё двигался. Живой фон подтверждён run04, Esc/resume — run02.
Обобщённое «любые порталы/карты» не аттестовано; ограничения ниже сохраняются.

EXE SHA-256 во всех native-утверждениях:
`b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`.
Прочитаны AGENTS, CURRENT_STATE, ROADMAP, опоры DOC-58B.1/58A/53/56/54,
native handoff, исполнение [DOC-65](65_P4_PRESENTATION_AND_SPAWN.md) и возврат
[DOC-67](67_P4_ACCEPTANCE_CHECK.md). Принятые исследования и runs не повторялись.
Три новых пары процессов проверяют новые кандидаты; дополнительных декомпиляций нет.

## Результат по задачам

| Задача | Что подтверждено | Граница |
|---|---|---|
| S1.3 | Anchor Spawn P2: исходная native точка, ожидание освобождения, проверка поддержки и всех actor AABB; положительный stationary witness run03, moving brackets run04 (точная позиция между samples UNKNOWN) | Одна карта/сессия, один P2; занятость ограничена 1800 eligible ticks, затем явный отказ без Reset/Spawn/fallback. Rearm — П3 |
| S1.4 | Native переходы 259→123→176 в run03, 123→176 на финальной DLL run04; промежуточный native lerp действительно дошёл до renderer | TOMB, один пассивный Actor1007, один взаимный копланарный portal за шаг, равные caches/water state. «Любые порталы» целиком не закрыты |
| S1.5 | Адаптивные часы и refill, delay160 ms, long-pause discontinuity; живой клиент в фоне с neutral input, Esc/resume; отдельная identity-only wire v1.11 по решению пользователя | LAN/два ПК, положительные client held Attack/mouse, Pause-key и расширения П6 не проверены |

S1 в целом не PASS: П3/П5 и широкая геометрическая область остаются отдельными gates.

## Исправления после DOC-67

- **F1.** `apply`, `currentSector`, дополнительные аргументы height/point изолированы
  `PSE_FEATURE_P4`. Финальная пересборка `pse_dll_client_proxy` в `b72c` равна
  принятой `P1_CLIENT_03` по **7/7 PE-секциям** с нормализацией CodeView/PDB пути.
  [Сравнение](evidence/m1/PSE_TOGETHER_P4_REWORK_72/P1_CLIENT_EQUIVALENCE.json).
  Хостовую находку П14 не исправляли; `b60` не перезаписывали. Канонический запуск
  принятого П1 по-прежнему из замороженных `P1_*_03/native_artifact`.
- **F2.** Скользящий минимум `(receive − relative serverTick/60)` за 2 s,
  плавная коррекция offset до 0,5%, monotonic cursor и переякорение после starvation.
  Delay160 ms выбран с учётом наблюдённого DOC-67 отставания123 ms и двух шагов60 Hz.
  Нет экстраполяции; stale250 ms удерживает настоящий pose. При long-pause gap>32
  тика после starvation первый настоящий pose возобновляется как discontinuity,
  затем delay наполняется заново. Старый несэмплированный интервал не растягивается
  на минуты. При вытеснении cursor ограничен реально хранящимся диапазоном.
- **F3.** Каждая DLL имеет отдельный `ARCHIVE_OUTPUT_DIRECTORY`; `pse_all --parallel 4`
  собран без общего `.exp`/LNK1104. Новая опция runner `--build-dir b72` изолирует пакет.
- **F4.** Pre-native gate копирует DLL/PDB и исходники в evidence **до** запуска;
  launcher использует эти неизменяемые копии и сверяет их SHA, а не живую `b60`.
- **F5.** Ожидание якоря ограничено; on-unbind очищает anchor/budget. Новый MapReady
  после перехода этим не реализован: текущий host binding терминален, rearm — П3.
- **F6.** Несмежные переходы/телепорт и геометрия с недоказанными caches остаются
  отказом без мутаций. Пользователь разрешил v1.11 только для map identity;
  `sectorIndex`/discontinuitySequence П6 не добавлялись.
- **F7.** Геометрия перечитывается каждый применяемый шаг, но существующие vector
  capacities используются повторно. В run04 измеренная часть чтения270 секторов:
  samples35–38 μs, наблюдённый max115,7 μs. Это bounded measurement, не обещание FPS
  на всех картах. Жёсткая аттестация TOMB/270 секторов сохраняется.
- **F8.** Журнал фабрики пишет настоящий sector, stopped возвращает Stopped. Отступ
  от reset при коротком portal-step проверен actual renderer: в run03 первая
  промежуточная submission после LinkSector в123 имела renderY−6911,059 при
  currentY−6915,667; совпали actual output bits и sprite entry bits, identity OURS,
  census полный, membership123. Run04 повторил native lerp через123→176.
- **F9.** Добавлены сценарии 30 min ±100/±500 ppm, stall без догона/с догоном,
  длинный разрыв100 s и восстановление delay. 200 из310 checks всё ещё относятся
  к bounded-producer loop; качество доказательств определяется сценариями и raw,
  не одним числом assertions.

## Wire v1.11 — отдельное решение пользователя

[Решение](evidence/m1/PSE_TOGETHER_P4_REWORK_72/DECISION_V111.md):
«Разрешить отдельную v1.11 только для проверки имени и структуры карты;
изменения П6 согласовать позже» (ROADMAP D9).

`src/wire111` — отдельно компилируемая копия transport sources с manifest исходных
хешей; `pse_core_p4`/`pse_session_p4` не линкуются с legacy `pse_core`.
**Все32 исходных файла v1.10 неизменны**, включая `net_protocol.h` SHA
`8272aa3ef7bcd9e555e8a7b6bc12a1fe320b378dc34c8edbe6b38891e3bbac0f`.
Принятые цели используют v1.10, кандидаты П4 — только0x0111.

Welcome/MapChange несут имя и `{psmVersion:u8,countA:u64LE,countB:u64LE}`;
MapReady добавляет их после slot/epoch. Имя1–32 printable bytes без усечения,
NUL/quote/backslash; truncated/trailing payload и zero counts отвергаются.
Host сравнивает все поля с native binding до promotion/baseline/eligibility;
generation задаёт аутентифицированный peer. Current mismatch отзывает право
peer до reconnect, stale/foreign DTO не может отозвать текущую generation.
Client проверяет структуру Welcome и MapChange. Mixed headers не допускаются,
Welcome payload version тоже должен совпасть, downgrade отсутствует.

Сложность, content hash, manifest, world synchronization, snapshot sectorIndex,
подтверждение ввода и решения D2/D3 не входят. Имя/структура обнаруживают честное
несовпадение, но не доказывают равенство файлов и не защищают от лжи клиента.
Подробный layout — [wire111 README](../experiments/pse_together/src/wire111/README.md).

## Проверки и артефакты

**VERIFIED offline:** [OFFLINE_02](evidence/m1/PSE_TOGETHER_P4_REWORK_72/OFFLINE_02/RESULT.json)
**30/30**; [P4 contract](evidence/m1/PSE_TOGETHER_P4_REWORK_72/OFFLINE_02/logs/p4_presentation_contract.log)
**310/0**, [identity/ENet](evidence/m1/PSE_TOGETHER_P4_REWORK_72/OFFLINE_02/logs/p4_wire111_contract.log)
**75/0**. Последний использует настоящий loopback ENet и production Session:
roundtrip и все truncated prefixes, no truncation/DTO identity, native Welcome/
MapChange, точный positive READY, duplicate/stale, name/version/countA/countB
mismatches с нулём baseline/eligibility и без re-promotion, mixed110 header,
trailing/truncated packet и последующий корректный READY. Это не KEX acceptance.

Финальные DLL/PDB/исходники — [PRE_NATIVE_RUN04](evidence/m1/PSE_TOGETHER_P4_REWORK_72/PRE_NATIVE_RUN04.json),
**12/12** pre-native checks. Host DLL SHA
`28735135d707a58d942aad6695b9a103facecbdbbb32f26b1397f4e1b6aad053`;
Client DLL SHA `2156d9cd37e1413ad3688a3ea981a2f9d31fb5f0f254f7e4be6b39d90fb0e53d`.
Реальные файлы находятся в `PRE_NATIVE_RUN04_snapshot/artifacts/{host,client}`;
не путать их с run02/run03 Client DLL `4c38e64f…`, у которой long pause ещё сломан.
Все сравниваемые code hashes и исходники приложены к gate.

## Новые native runs — RUNTIME OBSERVED

Launcher/isolation/loader — принятые M0/DOC51/DOC53 через REA MCP. Свежие profiles,
exact EXE перед каждым launch, current-run isolation PASS на обоих процессах,
только atomic controls из JS, gameplay/proxy — внутри DLL. Нового loader нет.

| Run | Host PID / Sim | Client PID / Sim | Наблюдение |
|---|---|---|---|
|02|40012 /36936|37940 /10564|Anchor занят→освобождён→P2 Spawn; v111 READY; proxy258, native movement/lerp69; renderer7788 OURS; client Esc остановил local updates, resume восстановил их |
|03|13948 /40028|41252 /4860|Stationary P1 вне anchor; proxy259→123→176; renderer85100 OURS,474 intermediate,237 lerp; длинная пауза выявила дополнительный F2, поэтому recovery этого буфера **не принят** |
|04|41544 /38028|31288 /12540|Финальные DLL; moving P1 brackets вне anchor (не stationary, см. DOC-79); proxy123→176; renderer49078 OURS,326 intermediate,164 lerp; long-pause recovery и измерение geometry refresh |

Во всех трёх renderer census: ambiguous0/unknown0/errors0, проверенные P1 поля
EQUAL. Invalidation после Remove — ожидаемый отказ чтения, не ошибка.
Run04: после длительной паузы хоста samples возобновились с большим serverTick
gap, `reanchors:1/discontinuities:1`; уже первый секундный playout sample имел
backlog8,425 ticks (~140 ms), далее сохранялось ~140 ms без многотысячного долга.
В фоне client native tick реально исполнялся с neutral commands; original Esc
gates не обойдены. Это bounded pause/return, не все положительные input fixtures.

[RUNTIME_CHECKS](evidence/m1/PSE_TOGETHER_P4_REWORK_72/RUNTIME_CHECKS.json): **68/68**
сверок сырых журналов/артефактов, не68 независимых игровых тестов и не приёмка В.
Sparse scene-witness run02 не доказывает точное расстояние в момент Spawn;
этот предел сохранён как UNKNOWN; stationary positive AABB witness — run03, moving brackets run04 — дополнительное наблюдение, не exact-position proof.
Первый ошибочно строгий checker68/69 и его исходник сохранены.

Каждый клиент: один Remove, retirement примерно50 ms, payload null/refs0/in_use0/
membership0/weak pointers null. Все controls и шесть proxy hooks восстановлены
exact16; обе роли каждой пары — retained+duplicate wait0/exit0 без emergency kill.
Все **63 REA sessions** закрыты. Raw: `P4_{HOST,CLIENT}_{02,03,04}/native.jsonl`,
`observer_*.jsonl`; selected events и три полных REA bundles — рядом с report checker.

## Целостность, воспроизведение и следующий шаг

[INTEGRITY_CONFIRM_FINAL](evidence/m1/PSE_TOGETHER_P4_REWORK_72/INTEGRITY_CONFIRM_FINAL.json):
оригинал77 файлов, защищённые profiles и обе копии равны baseline; EXE hashes
совпадают. [PROCESS_CHECK](evidence/m1/PSE_TOGETHER_P4_REWORK_72/PROCESS_CHECK.json):
игровых процессов0. Historical laboratories и их принятые evidence не редактировались.
Не затронуты Steam, original install, общий мир, П3, П8, П14 или П24.

```powershell
python -X utf8 -B experiments/pse_together/tools/run_offline.py --build-dir b72 --out <NEW_OUTPUT>
python -X utf8 -B experiments/pse_together/tools/pre_native_p4.py --build-dir b72 --offline <RESULT.json> --out <NEW_GATE.json>
python -X utf8 -B docs/evidence/m1/PSE_TOGETHER_P4_REWORK_72/review72.py
```

Последняя команда только перечитывает завершённые runs. Ни одно имя02/03/04
не переиспользовать; для нового запуска нужны новые RUNS/profiles/gate и порядок
[RUNTIME_P1](../experiments/pse_together/tools/RUNTIME_P1.md) с wrapper П4.
Выделение памяти/native lifecycle contracts прежние; coordinate/ABI table exact
EXE приведена в [DOC-65 §5](65_P4_PRESENTATION_AND_SPAWN.md): Factory RVA1d4780,
LinkSector1d3d70, Point1cf540, floor/ceiling1a3eb0/1a4060, reset1d3e60, responder
1b1640 и bindings95f20. Новых native ABI/полей не заявлено; analytical names
не являются исходными именами KEX. Geometry capture — read-only Sim-thread
numeric snapshot с прежними доказанными offsets, не новый lifecycle contract.

Последующая проверка режима В выполнена Codex по назначению пользователя:
[DOC-79](79_P4_ACCEPTANCE_REVIEW.md), PASS (bounded corridor). Полный water/cache-changing refresh, split apertures,
произвольные карты и несмежные teleport остаются UNKNOWN/refused. Полный список
ошибок и ограничений — [FAILURES_AND_LIMITS](evidence/m1/PSE_TOGETHER_P4_REWORK_72/FAILURES_AND_LIMITS.md).
