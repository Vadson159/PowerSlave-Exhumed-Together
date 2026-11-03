# P1-1 — Generation Routing Remediation

2026-11-02. **PASS только для generation-scoped control routing.** Исправлен P1-1 из [DOC-45](45_M12A_CRITICAL_ACCEPTANCE.md). До изменений дефект R1 воспроизведён на текущих Session/Worker; после изменений проверены старые/актуальные unicast, MAP_READY и регрессии DOC-40. Полноценный M1.2b не начинался. Нативная приёмка M1.2a остаётся в границах независимого CONDITIONAL DOC-45; новой native-runtime приёмки этим отчётом нет.

[Итог проверки](evidence/m1/M12B_GENERATION_46/RESULT_SUMMARY.json), [build/source manifest](evidence/m1/M12B_GENERATION_46/BUILD_MANIFEST.json), [diff](evidence/m1/M12B_GENERATION_46/SOURCE_CHANGES.patch), [before sources](evidence/m1/M12B_GENERATION_46/before/), [after sources](evidence/m1/M12B_GENERATION_46/after/), [baseline hashes](evidence/m1/M12B_GENERATION_46/BASELINE.json). Большие raw logs и копии исполняемых test binaries сохранены рядом. Ни один результат не основан только на PASS инструментов Gemini.

## R1: до и после

Изначальный audit reproducer скопирован byte-for-byte: SHA256 `1c89f2d6b9d83f3b062b10aef53f6272b644cf2a4c2c9e03d9f9dfd864542d8f`. До его первого запуска продуктовые исходники не менялись. Игры/KEX в этом опыте нет: один реальный ENet peer поколения1, paused outbound, Session recover/KICK1, graceful leave, rejoin того же participant/key как gen2, затем возобновление drain.

| Версия/проверка | Наблюдение до cleanup | Exit / значение |
|---|---|---|
| Неизменённый исходный R1, before | `client_connected=0 authenticated=0 session_wantsPlayer=0` |10, дефект реально воспроизведён. [Raw](evidence/m1/M12B_GENERATION_46/RUN_before_r1_original_20261102_004903_126495/console.log) |
| R1 с проверкой до cleanup, before | Та же потеря gen2 |10. [Raw](evidence/m1/M12B_GENERATION_46/RUN_before_r1_live_20261102_004905_438193/console.log) |
| R1 с проверкой до cleanup, final after | `client_connected=1 authenticated=1 session_wantsPlayer=1`; gen2 прошла собственный MAP_READY |0, PASS. [Raw](evidence/m1/M12B_GENERATION_46/RUN_after_r1_live_20261102_005631_895191/console.log) |
| Неизменённый исходный R1, final after | gen2 connected/authenticated/ready =1/1/1 |10 из-за ошибки самого валидатора; не считается product failure или PASS. [Raw](evidence/m1/M12B_GENERATION_46/RUN_after_r1_original_20261102_010047_756014/console.log) |

**Ограничение исходного R1:** `defect = !b.isConnected() || !s.wantsPlayer()` вычисляется после `s.stop()`, который уже установил stopped/active/ready. Поэтому wantsPlayer всегда false и verdict всегда DEFECT_REPRODUCED, даже после исправления. Это не опровергает исходный дефект: before-stop строка before действительно показывает отключённую gen2. В новой копии [r1_live.cpp](../experiments/m1/generation_routing/tests/r1_live.cpp) predicate перенесён перед stop и дополнен isAuthenticated. Старый audit source/report/Evidence не редактировались. Дополнительного блокера продукта в рамках задачи не выявлено.

## Изменения

Изменены ровно пять существующих файлов; изменения имеют только внутренний routing/session scope:

| Файл | Изменение |
|---|---|
| `tools/enet-prototype/include/thread_bridge_dto.h` | `connectionGeneration` для KICK_SLOT, unicast MapChange/Baseline и MAP_READY. Default0 не является wildcard. Поле не сериализуется. |
| `tools/enet-prototype/include/enet_worker.h` | Private resolver для peer по slot+generation; комментарий существующего outboundNoPeer учитывает отбрасывание stale/unstamped DTO. |
| `tools/enet-prototype/src/enet_worker.cpp` | Проверка текущего session ledger перед KICK/send; MAP_READY привязан к authenticated sending peer; клиентская очередь не может превратить старый ready в ready нового peer. Pending overflow KICK также использует generation запросов. |
| `tools/enet-prototype/src/mock_simulation.cpp` | Существующие mock-производители заполняют новые поля при создании DTO, host проверяет MAP_READY generation. Это необходимая адаптация действующих call sites к строгой адресации, без новой gameplay/replication логики. |
| `experiments/m1/network_input/src/network_input.cpp` | Session сохраняет gen в KICK/MapChange/Baseline до enqueue и проверяет MAP_READY до изменения ready. Старый отказанный запрос не создаёт KICK для нового поколения. |

