# P2-2 — Input Cadence & Timeout Remediation

2026-11-02. **PARTIAL по полной P2-2. Scheduler исправлен и принят в bounded scope, включая один KEX run. Default timeout сохранён: `6 ticks || 150 ms`.** После исправления клиента доступная матрица не показала преимущества 8/150 или 10/150 над 6/150; LAN/Radmin VPN не измерялись. Расширять fail-safe без положительного результата этих измерений не стали. Полноценный M1.2b не начинался.

Основные Evidence: [решения](evidence/m1/M12B_CADENCE_49/DECISIONS.json), [вся before/after статистика](evidence/m1/M12B_CADENCE_49/MATRIX_ANALYSIS.json), [исправленный функциональный сценарий](evidence/m1/M12B_CADENCE_49/FUNCTIONAL_REVIEW_parserfix_.json), [регрессии](evidence/m1/M12B_CADENCE_49/REGRESSION_20261102_100742_764553.json), [native summary](evidence/m1/M12B_CADENCE_NATIVE_01/RESULT_SUMMARY.json). Исторические DOC-44…48 и Gemini Evidence не переписывались.

Метки: **VERIFIED static** — исходники/хеши; **RUNTIME OBSERVED** — сохранённый запуск; **INFERRED** — вывод с указанной границей; **UNKNOWN** — не измерено. Отдельные scheduler mocks не использовались как основание приёмки: запускался настоящий `native_client.cpp`, а также pristine before/after executables.

## 1. Причина и изменения scheduler

В прежнем [native_client.cpp, frozen before](evidence/m1/M12B_CADENCE_49/before/experiments/m1/network_input/src/native_client.cpp) использованы `GetTickCount64()`, `next=now+16`, `client.poll(1)`, чтение control-файла в каждом проходе и `fflush` после каждой записи. **VERIFIED static:** источник времени имеет ограниченную дискретность; Microsoft указывает типичный диапазон 10–16 ms для [GetTickCount64](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-gettickcount64). Привязка следующего срока к фактическому now переносит задержку в будущие отправки. Старый настоящий binary в наших условиях дал около40 Hz. Усиление задержки из-за I/O/polling — вывод, согласующийся с кодом и Gemini; изолированная доля каждого фактора здесь не устанавливалась отдельным ablation-тестом.

Исправлен только headless scheduler в [актуальном native_client.cpp](../experiments/m1/network_input/src/native_client.cpp):

- Монотонный `steady_clock`; deadlines вычисляются от единого epoch как рациональные slots `slot/60 seconds`. От фактического пробуждения сетка не пересчитывается, накопительного drift нет.
- За проход формируется максимум одна актуальная команда. После задержки выбирается текущий слот и пропускаются истёкшие; старая очередь tick-команд не воспроизводится. Sequence увеличивается только при фактическом формировании INPUT, вместе с текущими slot/generation.
- Waitable timer с high-resolution flag; при недоступности — обычный timer, затем `sleep_until`. Busy-spin отсутствует. Поддержка high-resolution flag начинается с Windows10 1803; [контракт Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw). Это не гарантия точных60 Hz при произвольной нагрузке.
- Control-файл читается примерно каждые8 ms, ENet обслуживается nonblocking примерно каждые4 ms и сразу после INPUT dispatch. После длительной задержки control читается до формирования следующего состояния. Hold/Neutral/Silent, Disconnect и reconnect-neutral semantics сохранены.
- Все sent/mode/lifecycle записи сохраняются, добавлены монотонные timestamps, generated time, grid slot/skipped slots и cost counters. CRT journal buffered64 KiB, периодический flush100 ms и закрытие в конце. Интервал flush — цель цикла; при scheduler stall он может быть длиннее. `fflush` не является обещанием физической disk durability. При аварийном выходе возможна потеря buffered tail.

`sent` по-прежнему означает **локальный dispatch**, не host delivery. Реальная очередь ENet проверялась отдельным instrumentation, а native применение — журналом DLL. Опрос control, journaling, wait и весь ENet клиента выполняются одним client thread. Новых queue producers или KEX calls в клиенте нет.

## 2. Измерения actual compiled client

