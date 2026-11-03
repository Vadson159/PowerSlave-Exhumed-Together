# P2-1 — single-client admission/rejoin

2026-11-02. **PASS только устранению P2-1 из DOC-45.** R2 воспроизведён до изменений на коде после DOC-46. После исправления beta отклоняется до назначения слота, peer освобождается, alpha возвращается с правильным ключом и новым generation. Поддельные ключи и повторные beta не захватывают резерв alpha. Полноценный M1.2b не начинался; KEX не запускался.

[RESULT_SUMMARY](evidence/m1/M12B_ADMISSION_47/RESULT_SUMMARY.json), [source/build manifest](evidence/m1/M12B_ADMISSION_47/BUILD_MANIFEST.json), [diff](evidence/m1/M12B_ADMISSION_47/SOURCE_CHANGES.patch), [before](evidence/m1/M12B_ADMISSION_47/before/), [after](evidence/m1/M12B_ADMISSION_47/after/), [baseline](evidence/m1/M12B_ADMISSION_47/BASELINE.json). Контекст: [DOC-44](44_M1_FIRST_NETWORK_INPUT.md), [независимый DOC-45](45_M12A_CRITICAL_ACCEPTANCE.md), [DOC-46](46_M12B_GENERATION_ROUTING_REMEDIATION.md).

## R2 before/after

Исходный [repro.cpp Opus](evidence/m1/M12A_AUDIT_45/repro.cpp) скопирован byte-for-byte в новый test harness, SHA256 `1c89f2d6b9d83f3b062b10aef53f6272b644cf2a4c2c9e03d9f9dfd864542d8f`. Вызвана только ветка `r2`. До её запуска продуктовые исходники не менялись.

| Run | Реальные состояния / результат |
|---|---|
| [Original R2 before](evidence/m1/M12B_ADMISSION_47/RUN_before_r2_original_20261102_011807_591140/console.log) | beta authenticated1/slot2/connected1, Session не ready; alpha с верным ключом authenticated0. Exit10, дефект воспроизведён. |
| [Original R2 after](evidence/m1/M12B_ADMISSION_47/RUN_after_r2_original_20261102_012250_356639/console.log) | beta получает REJECT2 и отключается; alpha authenticated1/Session ready1. Exit0. |
| [R2 со строгим before-cleanup verdict](evidence/m1/M12B_ADMISSION_47/RUN_after_r2_fixed_20261102_012255_044347/console.log) | `beta_connected=0 alpha_slot=1 alpha_gen=2 alpha_ready=1 fixed=1`, exit0. |

Исходный R2 лишь проверяет, исчез ли его прежний defect predicate, причём после stop. Поэтому дополнительно сохранён [r2_fixed.cpp](../experiments/m1/admission_rejoin/tests/r2_fixed.cpp): тот же сценарий требует положительного результата до cleanup, включая disconnected/unauthenticated beta и authenticated/ready alpha в slot1/gen2. Исходный R2, DOC-45/46 и старые Evidence не переписывались.

## Минимальная policy и уровень исправления

Исправление находится в admission/slot allocation, до WELCOME, `PLAYER_JOIN`, регистрации participant→slot и authenticated callback Worker. Поздний KICK уже созданного P3 не требуется. Число transport peers не равно числу допустимых зарезервированных slots: ранее `maxClients=1` ограничивало только ENet, а allocator продолжал выдавать свободные1…7.

В `HostConfig` добавлены generic опции:

- `maxClientSlots`, default7: допустимый диапазон1…N для нового admission и rejoin. Текущая native Session задаёт1, совместно с прежним maxClients1. В общей архитектуре нет постоянного правила «только P2»; default по-прежнему позволяет beta получить другой слот.
- `rejectedPeerTimeoutMs`, default0: опциональная граница ожидания ACK/закрытия уже отклонённого неавторизованного peer. Session задаёт250ms; default оставляет прежнее ENet timeout поведение. Это отдельный rejection timer, не изменение input timeout/cadence P2-2.