Для последнего пути в `BridgeRespawnRequest` и `BridgeInteractionRequest` добавлен тот же **внутренний** peer context: DOC-45 P1-1 прямо указывает, что эти deferred requests вызывают recover/KICK. Без контекста старая request могла бы поздно создать уже формально «актуальный» KICK для N+1 и обойти исправление исходящего DTO. Worker заполняет context из peer session при приёме; Session отказывает только запросу своего текущего slot/gen. Respawn/Interaction не реализованы: сохранён прежний отказ через recover для актуального запроса. Это часть P1-1, не admission policy P2-1.

Новые средства проверки — `experiments/m1/generation_routing/` ([README](../experiments/m1/generation_routing/README.md), CMake, workflow/finalize и tests). В сборку входят accepted Worker/Bridge и текущий Session; новая игровая DLL не создаётся. Публичный native consumer с неизменёнными hooks скомпилирован как object library `/W4 /WX`, без загрузки/вызова KEX.

## Контракт маршрутизации

`resolveOutboundPeer(slot, generation)` выполняется только на существующем NETWORK owner. Он проверяет допустимый slot, ненулевую generation, active/gen worker ledger и фактический authenticated peer через `getPeerSession`. Между проверкой и применением нет poll или передачи другому потоку. Старый KICK/MapChange/Baseline после rejoin отбрасывается, увеличивая existing outboundNoPeer; новая generation не отключается и ничего не получает. Актуальные события продолжают работать.

Unicast generation фиксируется владельцем при создании сообщения и сохраняется в outbox/bridge/pending. При retry она не заменяется текущей. Broadcast MapChange/Baseline по-прежнему применяются ко всем текущим peers, игнорируют это внутреннее поле и сохраняют прежнюю публикацию session map. Client-local RECONNECT и его DOC-40 resync семантика не менялись.

MAP_READY защищён с двух сторон. Клиентский Worker отправляет DTO только при совпадении slot/gen с его текущим authenticated Node; он не переписывает старую идентичность перед отправкой. Host Worker извлекает generation из session отправившего peer и сохраняет её перед `deliverInbound`, включая pending FIFO. Session требует точное совпадение active slot/gen/epoch **до** ready=true и baseline.

DOC-40 механизмы ownership/FIFO/retry/resync сохранены. SPSC queue и ThreadBridge исходники не менялись. Устаревший unicast намеренно отбрасывается как действие завершившегося поколения, а не считается потерей состояния новой сессии; отсутствие актуального peer учитывается тем же счётчиком. Настоящее переполнение по-прежнему объявляется владельцем, вызывает resync/завершение сессии и не превращается в «продолжить после потери». Existing pending overflow сохраняет lifecycle FIFO; KICK адресуется только поколению, создавшему отказанный запрос.

## Проверки

[Финальный routing executable/log](evidence/m1/M12B_GENERATION_46/RUN_after_routing_contract_20261102_010049_972800/console.log) — пять сценариев, не330 отдельных интеграционных тестов; значительная часть330 assertions проверяет заполнение очереди. Release checks не вырезаются NDEBUG, affinity включена.