[Baseline](evidence/m1/M12B_CADENCE_49/BASELINE.json), [pristine before binary/hash](evidence/m1/M12B_CADENCE_49/BEFORE_BINARY.json), [stock before run](evidence/m1/M12B_CADENCE_49/NET_stock_before_clean/), [stock after run](evidence/m1/M12B_CADENCE_49/NET_stock_after_clean/).

Кроме pristine executables собраны `before_qa_client` и `after_qa_client`: **те же before/after native_client.cpp**, с QA-only ENet accounting wrappers. Это настоящий клиент, не precision generator/microbenchmark. Передача, throttle и wire bytes не заменены моделью. [Instrumentation generator](../experiments/m1/input_cadence/generate_qa.py), [точные QA-only ENet changes](../experiments/m1/input_cadence/qa_enet/MANIFEST.json) и `.patch` рядом; vendored ENet не редактировался. Production DLL/client линкуют pristine ENet, без этих hooks.

Основная матрица — **32 runs**: before/after × clean и5 профилей по seeds1337/2026/4242. Профили: input-datagram loss5% и20%; delay20±15 ms; loss10% + delay20±15 ms; серия5 последовательных потерянных INPUT-датаграмм каждые90. Задержка применяется в обе стороны; случайная потеря — к upstream datagrams с INPUT, control handshake сохраняется. Это синтетический loopback, не модель всей реальной сети.

У каждого run —4-секундное steady Hold measurement window после handshake/warm-up, затем Release/Hold/Silence/Neutral/quit. `META.json` содержит параметры, hashes, PID/TID, точные phase times; `proxy.jsonl` — raw UDP bytes/delay/drop/forward; `client/qa.jsonl` — queue result/throttle/local drop/socket sends; `host/host.jsonl` — **настоящая Session/Worker/Bridge**, 60 Hz state/tick/accepted/timeout; `host/qa.jsonl` — ENet-dispatched input payloads. Часы измерения монотонные. Seeds воспроизводят решения relay; Windows scheduling/RTT/ENet trajectory не являются детерминированными.

| Clean loopback, 4 s window | Hz | p50 interval ms | p95 ms | max ms |
|---|---:|---:|---:|---:|
| Pristine before, host decoded |39.90|30.002|33.136|34.652|
| Pristine after, host decoded |60.03|16.702|18.395|19.013|
| QA before, actual enqueue |40.07|29.658|33.156|34.235|
| QA after, actual enqueue |60.00|16.679|16.986|17.204|

Main after matrix enqueue frequency59.99–60.01 Hz. В дополнительных функциональных/flush runs наблюдались пропуски слота и interval34.388 ms; это сохранено, не скрыто усреднением. В native run scheduler сообщил13 skipped slots. Абсолютной realtime гарантии нет.

В основной матрице after CPU составлял0.39–2.73% **одного логического CPU**, before — от величины ниже разрешения CPU counter до4.69%. В pristine clean samples: before1.56%, after2.34%; универсальное снижение CPU не заявляется. Постоянной нагрузки busy-spin нет. В after clean за полный client lifetime:691 control reads/60291 µs, journaling1763 µs, flush878 µs, ENet polling16568 µs, dispatch2088 µs. [Полная запись](evidence/m1/M12B_CADENCE_49/NET_after_clean/client/client.jsonl). Это cost measurements стенда, не оценки будущего игрового клиента.

## 3. ENet delivery: throttle, batching и flush

Исследована именно vendored **ENet1.3.18**, commit `2662c0de09e36f2a2030ccc2c528a3e4c9e8138a` по [PROVENANCE](../tools/enet-prototype/third_party/PROVENANCE.md).

- [peer.c](../tools/enet-prototype/third_party/enet/peer.c), `enet_peer_send`: success означает ownership/enqueue. `enet_peer_throttle` меняет throttle по RTT observations; bandwidth limits задают дополнительный cap. Нельзя свести механизм к простому счётчику потерянных UDP.
- [protocol.c](../tools/enet-prototype/third_party/enet/protocol.c), `enet_protocol_check_outgoing_commands`: unreliable packet на fragmentOffset0 проходит `packetThrottleCounter`/scale32; при превышении throttle команда удаляется и packet освобождается. Это происходит **после успешного enqueue**.
- Там же команды собираются в UDP buffers; одна датаграмма может содержать несколько PSE messages. `enet_host_flush` отправляет queued traffic раньше следующего `enet_host_service`; он не отменяет throttle/congestion rules.
- [ENetClientNode::sendPacket/poll](../tools/enet-prototype/src/enet_client.cpp) не менялись. Новый main после enqueue вызывает обычный `poll(0)`, поэтому service отправляет queued traffic без ожидания следующего client frame.

