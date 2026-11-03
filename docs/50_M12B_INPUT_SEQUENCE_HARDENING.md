# P2-5 — Input Sequence Hardening

2026-11-02. **PASS для P2-5.A/B в текущем bounded sender scope после DOC-49.** Worker отвергает pathological initial/forward sequence до изменения mailbox. Причины отказа разделены. Новый запуск KEX, игровая DLL, M1.2b client integration и replication не выполнялись.

[Итог с фактическими значениями](evidence/m1/M12B_SEQUENCE_50/RESULT_SUMMARY_20261102_104928_504340.json), [финальные argv/exit/SHA/logs](evidence/m1/M12B_SEQUENCE_50/TESTS_after_20261102_104758_462714.json), [build/source manifest](evidence/m1/M12B_SEQUENCE_50/BUILD_MANIFEST_after_20261102_104621_810634.json), [полный source diff](evidence/m1/M12B_SEQUENCE_50/SOURCE_CHANGES.patch), [воспроизведение](../experiments/m1/input_sequence/README.md). **VERIFIED static** ниже — исходники/хеши; **RUNTIME OBSERVED** — автономные ENet/Session runs, не KEX. Неизмеренное явно ограничено в §8.

## 1. Причина и фактический INPUT path

**VERIFIED static**, источники сохранены в build manifests before/after:

1. `ENetHostServer::poll` получает ENET_EVENT_TYPE_RECEIVE; `onReceive` проверяет header/magic/version. В bridge mode INPUT направляется custom handler только после поиска authenticated peer в `m_peers`. Legacy `WorldSimulation::enqueueCommand` не является действующим gameplay-путём Session.
2. `ENetWorker::handleHostCustomPacket` → `handleHostInput`: десериализуется **payload `commandSequence`**, а не sequence из header. Header sequence в этом handler не участвует в acceptance. `resolvePeerSlot` → `getPeerSession` получает slot/generation из authenticated peer ledger и сверяет claimed slot. Wire generation должна точно совпасть с peer generation.
3. До исправления условие было только `mb.hasSeq && commandSequence <= mb.lastSeq`. Первый sequence был неограничен; любой больший, включая MAX после seq1, принимался. Затем записывались `hasSeq/lastSeq`, `lastButtons`, accumulated pressed edges/attack/use, latest DTO, dirty; увеличивались received/coalesced. Проверка Session уже не могла восстановить mailbox. Нейтрализация/timeout не сбрасывает sequence. Это и есть P2-5.A.
4. `flushInputMailboxes` сначала ждёт lifecycle pending FIFO, затем переносит latest DTO + accumulated edges в ThreadBridge. При backpressure mailbox удерживается/объединяется; при успехе очищаются dirty/edges/counts, **lastSeq остаётся**. ThreadBridge сохраняет DTO в SPSC без арифметики sequence. `BridgeInputCommand::fromNet` переносит payload sequence; generation дополнительно ставится из ledger.
5. `Session::consume` проверяет active/ready/levelReady, slot1, generation, strict monotonic sequence и finite axes/angles до `hasSeq/lastSeq/lastInputTick/lastInputMs/buttons`. При reconnect lifecycle сбрасывает sequence/ready; CLIENT_LEFT нейтрализует.
6. `bootstrap.cpp` на simulation thread вызывает Session tick; только затем `wantsPlayer` и принятый native update path читают `sequence/generation/nativeButtons`. Forward/back mapping остаётся 4/8, max один native update за tick. Hooks/KEX ABI/Player/Puppet lifecycle не редактировались и здесь заново не исполнялись.

Во втором дефекте generation mismatch ошибочно увеличивал `inputsRejectedForeign`, хотя комментарий счётчика описывал slot ownership. До sequence проверки mailbox при этом не менялся; это ошибка диагностики P2-5.B.

## 2. Before reproduction, до product edits

[Снимок baseline](evidence/m1/M12B_SEQUENCE_50/BASELINE.json), [before manifest](evidence/m1/M12B_SEQUENCE_50/BUILD_MANIFEST_before_20261102_104137_881529.json), [точные before argv/SHA](evidence/m1/M12B_SEQUENCE_50/TESTS_before_20261102_104140_763600.json).