| Сценарий | Проверенное поведение |
|---|---|
| Real ENet host unicast | Пока drain paused, queued gen1 KICK/MapChange/Baseline; тот же participant стал gen2. После drain клиент реально декодирует только актуальные MapChange/Baseline и broadcast. Старые и gen0 unicast не приходят; outboundNoPeer6. Gen2 остаётся connected/authenticated. Актуальный KICK2 затем даёт реальный disconnect и host CLIENT_LEFT2. |
| MAP_READY + pending/FIFO |140 reliable ready от gen1 заполняют inbound128 и pending≥12. Пока simulation не дренирует очередь, клиент переподключается как gen2 и отправляет ещё ready. Все141 MAP_READY доставлены в исходном порядке, со stamps140×gen1 и1×gen2; CLIENT_LEFT1/RECONNECTED2 сохраняют FIFO. Нет объявленной потери/resync, pending возвращается к0. |
| Client queued ready | MAP_READY1 оставлен в outbound того же bridge через stop/start CLIENT worker. После WELCOME2 старый ready отвергнут до wire send; текущий ready2 реально доходит до Host и получает stamp2. Host видит ровно один ready. |
| Session consumer/outbox | Worker joined перед контролируемой передачей NETWORK роли тесту; конкурентного producer нет. Full outbound256 удерживает два MapChange в Session outbox. После освобождения они выходят FIFO с поколениями1,2, хотя current уже2. Старый MAP_READY1 не меняет ready/не создаёт baseline; актуальный2 создаёт baseline2. KICK2 сохраняет2 после появления3. Старые deferred refused requests также не создают новый KICK. |
| Wire remains v1.10 | Изменение внутренних поколений не меняет serialized MapChange/Baseline/MAP_READY bytes; MAP_READY остаётся16bytes = header11+slot1+epoch4. Все четыре wire source files byte-identical. |

Все эти проверки используют собственные условия над decoded packets, реальными connection states, содержимым очередей и runtime metrics. Raw содержит `WIRE_RECEIVED`, `UNICAST_EVIDENCE`, `PENDING_EVIDENCE`, `CLIENT_READY_EVIDENCE`, `OUTBOX_EVIDENCE`, а не только PASS labels. Affinity violations новых сценариев и R1 =0. Штатный fault-injection child abort в F16 существующего suite остаётся ожидаемым доказательством enforcement.

| Existing suite, пересобранный из after sources | Результат / raw |
|---|---|
| `pse_test_suite` |46/46, exit0 — [log](evidence/m1/M12B_GENERATION_46/RUN_after_pse_test_suite_20261102_005709_035253/console.log) |
| `pse_stress_test` |11/11, exit0 — [log](evidence/m1/M12B_GENERATION_46/RUN_after_pse_stress_test_20261102_005718_440396/console.log) |
| `pse_fault_injection` |16/16, exit0 — [log](evidence/m1/M12B_GENERATION_46/RUN_after_pse_fault_injection_20261102_005722_806096/console.log) |
| `pse_bridge_acceptance` |11/11, exit0 — [log](evidence/m1/M12B_GENERATION_46/RUN_after_pse_bridge_acceptance_20261102_005731_462196/console.log) |

Оригинальные suite/assertions не редактировались. Совместимость mock-производителей обеспечена явным заполнением generation, без wildcard/fallback на текущее поколение в worker. Каждая сборка и каждый run имеют отдельные полный stdout, source/binary SHA, argv и exit code в Evidence. Все продуктовые изменения до финальных регрессионных прогонов уже присутствовали; позднее добавлялась только подробность вывода нового теста.

## Целостность, граница и остановка

[Integrity](evidence/m1/M12B_GENERATION_46/INTEGRITY.json): все401 исторических immutable файлов, включая DOC-40/44/45, R1/R2 audit Evidence и принятые DLL, неизменны.77 оригинальных и77 файлов независимой копии совпадают с baseline; оба защищённых профиля before/after равны. Native hooks/bootstrap/build profile, Player/Puppet lifecycle и wire protocol source files не менялись. Изменены только пять перечисленных текущих source files и созданы новые тесты/отчёт. PROGRESS/EVIDENCE_INDEX дополнены новым срезом; старые отчёты не переписаны.

Новых игровых запусков, Frida sessions, DLL injection/unload и native runtime tests —0. `native_compile_check` — compile-only, не приёмка новой DLL. Исторические DLL не содержат этот source patch; перед будущим runtime нужна согласованная сборка, без использования старого binary как якобы исправленного. Данные нового routing test не выдаются за native movement/lifetime Evidence.

P2-1…P2-5 не исправлялись: admission/rejoin policy, timeout/cadence, native diagnostics/P1 physics coverage, logging/timing и sequence hardening остаются в DOC-45. Вторая игровая DLL, world replication, HUD, weapons/campaign и полноценный M1.2b не начинались. **Остановка после P1-1; PASS относится только к generation-scoped control routing.**
