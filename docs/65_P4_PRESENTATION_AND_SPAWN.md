# DOC-65 — П4: старт P2, порталы, буфер и фоновый клиент

2026-11-04. Исполнитель — Codex (Sol). **REVIEW / PARTIAL, режим В (повышен с Б).** Реализованы отдельные экспериментальные DLL-цели и проверяемые числовые контракты. Критерии П4 полностью не выполнены: текущий исправленный host-кандидат не аттестован в игре, порталы и jitter-показ в runtime не проверены, изменение wire не принято. Никаких новых закрытых gates DOC-58C/58D. Исполнитель не ставит PASS.

EXE SHA-256: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`. Принятые исследования и runs не повторялись. Один новый ограниченный сценарий — `P4_HOST_01` / `P4_CLIENT_01`; он не воспроизводит sign-off П1. Новых декомпиляций нет. Архитектура Full Player на хосте и passive Actor на клиенте сохранена.

## 1. Объём и причина режима В

Выполнена только работа S1.3–S1.5. Вход: П1 PASS по [DOC-64](64_P1_ACCEPTANCE_REVIEW.md). Опора: [58B.1](58B1_NATIVE_LEVEL_UNLOAD_RECONNECT.md), [58A](58A_NATIVE_VISUAL_PROXY_COMPLETION.md), [53](53_HOST_BACKGROUND_SIMULATION.md), [56](56_NATIVE_MAP_IDENTITY_READY_GATE.md), [54](54_WORLD_REPLICATION_MVP_READINESS.md); требования к v1.11 — [DOC-62 §5](62_P6_ADR_AUTHORITY_AND_CLIENT_WORLD.md).

Новые нативные условия размещения, геометрическая область за пределами одного сектора, клиентские control hooks и оставшиеся UNKNOWN требуют режима В по [ROADMAP §3](ROADMAP.md). Режим определяет приёмку другой моделью, а не освобождает от недостающего runtime. Проверяющий — Opus extra. Новые архитектурные решения П6 не утверждены косвенно.

| Задача | Результат | Не выполнено |
|---|---|---|
| S1.3 | Кандидат: P2 получает первую наблюдённую нативную стартовую позицию, yaw и sector; при занятой точке остаётся Armed, без Reset/alloc/Spawn и без fallback к P1 | Native Spawn текущей исправленной версии и доказательство отсутствия overlap |
| S1.4 | Кандидат: поиск сектора по числовой копии текущих плоскостей, проверка взаимного портала, LinkSector и membership; жёсткий список 259→123 не используется | Настоящее движение/renderer через портал; смена caches/воды; другие карты; много порталов за шаг |
| S1.5 | Буфер 64/100 мс, offline; фоновый клиент наблюдался в игре без прокси; предложение решения v1.11 оформлено | Runtime playout/jitter с живым прокси, положительные held Attack/mouse, manual pause/return; утверждение и реализация wire |

Сегмент S1 в целом не закрыт. Его критерий двух ПК, смен карт и reconnect относится также к П3/П5; новый run не подменяет их приёмку.

## 2. Реализация

Код — [experiments/pse_together](../experiments/pse_together/README.md). Лаборатории и их DLL не редактировались. `pse_dll_host_p4` и `pse_dll_client_p4` включают `PSE_FEATURE_P4`; прежние четыре цели не задают этот флаг. `pse_all` собирает обе новые цели и 25 offline-наборов. Это не переключает принятые DLL на П4 автоматически.

**Размещение.** Host сохраняет позицию реального P1 после первого native gameplay update. Перед созданием P2 проверяет стабильный мир, native point-in-sector, прежние floor/ceiling и отсутствие пересечения диагностических AABB с акторами всех секторов. Списки читаются с проверкой reciprocal links и ограничением длины; записи в links/refs нет. При отказе нет инициализации Player и обращения к фабрике. Это консервативная стартовая точка с ожиданием освобождения, не завершённый подбор свободной позиции возле P1. Если точка остаётся занята, P2 не появляется.

**Порталы.** В адаптере П4 сохраняются TOMB и начальная аттестация карты; расширяется область движения одного Actor1007. На каждом движении обновляются числовые копии геометрии. Flags грани читаются по `face+0x1c` (u32), adjoin по `+6` (s16), planes по `+0xc/+0x18` — принятый layout `movement_native.js::sectorData58`. Текущий сектор остаётся владельцем точной границы. Между секторами нужен один взаимный копланарный портал; все углы swept AABB должны проходить непортальные плоскости обеих сторон. Нельзя пересечь стену или неоднозначный портал. До записей origin проверяются exact16 LinkSector и исходное/целевое membership. После native LinkSector — 0/1 membership. Не записываются sectorIndex, links, floor/ceiling caches, refs, flags или physics state.

При несовпадении native floor/ceiling с caches Actor либо смене water bit переход отклоняется. Поэтому «любые порталы» **не реализованы полностью**. Понадобится отдельный доказанный native контракт обновления этих свойств; ослаблять отказ без него нельзя. Native lerp при допустимом коротком переносе не сбрасывается, но соответствие render origin и секторного culling в игре — UNKNOWN. Исходные жёсткие инварианты P1 из N3 DOC-64 сохраняются; обычное изменение его здоровья/инвентаря может остановить диагностический адаптер.

**Буфер.** `presentation_buffer.hpp`: фиксированные 64 записи; задержка 100 мс; относительный serverTick при 60 Гц привязан к локальному первому receive, без вычитания абсолютных часов процессов. Отвергаются stale sequence/tick, смена epoch/connection/entity identity без clear, nonfinite pose и обратное receive time. Позиция/pitch интерполируются линейно, yaw — по короткой дуге в радианах. Телепорт >160 не интерполируется. При исчерпании записи вытесняются, при потере пакетов экстраполяции нет; спустя 250 мс без данных удерживается последний настоящий sample. Задержка фиксирована, адаптивная синхронизация часов и подбор delay не реализованы.

Вызов seam update помещает снапшот в буфер и возвращает Unchanged (без немедленной мутации); actual apply выполняется в native tickLeave после consume и отдельно журналируется. Это особенность диагностического П4: `p4_snapshot_buffered` и `proxy_update` имеют разные значения. `UpdateUnchanged` автомата не следует трактовать как битовое применение исходного сетевого pose. На Remove/world exit буфер очищается. Реальная точность отображения подтверждается будущим renderer-наблюдением, не этим кодом.

**Фоновый клиент.** Только атомарное разрешение `PSE_P4_EnableBackground(1)` из стенда; по умолчанию выключено. Перед scoped lease +319 проверяются running, native MapReady/baseline, карта, stable gameplay, provenance focus event и отсутствие shutdown/failure. Original pause/inventory/modal gates исполняются. На background update и первом return update команды нейтрализуются по шаблону DOC-53, native bindings сбрасываются; Commands/Turning hooks отбрасывают локальный результат. Исходящие samples без foreground невалидны/neutral. Контроль native caller и duplicate Update выполняется до original call в П4. Перед stop lease заканчивается, Update/Commands/Turning восстанавливаются exact16; responder и storage pinned до process exit. Это запись только в доказанный command block, не буквальное отсутствие любых Player writes. Runtime run01 ещё содержит старую общую запись `client_quiesced.player_writes:0`; она не описывает background input neutralization. Текущий лог исправлен.

## 3. Проверки и новый run01

**VERIFIED offline:** [OFFLINE_01](evidence/m1/PSE_TOGETHER_P4/OFFLINE_01/RESULT.json) — 25/25, П4 244 checks; после исправления support guard [OFFLINE_02](evidence/m1/PSE_TOGETHER_P4/OFFLINE_02/RESULT.json) — 25/25, **247 checks / 0 failures**. Исходные assertions старых наборов не менялись. Проверены jittered receive, ход playout между пакетами, wrap, stale/generation/epoch, bounded overflow, loss/freeze, teleport discontinuity, короткая дуга yaw; занятость anchor; разные portal IDs, подход/малый переход, стены, reciprocal/coplanarity/water/ambiguity, lease/new-focus precedence. Эти тесты не вызывают KEX.

После двух последних native-only исправлений (flags грани и точность quiesced-лога) пересобраны обе цели: [BUILD_FINAL](evidence/m1/PSE_TOGETHER_P4/BUILD_FINAL.log), `/W4 /WX`. Эти правки не меняют portable наборы. [PRE_NATIVE_CURRENT](evidence/m1/PSE_TOGETHER_P4/PRE_NATIVE_FINAL.json) — 9/9: exact EXE, 23 accepted sites обеих копий, prepare=193/no log в чужом процессе, export, Python syntax. Он содержит текущие DLL/PDB/source SHA. Это **не runtime приёмка текущих DLL**.

**RUNTIME OBSERVED, только run01:** Host PID13908 / Sim27100, Client PID10808 / Sim24488, свежие независимые profiles. Принятый launcher/isolation и DOC-51 loader, без JS gameplay adapter. Обе копии прошли current-run isolation gate. [Машинная сверка](evidence/m1/PSE_TOGETHER_P4/RUNTIME_RUN01_PARTIAL.json), [полный REA bundle](evidence/m1/PSE_TOGETHER_P4/RUN01_FINAL_BUNDLE.json), raw [host](evidence/m1/PSE_TOGETHER_P4/P4_HOST_01/native.jsonl) / [client](evidence/m1/PSE_TOGETHER_P4/P4_CLIENT_01/native.jsonl).

1. Стенд слишком рано передал фокус хосту: клиент не успел завершить 30 stable native ticks. Admission timeout вызвал reconnect до generation5. После возврата фокуса MapReady/baseline завершились. Это ошибка сценария, не доказательство принятого reconnect прокси П3; прокси вообще не создавался.
2. Host сохранил anchor `[-7420,-6775,1152]`, sector259. P1 штатным вводом дошёл до `[-7420.48193359375,-6896.23486328125,1152]`. Read-only witness: floor cache1152, ceiling1664, diagnostic bounds minZ−80/maxZ80. Начальный guard ошибочно требовал `originZ+minZ >= floor` и поэтому всегда отклонял эту штатную точку. События `p4_spawn_deferred`, **P2 Spawn=0 и Reset=0**, без обхода отказа. Сравнение неизменной native support-пары заменило выдуманное правило центра AABB только **после** run01. Исправленный host не запускался.
3. При READY gen5 клиент действительно обновлялся в фоне; зарегистрированный счётчик достиг **3840**, каждый из периодических samples: `local_this_tick=1`, focus_after1, buttons/held_forward/held_back0. Это выборочная телеметрия одного сценария без прокси, не 3840 независимых проверок, не все положительные input-preconditions и не manual pause test.
4. Proxy Spawn/Update/Remove, portal/renderer submissions и jitter playout — **0 / NOT_EXECUTED**. Буфер и портал не получают runtime-статус по одному background tick.
5. Native fault/blocked events0; control restores exact16; оба retained и duplicate handles wait0, exit0. У host `guards:false` при exit — внешнее Player storage не инициализировалось, это не утверждение о порче или проверке guards. Оба процесса штатно закрыты WM_CLOSE. [20 REA-сессий закрыты](evidence/m1/PSE_TOGETHER_P4/REA_SESSIONS_CLOSED.json). Последующий integrity — [INTEGRITY_FINAL](evidence/m1/PSE_TOGETHER_P4/INTEGRITY_FINAL.json).

Код реально загруженных DLL сохранён в `evidence/m1/PSE_TOGETHER_P4/run01_sources/`, DLL/PDB — в `P4_*_01/native_artifact/`, хеши — [PRE_NATIVE_GATE](evidence/m1/PSE_TOGETHER_P4/PRE_NATIVE_GATE.json). Не сопоставлять их с текущим builder без проверки SHA. `PRE_NATIVE_CURRENT` относится к последующей версии.

## 4. Wire v1.11

[Предложение решения S1.5](evidence/m1/PSE_TOGETHER_P4/PROTOCOL_V111_DECISION.md): принять отдельную несовместимую 0x0111 с native именем и структурой карты в MapReady, симметричными данными Welcome/MapChange, host comparison **до** promotion; без downgrade. Добавки ADR П6 (сложность/манифест, sectorIndex, discontinuity и подтверждение ввода) согласовать с D2/D3. Это предложение режима В, не разрешение на изменение замороженного протокола. Текущее ограничение echo v1.10 остаётся. `net_protocol.h` SHA-256 `8272aa3ef7bcd9e555e8a7b6bc12a1fe320b378dc34c8edbe6b38891e3bbac0f`; transport/DTO/Worker не менялись, world_synchronized=false.

## 5. Координаты и воспроизведение

Имена функций аналитические, ABI переиспользованы из принятых evidence, новые ABI не выводились. Везде exact SHA выше, runtime address = actual base + RVA; preferred VA отдельно:

| Роль | RVA | Preferred VA | File offset | Основание |
|---|---|---|---|---|
| Factory | 1d4780 | 1401d4780 | 1d3b80 | DOC-58A, Win64 Factory/type/Vec3/yaw/sector |
| LinkSector | 1d3d70 | 1401d3d70 | 1d3170 | DOC-58A, RCX Actor / EDX sector |
| Point query | 1cf540 | 1401cf540 | 1ce940 | DOC-58A, RCX sector / RDX point / XMM2 eps |
| Floor / ceiling | 1a3eb0 / 1a4060 | 1401a3eb0 / 1401a4060 | 1a32b0 / 1a3460 | DOC-58A, RCX point / RDX sector / R8B mode, XMM0 return |
| Reset interpolant | 1d3e60 | 1401d3e60 | 1d3260 | DOC-58A, RCX interpolant |
| Focus responder | 1b1640 | 1401b1640 | 1b0a40 | DOC-53, RCX instance / RDX event, полная исходная signature UNKNOWN |
| Reset bindings | 95f20 | 140095f20 | 95320 | DOC-53, no args |

Команды (из корня мода):

```powershell
python -X utf8 -B experiments/pse_together/tools/run_offline.py --out <NEW_OUTPUT_DIR>
python -X utf8 -B experiments/pse_together/tools/pre_native_p4.py --offline <RESULT.json> --out <NEW_GATE.json>
python -X utf8 -B experiments/pse_together/tools/review_runtime_p4.py
```

Первый шаг запускает только offline-наборы, второй игру не запускает, третий перечитывает завершённый PARTIAL run01. Принятый [runtime порядок П1](../experiments/pse_together/tools/RUNTIME_P1.md) применяется к `runtime_p4.py` только с новым gate, свежими RUNS/profiles и корректным выбором артефактов. Run01 имена завершены, не переиспользовать. Перед host focus transfer дождаться client Ready/baseline. Раннее фоновое разрешение не обходит готовность.

## 6. Что требуется от приёмки / следующего исполнения П4

Проверить новые controls/affinity и bootstrap failure/stop paths; host anchor validity/занятость/освобождение без P1 writes; временную шкалу буфера и соответствие renderer-выхода; sector membership/culling на подходе, пересечении и после портала. Затем fresh native run текущих DLL: реальный Spawn вне P1, движение с jitter и portal lerp, refused target/teleport, disconnect/retirement в актуальном контракте, background/return и Esc, integrity/exact restores/exit. Для water/cache-changing portals нужен доказанный native refresh; текущий guard не ослаблять. Согласовать v1.11 отдельно, затем реализовать codec/DTO/host negative tests. П3 lifecycle/rearm, семь прокси, общий мир, другие пакеты и два ПК этой работой не приняты.