**RUNTIME OBSERVED:** настоящий authenticated participant `sequence50`, slot1/gen1, MAP_READY. Последовательно доставлены INPUT1, INPUT4294967295 с attack, INPUT2. [Mailbox/Session raw](evidence/m1/M12B_SEQUENCE_50/RUN_before_sequence_contract_20261102_104140_169398/console.log):

| Посланный seq | mailbox lastSeq / latest | Session seq | received / stale / foreign |
|---|---|---|---|
|1|1 / 1|1|1 / 0 / 0|
|4294967295|4294967295 / 4294967295|4294967295|2 / 0 / 0|
|2|4294967295 / 4294967295|4294967295|2 / 1 / 0|

Последний mailbox: generation1, hasSeq1, dirty0 после flush, lastButtons1, pending edges/presses0. Нормальный seq2 действительно заблокирован, connection не обрывалась. Exit0 before означает успешное воспроизведение **проверенного defect predicate**, а не исправный продукт.

Независимо [normal threaded Worker + Session](evidence/m1/M12B_SEQUENCE_50/RUN_before_sequence_live_20261102_104140_376685/console.log) воспроизвёл то же на обычном unreliable INPUT: `LIVE_REPRO slot=1 generation=1 seq=4294967295 received=2 stale=1 foreign=0 connected=1 ready=1`. Ни один predicate не читает PASS marker.

## 3. Acceptance policy и её обоснование

[Новый чистый predicate](../tools/enet-prototype/include/input_sequence.h) вызывается **на каждом wire INPUT в Worker**, после authenticated slot/gen проверок, до любых mailbox writes/reset:

| Состояние | Условие / результат |
|---|---|
|Нет принятого sequence этой generation|0…10800 включительно — ACCEPT; больше — EXCESSIVE_JUMP|
|Есть lastSeq|next ≤ lastSeq — STALE; иначе next-lastSeq ≤1800 — ACCEPT; больше — EXCESSIVE_JUMP|
|Wrong slot/gen|Отказ до sequence проверки, свой счётчик; sequence/mailbox неизменны|

**Почему 1800:** actual sender DOC-49 — 60 Hz; vendored `ENET_PEER_TIMEOUT_MAXIMUM` =30000 ms (`enet.h`). Выбран запас на 30 секунд целиком пропущенных команд: 60×30, существенно больше 6-tick/150-ms movement fail-safe. Это локальный потолок восстановления, не утверждение, что ENet всегда разрывает INPUT-only loss через30s: reliable ping/control могут сохранять peer. При длительном INPUT-only blackout выше этого бюджета применяется fail-closed; автоматический disconnect/KICK не добавлен.

**Почему initial10800 отдельно:** текущий `native_client.cpp` сохраняет свой INPUT counter на authenticated reconnect внутри процесса; он ограничен deadline180s и fixed rational60Hz grid. Поэтому первый INPUT новой generation может легально иметь sequence значительно больше1. 60×180=10800 покрывает весь текущий sender lifetime, включая такой reconnect. Ограничение initial1800 или «только1» сломало бы существующий sender после30s работы. Неограниченное first-packet acceptance оставило бы первоначальный обход с MAX. Initial10800 проверяется и при `hasSeq=false`; wire не меняется, sender/scheduler не меняется.

Паузa/silent/skipped scheduler slots не увеличивают counter: первый вновь сформированный INPUT не требует расширения окна на длину паузы. Потеря/перестановка unreliable поддерживаются как latest-state contract: более новый в пределах окна принимается; пришедшие после него старые/дубликаты отвергаются, предыдущее движение не воспроизводится.

Отказ меняет только соответствующий atomic counter. Не меняются generation, hasSeq, lastSeq, lastButtons, pressedEdges, attack/usePresses, latest, dirty, received/coalesced и Session input timestamps. Auth/end callbacks по-прежнему сбрасывают mailbox по native transport lifecycle. Старый wire gen не может инициировать reset новой generation. Изменений admission/rejoin policy нет.