**RUNTIME OBSERVED**, полный run включая tail, единицы измерения различаются:

| Run | Generated INPUT | ENet queued | Local throttle discard INPUT | UDP datagrams sent | INPUT внутри UDP | Relay dropped INPUT | Host dispatched INPUT | Session accepted INPUT |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| [Before clean](evidence/m1/M12B_CADENCE_49/NET_before_clean/ANALYSIS.json) |214|214|0|245|214|0|214|214|
| [After clean](evidence/m1/M12B_CADENCE_49/NET_after_clean/ANALYSIS.json) |321|321|0|356|321|0|321|321|
| [After jitter, seed4242](evidence/m1/M12B_CADENCE_49/NET_after_jitter_4242/ANALYSIS.json) |320|320|35|316|285|0|260|255|
| [After combined, seed4242](evidence/m1/M12B_CADENCE_49/NET_after_combined_4242/ANALYSIS.json) |319|319|16|331|303|34|234|233|

Во втором jitter примере throttle опускался до26/32:35 queued commands были действительно отброшены sender-side. Затем ENet suppresses reordered unreliable-sequenced traffic, а Worker mailbox может сворачивать несколько входных состояний в последнее. Поэтому forwarded → dispatched → Session accepted — три разных наблюдения, а не общий «процент UDP потерь». Их differences и identity reconciliation сохранены в ANALYSIS.json каждого run. В after runs все22 instrumented queue/socket/throttle accounting checks сошлись; это не относится к uninstrumented stock boundary.

**Ограничение before telemetry:** в `NET_before_loss20_4242` seq215/gen1 и первоначальном `NET_before_functional` seq302/gen2 по одной queued INPUT не наблюдались ни в socket send, ни в throttle discard. Они относятся к terminal tail; peer reset удаляет outgoing queue при Disconnect/quit. Такая классификация — **INFERRED**, reset-removal не был отдельным instrumentation counter. Strict conservation check этих двух before runs остаётся false и не переименован в PASS. Оба остатка и IDs сохранены; в sender throttle их не записывали. Основные 4-секундные cadence/timeout windows от этих terminal tails не зависят.

**Throttle decision: UNCHANGED.** Congestion control полезен при настоящей перегрузке, где принудительное32/32 может увеличивать queues/задержку/потери других сообщений. Для scheduler fix отключение или настройка throttle не требуется. Reliable/unreliable/channel policy и v1.10 bytes сохранены.

**Explicit flush decision: UNCHANGED.** Отдельно выполнены3 after QA runs с немедленным `enet_host_flush` в test wrapper, те же combined settings/seeds. В production этот test switch отсутствует.

| Seed | Normal service after send: queue→UDP p50/p95 ms | Explicit flush p50/p95 ms | Normal/flush UDP count, 4 s | Normal/flush estimated IPv4+UDP bytes/s |
|---|---|---|---|---|
|1337|0.035 /0.068|0.029 /0.058|255 /257|5154 /5168|
|2026|0.036 /0.072|0.029 /0.053|258 /259|5175 /5183|
|4242|0.035 /0.069|0.029 /0.060|244 /248|4906 /5002|

Стоимость network overhead рассчитана как наблюдаемые UDP payload bytes +28 bytes на IPv4/UDP datagram, без lower-link overhead. Разница queue→socket — микросекунды; устойчивого выигрыша по delivered commands нет: decoded Hz normal/flush51.21/49.49,51.37/50.88,43.17/44.23. RTT/throttle траектории различаются, поэтому это не статистическое доказательство равенства. В измеренных after случаях max INPUT batch=1 и так; дополнительный flush не даёт полезной latency разницы и может дробить control batching. [Все3 flush runs](evidence/m1/M12B_CADENCE_49/NET_after_flush_1337/), [2026](evidence/m1/M12B_CADENCE_49/NET_after_flush_2026/), [4242](evidence/m1/M12B_CADENCE_49/NET_after_flush_4242/).

## 4. Timeout decision и fail-safe