`WorldSimulation::allocateClientSlot` получает верхнюю границу из HostConfig. Оба поиска — существующего participant и пустого слота — ограничены диапазоном. Если slot1 зарезервирован alpha, beta не получает slot2 и возвращается REJECT_SERVER_FULL до любой мутации реестра. Верный reconnectKey alpha проверяется прежним кодом, сбрасывает состояние соединения и увеличивает connectionGeneration. Несовпадение ключа не меняет резерв, generation или peer. Ключ без совпадающего participantId не даёт доступ к резерву.

После REJECT host запускает обычный `disconnect_later`, давая доставить reliable ответ. Если ACK не приходит, poll по истечении настроенного срока вызывает `disconnect_now` **только для rejected && unauthenticated peer** и удаляет его transport entry. До этого входящие сообщения такого terminal connection игнорируются, повторная авторизация не допускается. Таймер не продлевается повторными пакетами.

У отвергнутого peer нет назначенных slot/session generation: очистка не вызывает `disconnectSlot`, не публикует CLIENT_LEFT и не касается alpha. Все действия над **авторизованными** сессиями сохраняют проверки slot+generation DOC-46; Worker/DTO/routing исходники не менялись. Новое соединение перезаписывает PeerInfo новым состоянием; старый deadline не может удалить уже authenticated alpha.

Изменены ровно пять существующих файлов:

| Файл | Что |
|---|---|
| `tools/enet-prototype/include/enet_host.h` | Две опции конфигурации, private rejection helper и состояние rejected peer. |
| `tools/enet-prototype/src/enet_host.cpp` | Передача admission bound allocator, terminal rejection и bounded cleanup неавторизованного peer. |
| `tools/enet-prototype/include/simulation.h` | Опциональный аргумент допустимого диапазона во внутреннем C++ allocator API. Это не KEX ABI. |
| `tools/enet-prototype/src/simulation.cpp` | Граница двух admission-поисков; существующие проверки ключа и generation сохранены. |
| `experiments/m1/network_input/src/network_input.cpp` | Только opt-in конфигурация single-client host: slots1, rejection250ms. |

Новые исходники/тесты — [experiments/m1/admission_rejoin](../experiments/m1/admission_rejoin/README.md). Параметры legacy callers по умолчанию сохраняют прежние7 slots. Нет ротации P2…P8, освобождения participantId или изменения выбора Player/Puppet.

## Автономные проверки

[admission_contract raw](evidence/m1/M12B_ADMISSION_47/RUN_after_admission_contract_20261102_012259_650802/console.log) содержит четыре сценария,112 assertions и конкретные `REJOIN_EVIDENCE`, `LEDGER_EVIDENCE`, `EXPIRY_EVIDENCE`. Это реальные loopback ENet connections и проверки содержимого реестра, не PASS-маркеры Gemini.

| Проверка | Наблюдение |
|---|---|
| Repeated beta / forged key / alpha rejoin |8 циклов: beta с чужим ID, даже с ключом alpha, получает REJECT2; поддельная alpha key получает REJECT8. Оба rejected peers действительно отключены. Alpha восемь раз возвращается в slot1 с тем же ключом и поколениями2…9. Отказанные попытки не увеличивают generation alpha. |
| Stale input | До первого leave получен настоящий сетевой forward command и Session input4. Leave очищает input0. Во всех rejected attempts и rejoin input остаётся neutral0; новые сессии не наследуют hold. Это проверка Session/DTO, не native KEX movement. |
| Registry/orphan check | После beta reject: slots2…7 без participant/peer/input queue/buttons; slot1 содержит прежние alpha/key/gen, disconnected и peer=null. Auth events1, ended events1 принадлежат только alpha; rejected beta не создала callback, active session или custom input callback. |
| Rejection без ACK | Beta после отправки HELLO больше не poll-ится и не ACK-ит REJECT. Host логирует expiry. После600ms обслуживания только host alpha подключается и получает slot1/gen2 при ещё существующем не обслуживаемом socket beta. Поздний beta cleanup не отключает alpha2. |
| Локальность policy | Отдельный host с default конфигурацией по-прежнему выдаёт beta slot2 после ухода alpha; fixed policy включена только конфигурацией Session/тестового host. Это не повтор native capacity теста. |