**Почему upper bound не повторён в Session:** mailbox может объединить цепочку многих допустимых advances за долгое отсутствие simulation consumer. Такой DTO легально отстоит от предыдущего consumed seq больше1800. Повторный per-packet bound на нём блокировал бы дальнейшее управление. Worker — единственный untrusted ingress; после его проверки trusted Bridge DTO сохраняет sequence. Session сохраняет исходную monotonic/identity/finite защиту до мутации. Финальный тест валидирует1901 последовательную команду Worker, затем одним flush доставляет latest1901 Session с предыдущим0; принятие подтверждено. Произвольная запись DTO внутрь доверенного процесса не входит в модель authenticated remote INPUT.

## 4. UINT32_MAX / wraparound

INPUT wire v1.10 описывает monotonic u32 counter; отдельная modular snapshot semantics не переносится на INPUT. Новая policy **не модульная**. Сначала unsigned comparison `next <= last`, затем безопасное unsigned subtraction при next>last; `last+window`, signed casts/overflow, half-range ambiguity отсутствуют.

MAX с начального/небольшого last отклоняется. При legitimately established last=MAX-2 числа MAX-1 и MAX могут быть приняты как advances1; затем0/1/старые номера — STALE. Не возникает случайного принятия старого номера как нового. Дальше нужна новая authenticated generation **и низкий counter**; один reconnect с прежним MAX не обходит initial guard. Автоматического KICK/rotation нет. Текущий180s sender естественно не приближается к этому пределу; будущее неограниченное приложение должно явно reset counter при новой generation до исчерпания, отдельным изменением sender, без изменения wire. Boundary test seed не выдаётся за runtime миллиардов команд.

## 5. Минимальный diff и counters

Изменены3 existing files и добавлен один header, [полный patch](evidence/m1/M12B_SEQUENCE_50/SOURCE_CHANGES.patch):

- `enet_worker.cpp`: ingress predicate перед mutations/reset; generation reason исправлен; load/reset новых counters.
- `enet_worker.h`: два новых stats fields, добавленных в конец stats struct, и два atomics. Existing имена/типы stats сохраняются; бинарную совместимость отдельно собранных transport objects не обещаем, они пересобраны вместе. KEX ABI не затронут.
- `network_input.cpp`: additive JSON поля shutdown statistics и комментарий trusted/coalesced consumer. Executable predicate Session остался прежним.
- `input_sequence.h`: constexpr policy/predicate без времени, allocations и wire fields.

| Counter / JSON field | Финальное значение причины |
|---|---|
|inputsRejectedForeign / rejected_foreign|Несовпадение claimed slot и sending peer / неразрешённый peer в existing resolver. Generation сюда больше не входит. Resolver как прежде используется и другими slot-bearing requests.|
|inputsRejectedGeneration / rejected_generation|Wire generation не равна authenticated peer generation, включая stale/future gen.|
|inputsRejectedStale / rejected_stale|Duplicate или numerically older, включая wrap0 после MAX.|
|inputsRejectedJump / rejected_jump|Excessive forward delta либо initial выше10800.|

Existing consumers сохраняют старые поля; только ошибочное смешение generation с foreign исправлено. Парсеры JSON получают два additive keys. Никакого нового per-packet journal/spam в production Worker нет. Причины exclusive: foreign → generation → stale/jump → accept. ResetStats обнуляет обе новые причины; это проверено.

## 6. After, boundary/stress/regression Evidence

[Final mailbox raw](evidence/m1/M12B_SEQUENCE_50/RUN_after_sequence_contract_20261102_104651_981514/console.log), [все26 снимков](evidence/m1/M12B_SEQUENCE_50/BOUNDARY_STATES.txt). Slot1/gen1:

| INPUT | Фактическое последующее состояние |
|---|---|
|Первый MAX|hasSeq0, last/latest0, received0, jump1; остальные mailbox fields0|
|0, duplicate0,1,2, duplicate2, old1|last/latest/Session2; received3, stale3, jump1|
|MAX с attack/use после2|last/latest/Session2, buttons/edges0; received3, stale3, jump2|
|Нормальный3 после MAX|last/latest/Session3; received4, stale3, jump2|
|33 →1833 →3634 →1834|33 и jump1800 приняты; jump1801 отказан без сдвига1833; нормальный1834 принят|
|Foreign slot2/MAX, wrong gen2/MAX при gen1|foreign1, generation1; jump/stale неизменны|
|Настоящий reconnect в gen2; stale gen1/MAX|новая hasSeq0/last0; generation2, других mutations нет|
|Initial10801, затем10800|первый отказан; второй принят в gen2, Session10800|
|Gen3 first1; boundary seed MAX-2 →MAX-1 →MAX →0/1/old|новые верхние приняты; wrapped/old stale; gen4 first0 вновь принят|
|1901 валидных commands без flush → один DTO|Worker chain и Session1901 приняты; без queue overflow|
|Dirty mailbox1902/attack →MAX/use|dirty1, last/latest1902, edges1/attack1/use0 сохранены; Session1901|