В [network_input.hpp](../experiments/m1/network_input/src/network_input.hpp) выделен внутренний `InputTimeout` с predicate `elapsedTicks>=ticks || elapsedMs>=wallMs`, default **6/150**. Session owner получает его при construction; wire/API KEX не затронуты. [network_input.cpp](../experiments/m1/network_input/src/network_input.cpp) применяет тот же predicate и журналирует threshold, elapsed ticks/wall при expiry. Изменение диагностики и возможность проверить кандидатов не означают изменение default.

В QA host три shadow candidate состояния обрабатывают **один и тот же accepted stream** с этим predicate. Дополнительно [timeout_contract.cpp](../experiments/m1/input_cadence/timeout_contract.cpp) проверил **настоящие Session/Worker/ENet** отдельно с6/150,8/150,10/150: tick expiry, wall backstop при малом числе тиков, Release, level-not-running, held Disconnect, gen2 neutral, stale old-generation high-seq rejection, stop/join/idempotence/affinity. [Raw](evidence/m1/M12B_CADENCE_49/REG_timeout_contract_20261102_100741_797901/console.log): failures0.

| Candidate | Номинальный срок при60 ticks/s, с OR | False dropouts before,16 main runs | False dropouts after,16 main runs |
|---|---|---:|---:|
|6 ticks /150 ms|около100 ms|8|0|
|8 ticks /150 ms|около133 ms|6|0|
|10 ticks /150 ms|wall150 ms обычно раньше10 ticks|1|0|

Это события в 4-секундных **намеренно удерживаемых** measurement windows, отдельно от предусмотренных Silence/Disconnect/stall phases. Большинство before событий относятся к burst5 профилю; old slow cadence превращает пять потерянных samples в более длинный разрыв. After scheduler устранил события в этой ограниченной матрице уже при6/150. Нулевой результат не гарантирует отсутствия dropouts в реальной сети. **Увеличивать default на основании этих after данных не обосновано; выбран действующий минимальный6/150, а полная timeout remediation остаётся не принятой.**

Семантика путей сохранена:

- Доставленный Release/Neutral меняет input state сразу при consume, не ждёт timeout. Потеря первого Release компенсируется последующим neutral state; при исчезновении всех новых states работает fail-safe.
- Silence при удержании W запускает OR timeout. На каждом packet arrival deadline обновляется только после session/gen/seq validation.
- Настоящий Disconnect neutralizes при обработке terminal event; new generation начинается neutral. При `!gameplay` и stop используется немедленный отдельный neutral path.
- Stale old-generation input отвергается Worker до mailbox/sequence mutation; R1/R2 routing/admission policy сохранены.

## 5. Functional tests настоящих клиентов

[Исправленные before/after runs](evidence/m1/M12B_CADENCE_49/FUNCTIONAL_REVIEW_parserfix_.json): **14/14 условий для каждого**. Сценарии: Hold/Release/Neutral; действительная потеря первой neutral INPUT; Silence; Disconnect при W; reconnect gen1→2;400 ms scheduler suspension;350 ms suspension с Release во время паузы; остановка и запуск нового процесса.

After: даже с потерянным первым Release neutral принят примерно через39.55 ms после control write, без timeout. Это задержка control polling + send phase + потерянный sample + host tick, не5 ms native result ниже. После400 ms pause сформировано4 команды за первые50 ms; просроченная пачка из24 commands не отправлена. Суммарно functional client зарегистрировал44 skipped slots для двух пауз. Release во время паузы стал первым новым transmitted input state после resume.

Внутри того же процесса reconnect сохраняет secret key и получает generation2. **Свежий процесс без утраченного secret в том же host ledger ожидаемо получает REJECT8**, не чужое управление и не новый slot. Raw restart console сохранён в обоих corrected functional folders; это принятый DOC-47 contract, не успешная auth без ключа. Автоматическое сохранение ключа на диск не добавлялось. Fresh-host starts нового client process также многократно выполнялись матрицей. Политика key persistence остаётся отдельной границей.

## 6. Регрессии и сохранённые ошибки стенда

Все existing tests/assertions сохранены. [Manifest executable hashes/argv/exit](evidence/m1/M12B_CADENCE_49/REGRESSION_20261102_100742_764553.json).