[R1 DOC-46](evidence/m1/M12B_ADMISSION_47/RUN_after_r1_live_20261102_012302_977871/console.log): exit0, gen2 connected/authenticated/ready1/1/1 после queued KICK1. [Все5 routing scenarios DOC-46](evidence/m1/M12B_ADMISSION_47/RUN_after_routing_contract_20261102_012305_177721/console.log) также exit0: stale/current control, broadcast, MAP_READY/FIFO и generation capture не ослаблены. Оба теста переиспользованы из неизменённых исходников DOC-46.

| Пересобранные existing suites | Результат |
|---|---|
| [pse_test_suite](evidence/m1/M12B_ADMISSION_47/RUN_after_pse_test_suite_20261102_012442_288156/console.log) |46/46, exit0 |
| [pse_stress_test](evidence/m1/M12B_ADMISSION_47/RUN_after_pse_stress_test_20261102_012451_647348/console.log) |11/11, exit0 |
| [pse_fault_injection](evidence/m1/M12B_ADMISSION_47/RUN_after_pse_fault_injection_20261102_012456_011420/console.log) |16/16, exit0 |
| [pse_bridge_acceptance](evidence/m1/M12B_ADMISSION_47/RUN_after_pse_bridge_acceptance_20261102_012504_737538/console.log) |11/11, exit0 |

Существующие tests/assertions не менялись. Новых affinity/SPSC/ownership нарушений нет; Session affinity counter0, проверки включены, роли Worker/Simulation не передавались дополнительным producers. Direct registry inspection выполнялась только в отдельном тесте ENetHostServer, у которого весь I/O и чтение реестра на одном потоке. Стандартный ожидаемый F16 child abort остаётся частью проверки enforcement.

## Целостность и оставшаяся граница

[INTEGRITY](evidence/m1/M12B_ADMISSION_47/INTEGRITY.json):253 проверенных historical files unchanged, original77/copy77 unchanged, оба protected profiles before/after равны. Wire v1.10 source/serializers, DOC-46 Worker/DTO generation checks, SPSC/Bridge и native hooks/ABI/lifecycle byte-identical. KEX game launches, DLL builds/loads и Frida sessions0. Старые DLL — исторические, не содержат этот source patch; новая согласованная DLL потребуется перед будущим разрешённым runtime.

[Финальная проверка процессов и UDP endpoints](evidence/m1/M12B_ADMISSION_47/PROCESS_CHECK.json): тестовых процессов и слушающих тестовых портов не осталось.

**Ограничение резерва:** первая успешно принятая identity сохраняет slot/key до создания нового host session ledger. Если alpha потеряла ключ или ушла навсегда, beta не заменяет её автоматически. Освобождение participantId, ручная передача резерва, persistence ключей и динамическая ротация требуют отдельной политики. Это намеренно не разрабатывалось.

Текущий scope — loopback с token, maxClients1, admissible slots1, без искусственного latency/drop. Deadline действует при продолжающейся работе poll, не является hard realtime гарантией. Pre-HELLO stalls, непрерывный malicious flood, fairness при конкурирующих handshakes и LAN/VPN quality не принимаются этим тестом; input timers, diagnostics и sequence hardening P2-2…P2-5 не изменялись.

PROGRESS/EVIDENCE_INDEX дополнены. **PASS только P2-1. После исправления остановиться: полноценный M1.2b, world replication, другая игровая DLL, оружие и кампания не начинались.**
