# M1.2a — First Native Network-Controlled P2

**PASS в ограниченном scope M1.2a, 2026-11-02.** Реальная цепочка отдельный ENet-клиент → принятый Worker → Thread Bridge → одна KEX native DLL → настоящий P2 проверена в двух свежих изолированных процессах. M0/M1.1 остаются PASS. Это не приёмка всего M1, клиента с игрой, репликации мира или capacity8. **Работа остановлена после M1.2a.**

Основные доказательства: [run01 native journal](evidence/m1/M12A_NATIVE_01/native.jsonl), [client sends](evidence/m1/M12A_NATIVE_01/client.jsonl), [сверка run01](evidence/m1/M12A_NATIVE_01/RESULT_SUMMARY.json), [Direct Quit native](evidence/m1/M12A_DIRECT_QUIT_01/native.jsonl), [сверка Direct Quit](evidence/m1/M12A_DIRECT_QUIT_01/RESULT_SUMMARY.json). Итог основан на проверенной реализации, сопоставлении конкретных команд с native updates/physics и независимых OS-наблюдениях. Счётчики PASS и текстовые маркеры Gemini не являются основанием native-приёмки.

## Сборка и изменения

- EXE SHA-256 для всех native contracts: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`. Запускалась только независимая копия `experiments/m0/game`; каждый run имел новый профиль в `experiments/m1/network_input/profile`.
- Комбинированная DLL: `experiments/m1/network_input/build/Release/pse_m11_bootstrap.dll`, SHA-256 **`24e2809f809435ccf53cc4c7299f28dffb5e32def910af54778232d20fe826ad`**. Одинаковый binary в обоих native runs. Старое имя/exports сохранены для принятого loader; в игровой процесс загружена одна игровая DLL.
- [Полный build/source manifest](evidence/m1/M12A_IMPLEMENTATION_20261102/BUILD_MANIFEST.json), [успешная сборка](evidence/m1/M12A_IMPLEMENTATION_20261102/BUILD_20261101_235510_644042.json), [frozen sources](evidence/m1/M12A_IMPLEMENTATION_20261102/final_sources/), [diff](evidence/m1/M12A_IMPLEMENTATION_20261102/SOURCE_CHANGES.patch). MSVC19.44, SDK10.0.26100.0, C++20, Release x64, /MT; native/adapter /W4 /WX, PSE_M12A. Старый принятый M1.1 DLL `0cb84521…48629` сохранён без пересборки.
- Изменён `experiments/m1/native_bootstrap/src/bootstrap.cpp`: условный вариант PSE_M12A подключает Session, применяет сетевое состояние и журналирует каждый native update/motion. Прежняя программа45/15/45/15 исключена из этого варианта компиляции. Native allocation/defaults, Reset, scoped Getter, Spawn, weapon-ready и lifecycle contracts сохранены.
- Единственное изменение общего транспорта —8 строк в `tools/enet-prototype/src/enet_worker.cpp`: проверка wire generation **до** изменения mailbox/sequence. Wire serializers, opcode/поля v1.10, очереди и остальной Worker/Bridge не менялись.
- Новые файлы — `experiments/m1/network_input/`: CMake/build, Session, headless client, runner/control, OS probes, три transport tests, offline review/integrity и README. Manifest перечисляет каждый файл/хеш, включая использованные ENet/MinHook dependencies. M0 исходники не изменялись.

Адреса не переоткрывались и не выводились заново. [14 native sites с SHA, preferred VA/RVA/file offset/prefix16 и конкретным M0 Evidence](evidence/m1/M11_IMPLEMENTATION_20261101/NATIVE_COORDINATES.json), ABI и ограничения — [DOC-41](41_M1_NATIVE_BOOTSTRAP.md), [handoff](M0_TO_M1_HANDOFF.md). Все функции/поля KEX здесь непрозрачные; имена ролей аналитические. Новых KEX ABI, offsets или native вызовов из worker нет.

## Проверка протокола и кода Gemini

[Подробный source audit с хешами и excerpts](evidence/m1/M12A_IMPLEMENTATION_20261102/SOURCE_REVIEW.json). VERIFIED ниже означает статическое наблюдение; runtime отдельно обозначен RUNTIME OBSERVED.

1. Действительный v1.10 определяется `net_protocol.h`/`replication_protocol.h`: common header11bytes, LE magic0x50534558/version0x0110; INPUT payload34bytes, всего45. HELLO и WELCOME содержат u64 `reconnectKey`, пропущенный в отдельных prose-таблицах WIRE_PROTOCOL.md. Оба тестовых клиента используют эти serializer/encodePacket, без собственного wire layout. INPUT идёт по channel1, ENet unreliable sequenced; reliable control — channel0. Wire `BUTTON_USE=4` и native forward4 — разные пространства; копирования bitmask нет.
2. В `M12aClient` Gemini `sendInputCommand` доказывает dispatch в локальную ENet очередь. Сам по себе он не доказывает host arrival, применение или движение. Его legacy MapChange handler немедленно отправляет MAP_READY; это не доказательство загрузки KEX. В новом headless controller это явно control readiness, после неё он ждёт пустой baseline. Второго игрового процесса/renderer нет.
3. Исправленный NET-14 действительно ловит проверенный ADV-09: принятие существующего slot/повышенного generation/forged key ведёт в FAIL. Но source всё ещё допускает PASS для любого REJECT и при некоторых fresh-session WELCOME; seeding считает любой reject доказательством существующего participant. Поэтому15/15 не принимается за общий security proof. Adversarial main возвращает0 после runAll независимо от scorecard; его exit0 тоже недостаточен. Эти ветки в native runtime не используются и здесь не переписывались.
4. `kex_adapter.py` Gemini полезен как дополнительный синтетический gate. Его `inputSeq>0`, две P1 snapshots и workerStopped marker не удостоверяют вызовы native, provenance команды и реальный join. Для этой приёмки используются собственные полные journals/сопоставление send→accept→update→post-traversal motion и OS socket/thread evidence.
5. Найден реальный blocker Worker: чужая wire generation перезаписывалась текущей после обновления lastSeq. [До исправления](evidence/m1/M12A_IMPLEMENTATION_20261102/generation_before.log): wire gen11/seq900 доставлена как gen1; valid gen1/seq1 потеряна. Frozen [исходник до](evidence/m1/M12A_IMPLEMENTATION_20261102/enet_worker_before.cpp) и [reproducer binary](evidence/m1/M12A_IMPLEMENTATION_20261102/generation_before.exe) сохранены. [После проверки равенства gen](evidence/m1/M12A_IMPLEMENTATION_20261102/generation_after.log): foreign/duplicate forwarded0, valid low seq delivered1. Общий код изменён только после этого опыта; существующий foreign-counter теперь учитывает и generation mismatch.

## Владение и применение

Единственный SIMULATION — attested StateTick TID. Только он создаёт Session, дренирует inbound, владеет outbox и вызывает KEX. Worker принудительно работает в bridgeMode; его внутренний legacy WorldSimulation не тикает. Callback worker хранит только transport/session DTO, без KEX pointers/callbacks. Loader/diagnostics дают только atomic Permit после свежего isolation gate; затем требуются60 действительных P1 updates.

Slot1, active connectionGeneration и возрастающая sequence проверяются перед принятием; все накопленные команды сворачиваются в одно состояние движения на tick. Native P1 Update проходит исходный путь; P2 получает ровно один native Update и один Commands на том же simulation TID до штатного Puppet traversal. Native turning/consuming local builders P2 не выполняет. Поддержан только forward/back axis через доказанный digital seam; strafe/look, wire action buttons и weapon selection не применяются. Два источника ввода не конкурируют.

Первый authenticated/ready клиент при готовом host level создаёт P2 один раз. Disconnect очищает ввод и оставляет native P2 на neutral scheduling до штатного world/process exit. Проверенный same-participant reconnect меняет connectionGeneration и повторно привязывает **тот же** Actor/Player, без Reset/Spawn. Timeout —6 simulation ticks плюс150ms wall-time backstop; обнуление input не исправляет velocity/position вручную.

Критические исходящие control/baseline события держит FIFO outbox до isSuccess(pushOutbound). Отказ не считается доставкой. При превышении bound8192 владелец объявляет RESYNC_OUTBOUND_LOSS, прекращает старую сессию и ставит надёжный KICK для reconnect. Inbound resync также прекращает старую сессию. Продолжения с потерянными событиями нет; core pending/retry/resync DOC-40 не заменён. Пустой baseline относится только к diagnostic control domain `M12A_INPUT_ONLY`: он не описывает реальных actors и не означает загрузку удалённого KEX уровня. Real map publication/transitions/replication не приняты этим этапом.

## RUNTIME OBSERVED — run01

PID31260; simulation TID13568, socket I/O worker TID15852. [Launch](evidence/m1/M12A_NATIVE_01/launch.json), [isolation PASS](evidence/m1/M12A_NATIVE_01/ISOLATION_GATE.json), [DLL frozen](evidence/m1/M12A_NATIVE_01/NATIVE_ARTIFACT.json), [полный provider108messages/not truncated](evidence/m1/M12A_NATIVE_01/status_final.json).

| Gate | Наблюдение |
|---|---|
| Connect → настоящий P2 | Native Spawn23:58:51.515Z, отдельные Player/Puppet/node/owner; P1 health200/P2 health200, guards. Ровно один Spawn. Клиент PID38508 — отдельный процесс. |
| Команда → KEX |1629 принятых commands сопоставлены с точными `(slot, generation, input_seq, forward)` в client sends.1475 native updates/Commands и1475 Puppet ticks; каждый update связан с последней принятой командой и motion того же tick/TID. Накопленные commands не дают дополнительных шагов. |
| Hold W | Исходные900 neutral updates не двигали P2. Seq602…618, ticks6149…6174: native путь41.994961 XY units и подъём при collision с P1. Следующий Hold seq647…656, ticks6217…6235 дал127.102574 XY displacement; никакой записи позиции адаптером нет. |
| Release | Seq620/forward0 принят на tick6175; следующие42 native updates до6216 имеют input0. Остаточный XY путь6.859424 — native coasting: max step первых10 ticks1.939470, последних10 —0.044922, последний XY step0. Нельзя выдавать Neutral за мгновенную остановку всех physics coordinates. |
| Silence/timeout | После seq656 на tick6230 новый input отсутствовал. На tick6236 input_timeout очищает4→0; этот же native update neutral. В последующих30 ticks без новых команд W не применяется; инерция наблюдалась и не исправлялась. |
| Disconnect/reconnect | На tick6278 disconnect очищает живой hold4→0. На6296 reconnect gen1→2; Actor/Player те же, Spawn по-прежнему один. Длительный последующий neutral имеет нулевые последние10 XY steps. |
| P1 независим |1475 полных before/after сравнений identity, owner/node/Actor, input32u32, health, owned8, ammo8, weapon/pending/animation совпали. Пять updates содержат ненулевой P1 W; после собственного HWND W P1 переместился88.2341 XY units, при сохранённых health200 и графе. P2 не менял сравниваемые поля P1. |

P2 изначально создаётся в позиции P1 по принятому M1.1 contract. Контакт и вертикальное смещение — реальная native physics; свободная spawn placement policy не добавлялась. Snapshot legacy-поле `generation:1` относится к единственному native Actor lifetime; connectionGeneration в `network_*` равна1/2 и проверяется отдельно.

## StateExit и Direct Quit

**StateExit:** run01 native journal4668–4682.23:59:34.014Z entry → stop/network join.023 → control restore exact16 → один native Reset.149–.151/handle0/P1 unchanged → owned Actor destructor return.157 → StateExit return.161. [OS evidence](evidence/m1/M12A_NATIVE_01/status_state_exit.json): socket0x17e8 закрыт на worker15852. [Независимая OS-проверка после StateExit](evidence/m1/M12A_NATIVE_01/os_after_state_exit.json): TID15852 отсутствует в ещё живом процессе, endpoint28745 отсутствует. Последующий title Quit не повторяет Reset/worker stop; exit0.

**Direct Quit с работающей сетью:** свежий PID33084, simulation37536/worker38452, та же DLL. [Launch/gate](evidence/m1/M12A_DIRECT_QUIT_01/ISOLATION_GATE.json), [full provider141messages/not truncated](evidence/m1/M12A_DIRECT_QUIT_01/status_final.json).582 native updates,900 accepted inputs; live P2 и активный neutral packet stream до pause-menu Quit. Ни StateExit, ни owned Puppet destructor не было.

00:03:27.702Z shutdown с live handle → stop/join.711 → exact controls → Reset.841–.844/handle0/P1 unchanged → engine cleanup.849–.951 → native final.966 → process exit0. [OS wait](evidence/m1/M12A_DIRECT_QUIT_01/RESULT_SUMMARY.json): simulation37536 ожидает именно worker38452 через handle0x1774, WaitForSingleObjectEx result0, GetExitCodeThread0. Socket0x172c реально обслуживался/закрывался worker38452; closesocket return0. Это доказательства завершения worker, дополнительно к native marker.

Новый ownership review: Session/Bridge/Worker создаются на simulation thread после Permit; Session хранится через pointer в pinned Context без CRT destructor. `stop()` синхронно join-ит worker; все его callbacks завершаются, ENet host/peer/socket освобождаются, затем owner-node unique_ptr сбрасывается. После join дренируются terminal inbound events, явно учитываются отменённые critical/outbound/loss reasons, unregister выполняется перед reset queues. В обоих runs cancelled critical1 — terminal CLIENT_LEFT; lost_on_stop0, retired outbound0, resync0, affinity violations0. Память Session/code/Player и MinHook trampolines остаётся retained до process reclaim. Нового release/hot unload нет; native unbind/destructor rules DOC-43 сохранены. OS ExitProcess checkpoint отдельно не снимался и не объявляется снятым.

## Проверки и целостность

| Проверка | Результат |
|---|---|
| Переcобранные `pse_test_suite` / stress / fault injection / bridge acceptance |46/46,11/11,16/16,11/11; [manifest exit codes/хеши](evidence/m1/M12A_IMPLEMENTATION_20261102/REGRESSION_235621_942618.json), полные отдельные logs рядом. Release affinity enforcement включён. |
| Generation reproducer |FAIL до исправления → PASS после; foreign high sequence не отравляет current generation. |
| Новый Session transport test |[PASS](evidence/m1/M12A_IMPLEMENTATION_20261102/adapter_test_01.log): Release,6-tick timeout, terminal resync/reconnect, worker join. Без KEX. |
| Outbox pressure |[PASS](evidence/m1/M12A_IMPLEMENTATION_20261102/pressure_test_01.log): outbound256/256,37 критических отказов, baseline остаётся у Session и доставлен ровно один раз после снятия паузы, без объявленной потери. Без KEX. |
| Wrong-process preflight |Реальная DLL вернула193 до game calls/hooks/log creation; [preflight](evidence/m1/M12A_IMPLEMENTATION_20261102/PREFLIGHT_235525_673480.json). |
| Дополнительные Gemini checks |[Mock client15/15, KEX deferred4](evidence/m1/M12A_IMPLEMENTATION_20261102/GEMINI_CLIENT.json), [runner unit23](evidence/m1/M12A_IMPLEMENTATION_20261102/gemini_runner_unit.log). Это дополнительные тесты инструментов, не независимая native-приёмка. |
| Native offline audit |33/33 условий согласованности run01 и26/26 Direct Quit. Это проверка двух journals, не59 новых игровых экспериментов. Полные fields/segments/OS evidence в RESULT_SUMMARY. |

[Integrity PASS](evidence/m1/M12A_IMPLEMENTATION_20261102/INTEGRITY.json): original77/copy77 совпадают с preparation hashes, aliases/hardlinks нет, полный original snapshot неизменен. Два защищённых профиля before/after/current равны для обоих runs.88 baseline M0/transport files неизменны; одно доказанное transport-исправление перечислено отдельно. Историческое изменение M1.1 runner после старой сборки совпадает с принятым DOC-41 final_harness, к M1.2a не относится. [Process/UDP check](evidence/m1/M12A_IMPLEMENTATION_20261102/PROCESS_CHECK.json):0 игровых/test-client процессов, endpoint28745 отсутствует; [sessions closure](evidence/m1/M12A_IMPLEMENTATION_20261102/SESSIONS_CLOSED.json).

Игра работала с ранее принятым диагностическим OS write-open guard; full captures без script errors/ledger overflow. Это bounded observation, не полный OS sandbox: прежние ограничения direct syscalls/inherited handles/rename/других процессов сохраняются. Изменений Steam-achievement кода/протокола или пользовательских saves нет.

Ошибки сохранены в [журнале попыток](evidence/m1/M12A_IMPLEMENTATION_20261102/HARNESS_ERRORS.md) и BUILD logs: C++ standard/include/target path, provider argv, wrapper argv mutation, UI до появления окна. Они исправлены до зависимых действий; native faults/identity/lifetime violations в двух gameplay runs0. Никакого повторного общего реверса M0, pointer/refcount repair или forced Puppet teardown не было.

## Граница и задачи M1.2b

M1.2a принимает один host process и один headless loopback client, одного slot1/P2, forward/back digital input, silence/disconnect/reconnect и два обычных shutdown paths. Произвольные новые participant/slots после leave, strafe/look/analog input, другие карты/переходы, долгие сессии, malicious floods, crash exits и client game не проверялись. Старый native polling loop13buttons сохраняется; hardware input в целом не сертифицирован этим ограниченным опытом. Пауза не является второй simulation loop.

Оставлено для следующего отдельно заданного этапа:

1. Production session/slot admission и rejoin policy, публикация реальной map identity/epoch, переходы уровня и повторный lifecycle в новой сессии.
2. Остальной movement/look input с независимыми native seams и более широким physical-input тестом P1; spawn placement вне P1, дополнительные нейтраль/coasting случаи на разных surfaces.
3. Host acceptance hardening по channel/header version/length/numeric ranges и соответствующие adversarial tests; уточнение слишком широких NET-14 assertions Gemini.
4. Дальнейшая репликация/игровой клиент — только по отдельному M1.2b scope. Оружие/respawn/campaign/save/achievement и3–8 native players здесь не начинались.

Воспроизведение и команды — [README](../experiments/m1/network_input/README.md). Offline `review.py`/`integrity.py` не запускают игру. **Новых native runs после этих двух не было; M1.2b не начат.**