**Boundary contract: 282 checks,0 failures**, включая220 arithmetic reference cases над independently widened uint64 comparison/difference, high marks по обе стороны2^31 и околоMAX. Это число условий, не282 отдельных сетевых сценария. Reliable test delivery на INPUT channel обеспечивает реальную доставку stale/duplicate до application filter; normal live тест использует обычный unreliable канал.

[Финальный threaded stress raw](evidence/m1/M12B_SEQUENCE_50/RUN_after_sequence_live_20261102_104652_647086/console.log): одна MAX попытка после1, normal2 принят; затем12s/720 generated commands на60Hz. Детерминированно omitted84 payloads (включая серии5), delayed49 payloads на2–4 slots, duplicate38, дополнительные старые seq; reconnect в gen2 по actual key и stale gen1/MAX. Это application payload impairment перед обычным ENet send, **не измерение UDP datagram loss/physical network**. Throttling не отключался.

Получено594 valid inputs в Worker,592 Session acceptances,103 stale,1 wrong generation,1 excessive jump;2 coalesced, pendingPeak0, escalation0, affinity0. Каждый из12 секундных windows продвинул accepted sequence; последний gen2/seq723 принят. Connected/ready сохранялись, post-poison stream не блокировался. Terminal cancelled_critical1 — CLIENT_LEFT при stop, как journal показывает; lost_on_stop0/resync0. Это не manufactured overflow для счётчиков.

| Финальные regressions | Результат |
|---|---|
|pse_test_suite|46/46, exit0|
|pse_stress_test|11/11, exit0|
|pse_fault_injection|16/16, exit0; штатный F16 affinity child abort сохранён|
|pse_bridge_acceptance|11/11, exit0|
|R1 DOC46, live verdict до cleanup|gen2 остаётся connected/authenticated/ready, exit0|
|R2 DOC47, argv `r2`|beta rejected, alpha slot1/gen2/ready, exit0|
|Generation routing contract|5 scenarios/330 checks,0 failures|
|Admission/Rejoin contract|4 scenarios/112 checks,0 failures|
|Actual Session timeout contract|6/8/10 test candidates, failures0; default6/150 unchanged|
|Native consumer compile|object build `/W4 /WX`, success; KEX runs0|

Точные raw links каждого regression, argv и SHA — [final manifest](evidence/m1/M12B_SEQUENCE_50/TESTS_after_20261102_104758_462714.json). Existing test sources/assertions не редактировались. Scheduler `native_client.cpp` и timeout header byte-identical DOC49; Session tick/timeout implementation также не менялась. Промежуточный extra upper bound Session удалён до финальных runs; новый coalescing test проверяет причину. [Ошибки нового стенда и intermediate history](evidence/m1/M12B_SEQUENCE_50/HARNESS_ERRORS.md) сохранены, не объявляются product PASS.

## 7. Build/source SHA256 и integrity

Release x64 MSVC C++20 /MT, SDK10.0.26100.0. [Final build raw](evidence/m1/M12B_SEQUENCE_50/BUILD_after_20261102_104611_914969/console.log), [suite build](evidence/m1/M12B_SEQUENCE_50/SUITES_BUILD_after_20261102_104620_015970/console.log), [manifest/frozen path](evidence/m1/M12B_SEQUENCE_50/BUILD_MANIFEST_after_20261102_104621_810634.json). Frozen compiled source hashes повторно проверены после runs. Поздние изменения только diagnostic workflow import, README/finalizer; [harness manifest](evidence/m1/M12B_SEQUENCE_50/HARNESS_MANIFEST_20261102_104928_505048.json) отделяет их от compiled inputs.