| Проверка | Результат / raw |
|---|---|
|pse_test_suite|[46/46, exit0](evidence/m1/M12B_CADENCE_49/REG_pse_test_suite_20261102_100649_074516/console.log)|
|pse_stress_test|[11/11, exit0](evidence/m1/M12B_CADENCE_49/REG_pse_stress_test_20261102_100658_603819/console.log)|
|pse_fault_injection|[16/16, exit0](evidence/m1/M12B_CADENCE_49/REG_pse_fault_injection_20261102_100702_984803/console.log), штатный F16 abort preserved|
|pse_bridge_acceptance|[11/11, exit0](evidence/m1/M12B_CADENCE_49/REG_pse_bridge_acceptance_20261102_100711_646822/console.log)|
|R1 DOC-46|[live verdict exit0](evidence/m1/M12B_CADENCE_49/REG_r1_live_20261102_100730_577082/console.log)|
|R2 DOC-47|[strict live verdict, argv r2, exit0](evidence/m1/M12B_CADENCE_49/REG_r2_fixed_20261102_100732_874682/console.log)|
|Generation/FIFO/outbox routing|[5 cases,330 checks,0 failures](evidence/m1/M12B_CADENCE_49/REG_routing_contract_20261102_100737_863735/console.log)|
|Admission/rejoin|[4 scenarios,112 checks,0 failures](evidence/m1/M12B_CADENCE_49/REG_admission_contract_20261102_100738_381438/console.log)|

Standalone Session timeout tests also проверили Release SPSC affinity, no stale input poisoning, one stop/join и level/stop neutral. Worker/Bridge/DTO implementations не редактировались. Source selection и late-enqueue generation capture не изменились.

В Evidence сохранены все выявленные ошибки, без выдачи initial incomplete tests за PASS:

1. Первая production-client compilation не имела NOMINMAX перед Windows header; QA build уже задавал его. [Build failure](evidence/m1/M12B_CADENCE_49/BUILD_20261102_094911_109936.log), исправление header guard, subsequent successful builds сохранены.
2. Первый smoke не мог читать live CRT-locked host.jsonl. [META/traceback](evidence/m1/M12B_CADENCE_49/NET_before_smoke/META.json). Host теперь пишет отдельный closed ready.flag; повторён только этот precondition с новым run name.
3. WinSock UDP ICMP-unreachable после client socket close вызвал10054 в relay recvfrom. [Original combined failure](evidence/m1/M12B_CADENCE_49/NET_before_combined_1337/META.json); relay теперь записывает этот event и продолжает drain. Завершённые runs переиспользованы; новый run `_icmpfix` сохранён отдельно.
4. В первом decoder forward читался по pitch offset24 вместо moveForward offset32. **Raw bytes сохранены**, core loss/burst decisions зависели от наличия INPUT, не forward, поэтому cadence/sequence accounting пересчитаны из raw без повтора матрицы. Но first-Release drop действительно не включился: [initial review13/14](evidence/m1/M12B_CADENCE_49/FUNCTIONAL_REVIEW.json) сохранён. Смещение сверено с неизменённым serializer и добавлен real-packet check. Повторены только2 functional runs с `parserfix_`; оба14/14. Старое derived поле inputs.forward в ранних relay logs не используется как авторитетный payload decode.

43 completed standalone captures включают2 initial functional captures с неполным Release impairment. Это не43 безусловных PASS. Два failed startup/relay attempts также сохранены. Никакие эти исправления стенда не потребовали повторного KEX run.

## 7. Единственный native integration run

До запуска: [autonomous gate](evidence/m1/M12B_CADENCE_49/AUTONOMOUS_GATE.json), [wrong-process DLL rejection193](evidence/m1/M12B_CADENCE_49/PREFLIGHT_100704_624296.json), [OS no game/client/port](evidence/m1/M12B_CADENCE_49/OS_before_native.json), copy/original integrity. После gameplay/P1 witness до Permit: [ISOLATION_GATE PASS](evidence/m1/M12B_CADENCE_NATIVE_01/ISOLATION_GATE.json).