[Final verification](evidence/m1/M12B_SEQUENCE_50/FINAL_VERIFICATION.json):96 compiled input files и11 archived executables повторно сверены; SHA native consumer object `20f31493900a2540675edd545d0bd717f4a9043d30b02551d382f6557d0211c3`, canonical compiled-source commitment `e1548a2bc74ca9b79e876d561564591239e2480010a7aa76f2aced478f0bd84b`. Там же library hashes, final harness hashes и повторный integrity после обновления документов:3634 checked, unexpected0.

| Объект | SHA256 |
|---|---|
|enet_worker.cpp|`bbb6198cd1aead7cd555844cdbe90900781d2c694ef9f4f3330204869d0a7bc1`|
|enet_worker.h|`2b88a45855111c68b5027ae7c7d2aea2ec663ce65735d07b50e121a718440932`|
|input_sequence.h|`f6fdcd8343078e3081d1382bb0acdb4540c725584a7534671c59f36bd6a1bbde`|
|network_input.cpp|`e7b605d9a92f1f1fff7398dae8fcfa55c6d4c16fa843738116ac15d3104b150d`|
|Before inspection executable|`9a7311b34de85f1d90d5d448dd7acababc39443c8661156803fea2c3bee0ceb6`|
|Final inspection executable|`d08310c927d0a0e7f420772db494c4be5d25b44b6c41b8bc1fb20c5ec0bf7a81`|
|Final normal threaded executable|`ba0fa35bd0663b427b9edf12e7e3322d278a467b7fbdc3eadb25a79e8af541c5`|
|Unchanged DOC49 scheduler|`aa72b5394456e988eb52c5625245fec240fdcec86d96512ff90255ebf03a524f`|
|Unchanged timeout header|`a996b15c4203b87f46a98584f0ca1af322fd3382b2ce9f4a033b8856e5766349`|

[Integrity](evidence/m1/M12B_SEQUENCE_50/INTEGRITY_20261102_104846_779731.json):3634 baseline files checked, ровно3 разрешённых existing source changes, unexpected0. Новый header и isolated lab/docs/Evidence добавлены отдельно. Historical docs/Evidence/accepted binaries/Gemini stand, wire serializers v1.10, ThreadBridge/DTO, admission allocator/host policy, native hooks/ABI/lifecycle unchanged. Original77/copy77 совпадают с preparation/no alias; полный original snapshot и оба protected profiles unchanged. Target EXE SHA256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`, проверен hash comparison существующим read-only game_check; KEX не запускался. [Final OS](evidence/m1/M12B_SEQUENCE_50/OS_FINAL.json): game/test processes0, проверяемые UDP endpoints0. Новых runtime diagnostic sessions0. Новая игровая DLL не собиралась/не заменялась; исторические DLL сами по себе этот patch не содержат.

## 8. Ограничения и остановка

Пределы ограничивают одиночные excessive jumps, а не реализуют rate limiting или восстановление произвольной последовательности вредоносного authenticated отправителя. Внутри окна он всё ещё может продвинуть свой high-water mark и заставить меньшие номера ждать/отбрасываться; initial budget10800 намеренно шире для совместимости принятого reconnect sender. Проверка не доказывает честность counter/clientTick. Случайный MAX больше не создаёт lifetime poisoning обычной generation; ошибочный большой пакет не вызывает disconnect/KICK.

Длительный INPUT-only blackout с более1800 пропущенными incremented commands требует новой generation; initial counter выше10800 fail-closed даже после reconnect. Unlimited future senders с сохраняемым сквозь reconnect counter не покрыты initial budget: им потребуется низкий per-generation counter. Сам текущий180s sender и его accepted reconnect не менялись. Такие ограничения зафиксированы явно, wire v1.10 не расширен.

Физический LAN/Radmin VPN, KEX application этого нового patch, arbitrary flood/все schedules/месяцы непрерывной работы — UNKNOWN. Compilation native consumer не является новой runtime приёмкой. M0/M1.1 не повторялись; P2-2 остаётся прежним PARTIAL, другие audit findings не закрываются этим документом.

**Вердикт: PASS исключительно P2-5 в описанной policy и тестируемом scope.** PROGRESS/EVIDENCE_INDEX обновлены. После этого блока остановиться: без второй игровой DLL, M1.2b integration, 3–8 клиентов, достижений или world replication.