Exact EXE SHA256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`. Использовалась только независимая копия `experiments/m0/game`, fresh profile `experiments/m1/input_cadence/profile/M12B_CADENCE_NATIVE_01`. [Launch](evidence/m1/M12B_CADENCE_NATIVE_01/launch.json): game PID33188, simulation TID11724; external current headless client PID9964; Worker TID7800. [Loaded/frozen DLL](evidence/m1/M12B_CADENCE_NATIVE_01/NATIVE_ARTIFACT.json).

Существующие native ABI/sites/lifetime hooks byte-identical: координаты VA/RVA/file offsets и бинарный SHA — [DOC-48](48_M12B_WINDOW_CLOSE_SHUTDOWN.md), [accepted coordinate manifest](evidence/m1/M11_IMPLEMENTATION_20261101/NATIVE_COORDINATES.json). Нового реверса, KEX fields/ABI, raw pointer repairs и Puppet destruction нет. Passive shutdown observer переиспользован из DOC-48, заново bound к PID/prefixes.

**RUNTIME OBSERVED** — [native10324 records](evidence/m1/M12B_CADENCE_NATIVE_01/native.jsonl), [client](evidence/m1/M12B_CADENCE_NATIVE_01/client.jsonl), [scripted phases](evidence/m1/M12B_CADENCE_NATIVE_01/CLIENT_SEQUENCE.json), [full capture316/not truncated](evidence/m1/M12B_CADENCE_NATIVE_01/status_final.json), [summary25/25 offline consistency checks](evidence/m1/M12B_CADENCE_NATIVE_01/RESULT_SUMMARY.json).

| Событие, UTC2026-11-02 | Наблюдение |
|---|---|
|10:10:00.466|Один native Spawn. P1 Player0x140af33e0/Actor0x61b7f680; P2 Player0x7ff9f6c520e0/Actor0x61b7eaf0/Node0x61f47060/свой owner. Оба health200.|
|Hold phases|Реальные native Commands/Update/Puppet traversal, max XY displacement259.036 units. Никаких position/velocity writes.|
|10:10:19.416→.433|Последний held seq1142 tick3329 → Release seq1143 tick3330. Native drive0 в том же tick3330, примерно5 ms после client generation; timeout ещё не происходил.|
|10:10:20.334|После второй Hold и Silence: `network_input_expired`, elapsed_ticks6, threshold6/150; тот же native update neutral. wall elapsed93 ms — квантованное GetTickCount64 наблюдение, не подмена6-tick интервала.|
|До shutdown|3435 native P2 updates/Commands/Puppet ticks; все сравниваемые P1 fields совпали, оба health200, guards.3394 accepted commands найдены в client sends; Worker received3403; client сформировал3406. Coalescing/terminal tail не объявляются дополнительными применениями.|
|10:10:57.776–.784|WM_CLOSE с live handle и сетью → stop → реальный WaitForSingleObjectEx на Worker7800. Socket0xa24 closesocket return0 на7800; wait0/GetExitCodeThread0 на11724.|
|.818–.901|Четыре control exact16 restore → один exit Reset.896/handle null/P1 unchanged/guards → quiesced → engine cleanup entry.901.|
|.962–.997|Engine return → pinned native final, foreign_calls0/ticks_after_shutdown0/failed=false → process exit0.|

Native Release снимает **drive**, не гарантирует мгновенную остановку native coasting. P1 independence относится к принятому набору identity/input/health/inventory/weapon fields, не ко всей physics. Runtime проверяет одну gen1 loopback связь; native rejoin/loss/jitter в этом run не воспроизводились. Новые client scheduler hitches13 slots были пропущены, не догонялись пачкой. OS Worker join/socket close положительно наблюдены до Reset, а не выведены из process exit0.

## 8. Build/source hashes, diff, integrity

Новые builds находятся только в `experiments/m1/input_cadence/build`; исторические accepted DLL/client не заменялись. Release x64, C++20 adapter, /MT, SDK10.0.26100.0. Production native DLL использует pristine ENet/accepted MinHook; QA copies отдельно.

[Final build/source manifest](evidence/m1/M12B_CADENCE_49/BUILD_MANIFEST_20261102_100611_477963.json), [canonical source inventory commitment](evidence/m1/M12B_CADENCE_49/SOURCE_COMMITMENT.json), [build raw](evidence/m1/M12B_CADENCE_49/BUILD_20261102_100611_477963.log), [suite build raw](evidence/m1/M12B_CADENCE_49/SUITES_BUILD_20261102_100611_477963.log), [final frozen sources](evidence/m1/M12B_CADENCE_49/sources_20261102_100611_477963/), [artifacts](evidence/m1/M12B_CADENCE_49/artifacts_20261102_100611_477963/), [diff](evidence/m1/M12B_CADENCE_49/SOURCE_CHANGES.patch), [final diagnostic manifest](evidence/m1/M12B_CADENCE_49/HARNESS_MANIFEST.json).

| Объект | SHA256 |
|---|---|
|Pristine before client, DOC-48|`848d2f6b29f20c36a33504d19bcc737b4f1d2351c17d6a2c0df527f94912d117`|
|Pristine after client, включая native run|`3893810cd0f8de043efd6199d25b7e8cbf600adcc823782faaa6a0387b7bdd83`|
|QA before actual client|`9e31789d6b70c7e943d26676d9ff60d81462326d38e10436ac0ed7c213ac408e`|
|QA after actual client|`e0a6cd41585d7a044b9a80a40ab6ec939fbfd14844b2a090ba1d45d09f96b6d7`|
|Новая согласованная DLL|`0ba9670f234ea3626e5c534422d18afefe572caf0fcf3cedc4323e706b4130db`|
|native_client.cpp|`aa72b5394456e988eb52c5625245fec240fdcec86d96512ff90255ebf03a524f`|
|network_input.cpp|`ed8e93de2e7ae334ea7118d8c77309c42703401d2e8b97f973cd8c135189086b`|
|network_input.hpp|`a996b15c4203b87f46a98584f0ca1af322fd3382b2ce9f4a033b8856e5766349`|
|Final BUILD_MANIFEST.json bytes|`35003d3a101d1aeb780907f108fa7e12cd0211d1d1d730ed9c29b4af7518a2f3`|

Изменены ровно3 production files в таблице. Native bootstrap/hooks/profile, ENet shared implementation/vendored sources, Worker/Bridge/DTO, wire v1.10 и historical tests не менялись. Дополнительно созданы [изолированные инструменты DOC-49](../experiments/m1/input_cadence/README.md), snapshots/logs/docs.

[Перед native](evidence/m1/M12B_CADENCE_49/INTEGRITY_20261102_100703_904646.json) и [после](evidence/m1/M12B_CADENCE_49/INTEGRITY_20261102_101124_824810.json):952 baseline files checked, только3 разрешённых source changes, unexpected changes0. Original77/copy77 unchanged/no alias; полный original snapshot unchanged; оба protected profiles unchanged. Отдельные [native protected before](evidence/m1/M12B_CADENCE_NATIVE_01/protected_before.json)/[after](evidence/m1/M12B_CADENCE_NATIVE_01/protected_after.json) равны. Guard errors/overflow0, успешных наблюдавшихся outside disk write-opens0; guard остаётся bounded diagnostic, не полным OS sandbox.

[Final OS](evidence/m1/M12B_CADENCE_49/OS_final.json):game/client/QA processes и проверяемые UDP endpoints0. [Все7 diagnostic sessions закрыты](evidence/m1/M12B_CADENCE_49/SESSIONS_CLOSED.json). Client завершился штатно после игры; повторного native run нет.

## 9. Оставшиеся ограничения и остановка

**Scheduler — bounded PASS. Полная P2-2 — PARTIAL.** Доказано улучшение настоящего генератора40→60 Hz в доступных условиях, bounded missed-slot handling и корректные state/lifetime пути. Default timeout не retuned: доступные after данные не дают положительного результата расширения, а LAN/VPN optimum **UNKNOWN**. Поэтому полная P2-2 не объявляется закрытой. Следующий шаг по отдельному заданию — более длительная и реальная channel calibration, с учётом stop/run-on tradeoff; не автоматическая смена на8/150.

Сохранены [Gemini REPORT](../tools/m12b-input-cadence-qa/REPORT.md) и [INTEGRITY_AUDIT](../tools/m12b-input-cadence-qa/INTEGRITY_AUDIT.md), весь их stand unchanged. Эти данные учтены как independent experimental inputs; рекомендация8/150 не стала обязательной спецификацией. Они не являются LAN/VPN acceptance.

Публичные серверы/NAT, новая игровая клиентская DLL, world replication, campaign/map transitions, look/strafe/weapons и3–8 players не начинались. PROGRESS/EVIDENCE_INDEX обновлены. **После DOC-49 работа остановлена.**
