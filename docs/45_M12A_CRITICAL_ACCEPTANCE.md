# M1.2a — Critical Acceptance Audit

Дата: 2026-11-02. Независимый аудит результата [DOC-44](44_M1_FIRST_NETWORK_INPUT.md) (Sol, PASS в ограниченном scope). Код не изменялся, игра не запускалась, M1.2b не начат. Отчёты Sol/Gemini не принимались как доказательство сами по себе: ключевые утверждения перепроверены по исходникам и сырым journals собственным скриптом.

**Вердикт: CONDITIONAL** (обоснование в §6).

Метки: **VERIFIED** — runtime-наблюдение в сохранённом Evidence, перепроверенное здесь; **CODE-VERIFIED** — статически по замороженному исходнику; **REPRODUCED** — автономное воспроизведение этого аудита без игры; **INFERRED** — вывод; **UNKNOWN** — не установлено.

## 1. Что проверено

- Исходники: [network_input.cpp](../experiments/m1/network_input/src/network_input.cpp), [network_input.hpp](../experiments/m1/network_input/src/network_input.hpp), [bootstrap.cpp](../experiments/m1/native_bootstrap/src/bootstrap.cpp), [enet_worker.cpp](../tools/enet-prototype/src/enet_worker.cpp), а также затронутые пути `enet_host.cpp`, `simulation.cpp` (реестр слотов), `thread_bridge.cpp`, `thread_bridge_dto.h`, ENet `win32.c`, тесты M1.2a и `native_client.cpp`.
- Совпадение с замороженной сборкой: `final_sources/` = текущие `src` (diff пуст); `enet_worker_before.cpp` → текущий отличается ровно 8 строками 577–584. Изменения `bootstrap.cpp` вне `#ifdef PSE_M12A` — только рефакторинг `before == after` без смены семантики.
- Journals: [run01](evidence/m1/M12A_NATIVE_01/native.jsonl) (4692 записи) и [Direct Quit](evidence/m1/M12A_DIRECT_QUIT_01/native.jsonl) (2122) сопоставлены с `client.jsonl` собственным [скриптом](evidence/m1/M12A_AUDIT_45/journal_crosscheck.py) ([run01](evidence/m1/M12A_AUDIT_45/crosscheck_M12A_NATIVE_01.log), [DQ](evidence/m1/M12A_AUDIT_45/crosscheck_M12A_DIRECT_QUIT_01.log)).
- Два автономных воспроизведения на неизменённых Session/Worker/Bridge, собранных в scratch-каталоге: [repro.cpp](evidence/m1/M12A_AUDIT_45/repro.cpp), логи [R1](evidence/m1/M12A_AUDIT_45/R1_stale_kick.log), [R2](evidence/m1/M12A_AUDIT_45/R2_orphan_slot.log). KEX/игра не использовались.
- Инструменты Gemini (`m12a-test-client`, `m12a-adversarial-qa`, `multiclient-test-runner`) в доказательную базу не включены.

## 2. Результаты по областям

### A. Session generation и безопасность ввода

| Вопрос | Статус | Основание |
|---|---|---|
| Исправление generation laundering корректно | **CODE-VERIFIED** | `enet_worker.cpp:580-583`: `cmd.connectionGeneration != gen` отклоняется до `resetMailbox`/`lastSeq`/edges (585–606). Генерация `gen` берётся из реестра пира (`resolvePeerSlot` → `getPeerSession`), не из wire. Равенство закрывает и старшую, и младшую чужую генерацию. До/после: [before](evidence/m1/M12A_IMPLEMENTATION_20261102/generation_before.log) — gen11/seq900 доставлен как gen1; [after](evidence/m1/M12A_IMPLEMENTATION_20261102/generation_after.log) — 0 forwarded, valid seq1 доставлен. |
| Проверка до мутации на стороне Session | **CODE-VERIFIED** | `network_input.cpp:100-107`: `valid` (slot, active/ready/levelReady, gen, seq>last, isfinite) вычисляется до изменения `lastSeq/buttons`. |
| Stale input после disconnect/reconnect | **CODE-VERIFIED + VERIFIED** | CLIENT_LEFT → `neutral`, `active=false` (89–92); новая генерация → `hasSeq=false, ready=false` (81–85); worker сбрасывает mailbox в auth/ended callbacks (`enet_worker.cpp:92,105`). Run01: tick 6278 disconnect 4→0; ticks 6278–6297 все updates `native_buttons=0`; gen2 вводы приняты только после `network_ready` (tick 6297). |
| ABA / wrap | **INFERRED: нет практического риска** | Генерация слота монотонна (`simulation.cpp:139`, u32). Самоотравление собственной генерации `seq=UINT32_MAX` возможно, но затрагивает только свой слот (P2-5). |
| Неверная привязка slot | **P1-1** | Ввод привязан к (slot, gen) корректно. Но исходящие KICK/MapChange/Baseline и входящий MAP_READY адресуются только slot — см. §3. |
| Не более одного movement update за tick | **CODE-VERIFIED + VERIFIED** | `bootstrap.cpp:184` (`p2Tick == ticks` → block), P2 Update только из P1 Update при `localThisTick==1` (172–173). Run01: 1475 уникальных ticks, `commands_this_update==1` везде; 124 тика с 2–5 принятыми командами дали по одному update. DQ: 582, аналогично. |

### B. Thread Bridge и KEX

| Вопрос | Статус | Основание |
|---|---|---|
| KEX вызывает только simulation thread | **CODE-VERIFIED + VERIFIED** | Все KEX-вызовы в `construct`/`unbind`/хуках с `thread()` (`bootstrap.cpp:69,125,233,269,330`). Worker не получает ни `log`, ни `g`, ни KEX-указателей. Journals: все runtime-записи на TID 13568 / 37536; две записи на других TID — loader (`dll_prepared`, `bootstrap_queued`). |
| SPSC/affinity в Release | **CODE-VERIFIED** | `m_threadChecksEnabled{true}` (`thread_bridge.h:182`), никто не выключает; handler по умолчанию `abort` (`thread_bridge.cpp:19-28`). Session регистрирует SIMULATION на attested TID (`network_input.cpp:13`), `tick`/`stop` проверяют роль (120, 134). Поле `"thread_checks":true` в журнале — литерал (строка 31), не runtime-запрос (P2-3). |
| Гонки Worker / Session / native callbacks | **CODE-VERIFIED: не найдено** | Общих данных кроме SPSC-очередей и атомиков нет. ENet host создаётся на simulation thread в `startHost` до `std::thread` (happens-before), уничтожается на worker (`finalizeOnStop`). Переключение Getter-хука в `construct()` при живом worker: MinHook приостанавливает и worker, который не исполняет код KEX — **INFERRED безопасно**. |
| Скрытый второй authoritative path | **CODE-VERIFIED: нет** | `bridgeMode` принудительно (`enet_worker.cpp:32`); `ENetHostServer::poll` не тикает `WorldSimulation` в bridgeMode (`enet_host.cpp:115-117`). Legacy `WorldSimulation` остаётся только реестром слотов/ключей (allocate/disconnect) и источником `PLAYER_JOIN` broadcast — данных KEX там нет. |

### C. Lifetime и shutdown

| Вопрос | Статус | Основание |
|---|---|---|
| Порядок stop → join → restore → Reset → StateExit/engine cleanup | **CODE-VERIFIED + VERIFIED** | `unbind`: `network->stop` (236) → обнуление команд (237–240) → `restoreControls` (242) → Reset (248). Run01 seq 4668–4682: entry → neutral → stop_begin → joined (5 мс) → retired → 4× exact restore → Reset `handle_null:true, p1_unchanged:true` → owned dtor → StateExit return. DQ seq 2107–2122: shutdown_entry → joined → restore → Reset → quiesced → engine cleanup → final. |
| Join реально завершён | **VERIFIED** | Run01: [os_after_state_exit](evidence/m1/M12A_NATIVE_01/os_after_state_exit.json) — TID 15852 отсутствует в живом процессе, UDP endpoint отсутствует. DQ: `WaitForSingleObjectEx(0x1774)` на TID 38452 result 0, exit code 0; closesocket на worker TID ([RESULT_SUMMARY](evidence/m1/M12A_DIRECT_QUIT_01/RESULT_SUMMARY.json)). |
| Deadlock при join | **CODE-VERIFIED: ограничен** | Worker не берёт `m_lifecycleMutex`; ожидание дренажа в `finalizeOnStop` ограничено 250 мс (`enet_worker.h:63`), затем `RESYNC_INBOUND_LOSS`. Худший случай — задержка выхода ≤ ~250 мс. |
| UAF / callbacks после shutdown | **CODE-VERIFIED: не найдено** | Session/Bridge/Worker никогда не удаляются (`bootstrap.cpp:56`, pointer в pinned `g`); ENet объекты освобождены до возврата `join`; `Session::tick` после stop — no-op (`running()`). Повторный `unbind` при title Quit: stop no-op, restore идемпотентен, `unbind_already_empty` (run01 seq 4688). |
| Контракты M0/M1.1 | **VERIFIED** | Один Spawn, один Reset на выход, exact16 restore, pinned module/storage, `ticks_after_shutdown 0`, `foreign_calls 0` в обоих runs. |
| Иные пути выхода (закрытие окна, crash) | **UNKNOWN** | Проходят ли они через Shutdown hook — не проверено; если нет, `ExitProcess` застанет живой worker. DOC-44 исключает crash exits, но не упоминает закрытие окна. |

### D. Надёжность транспорта

| Вопрос | Статус | Основание |
|---|---|---|
| Outbox retry / overflow / resync | **CODE-VERIFIED** | FIFO до `isSuccess` (`network_input.cpp:43-48`); bound 8192 → `requestResync(OUTBOUND_LOSS)` → `recover` (34–41, 129); inbound resync → `recover` (123). Pressure test: baseline удержан и доставлен ровно один раз ([log](evidence/m1/M12A_IMPLEMENTATION_20261102/pressure_test_01.log)). |
| Disconnect/timeout не оставляет P2 с активным вводом | **CODE-VERIFIED + VERIFIED** | Три независимых пути обнуления: CLIENT_LEFT (89–92), timeout 6 ticks/150 мс (127), `!gameplay` (127). Run01: timeout ровно через 6 тиков (accept tick 6230 → neutral 6236), дальнейшее движение — затухающая native инерция 12.03→0.85 за 30 тиков без W. |
| Согласованность клиента и хоста после reconnect | **P1-1 (REPRODUCED)** | Устаревший KICK обрывает новую генерацию; MapChange/Baseline старой генерации может уйти новой. |
| Слоты после leave | **P2-1 (REPRODUCED)** | Чужой participant занимает единственный peer навсегда; P2 не может вернуться. |

### E. Достоверность приёмки DOC-44

| Утверждение | Статус | Комментарий |
|---|---|---|
| Сеть → native P2 movement | **VERIFIED** | 1629/1629 принятых `(gen, seq, forward)` найдены в client sends (0 расхождений, 0 дублей); 1475/1475 updates ссылаются на последнюю принятую команду (2 исключения — ticks 6296–6297 сразу после RECONNECTED, `seq 0`, ожидаемо); `native_buttons` = отображение forward. Первые 900 neutral updates — путь 0.0. Hold1 41.994961 и release 6.859424 XY воспроизведены точно; hold2 — 128.62 path (DOC: 127.10 displacement, совместимо). Ограничение: оба journal — самоинструментация harness Sol; OS socket evidence подтверждает реальный трафик. |
| Независимость P1 | **VERIFIED для сравниваемых полей** | Все 2057 updates: `unchanged:true` и строковое равенство `p1_before==p1_after`; ненулевой P1 input в ticks 6388–6392. **Позиция/скорость P1 в сравнение не входят** (`LocalState`, `bootstrap.cpp:34-41`), а P2 спавнится в точке P1 и физически с ним контактирует. Значение 88.2341 XY движения P1 этим аудитом не пересчитывалось. |
| Нет дублированного Spawn | **VERIFIED + CODE-VERIFIED** | Ровно один `spawn` attempt в каждом run; `construct()` достижим только из `Phase::Armed`, после него — `Live`/`Blocked` без возврата. |
| Корректный join worker | **VERIFIED** | См. C. |
| Lifetime при двух способах выхода | **VERIFIED** | См. C. |
| «Session transport test: terminal resync/reconnect» | **Завышено (P2-3)** | [adapter_test_01](evidence/m1/M12A_IMPLEMENTATION_20261102/adapter_test_01.log) доходит до resync → KICK → CLIENT_LEFT; reconnect в тесте отсутствует. Reconnect покрыт только native run01 и только путём «graceful leave → rejoin». |

## 3. Найденные проблемы

P0 не найдено: ни один механизм ниже не приводит к применению чужого/устаревшего ввода к P2, вызову KEX вне simulation thread, UAF или нарушению native lifetime.

### P1-1. Исходящие control/baseline/map сообщения адресуются только slot, без connectionGeneration

- **Где:** `tools/enet-prototype/include/thread_bridge_dto.h:307-312` (`BridgeMapChange::targetSlot`), `:441-444` (`BridgeWorldBaseline::targetSlot`), `:482-490` (`BridgeControl::slotId`), `:330-333` (`BridgeMapReady` без generation); `tools/enet-prototype/src/enet_worker.cpp:497-512, 531-545, 546-550` (`getPeerForSlot`/`kickSlot` по текущему пиру слота); `experiments/m1/network_input/src/network_input.cpp:62-75` (`Session::recover` всегда `KICK slot 1`), `:93-98` (MAP_READY без проверки генерации), `:112-116` (Respawn/Interaction любого slot вызывают recover и KICK slot 1).
- **Механизм:** Session ставит KICK (или MapChange/Baseline) для генерации N. Если до того, как worker его выполнит, слот переходит к генерации N+1, worker выполняет его для **текущего** пира слота, т.е. для N+1. Реалистичный триггер без тестовых хуков: worker сам кикает слот при переполнении pending (`checkPendingBound`, `enet_worker.cpp:394-398`) и одновременно поднимает `RESYNC_INBOUND_LOSS`; Session реагирует своим KICK на следующем тике (до ~16 мс), а клиент на loopback/LAN успевает переподключиться за несколько RTT. Аналогично любая задержка outbound (backpressure, которую M1.2b с реальными baseline/snapshot сделает обычной).
- **Доказательства:** **REPRODUCED** ([R1](evidence/m1/M12A_AUDIT_45/R1_stale_kick.log)), замороженные Session/Worker: gen1 ready → `requestResync` → `recover` ставит KICK; gen1 уходит; gen2 переподключается по ключу (`network_session type 2, generation 2`); после снятия паузы drain — `CLIENT_LEFT generation 2`, клиент gen2 отключён, хотя для gen2 recover не вызывался. Пауза drain (`setOutboundDrainPaused`) лишь детерминирует окно гонки.
- **Последствия в M1.2a:** лишний обрыв новой сессии и повторный reconnect; ввод при этом нейтрален — безопасность P2 не нарушена, поэтому не P0. Для M1.2b (реальная map identity, baseline как замена мира) baseline/MapChange, адресованные генерации N, будут применены генерацией N+1 — это прямой дефект согласованности.
- **Минимальное исправление:** добавить `connectionGeneration` во внутренние (несериализуемые) `BridgeControl`, `BridgeMapChange` (unicast), `BridgeWorldBaseline` (unicast) и в `BridgeMapReady` (заполнять из `resolvePeerSlot`); в worker отбрасывать unicast-сообщение, если `!m_slotActive[slot] || m_slotGen[slot] != gen` (считать в `outboundNoPeer`). В Session проверять генерацию MAP_READY и в `recover` для Respawn/Interaction использовать slot/gen запроса. Wire v1.10 не меняется. Добавить R1 как регрессионный тест.

### P2-1. Participant не-slot-1 после leave занимает единственный peer; P2 не может вернуться

- **Где:** `tools/enet-prototype/src/simulation.cpp:120-158` (participantId слота сохраняется, новый participant получает следующий свободный slot), `:165-171` (`disconnectSlot` не освобождает participantId); `experiments/m1/network_input/src/network_input.cpp:17` (`maxClients=1`), `:79` (события slot≠1 игнорируются, таймаут готовности и kick к ним не применяются).
- **Механизм:** P2 (`alpha`) уходит штатно; другой participant (`beta`) с верным join token получает slot 2. Session его игнорирует, но не отключает; ENet peer limit 1 занят. `alpha` с валидным reconnect key больше не может подключиться до перезапуска хоста.
- **Доказательства:** **REPRODUCED** ([R2](evidence/m1/M12A_AUDIT_45/R2_orphan_slot.log)): `beta: slot=2 … still_connected=1`, после 700 тиков `wantsPlayer=0`; `alpha rejoin with valid key: authenticated=0`.
- **Почему P2:** DOC-44 явно исключает «новых participant/slots после leave»; хост привязан к 127.0.0.1 и требует token. Становится P1, как только bind выйдет за loopback (M1.2b).
- **Минимальное исправление:** в Session немедленно KICK любой slot, который не обслуживается (или в worker для M1.2a ограничить admission slot 1); в M1.2b — явная admission/rejoin policy (DOC-44 §задачи 1).

### P2-2. Запас input timeout мал относительно реальной частоты команд

- **Где:** `experiments/m1/network_input/src/network_input.cpp:127` (6 ticks / 150 мс); `experiments/m1/network_input/src/native_client.cpp:60` (`next=now+16` при разрешении `GetTickCount64` ≈15.6 мс).
- **Механизм/доказательства:** **VERIFIED** — интервалы между принятыми командами в тиках: run01 `{0:127, 1:604, 2:857, 3:37}`, DQ `{0:73, 1:327, 2:476, 3:22, 4:1}`; при ~59.9 Hz симуляции клиент фактически шлёт ~30 Hz, а не 60. На loopback запас до нейтрали — 3–4 тика; на LAN/Radmin VPN 2–3 потерянных unreliable пакета или джиттер >~70 мс дадут прерывистый hold (нейтраль → повтор W). Безопасность не страдает.
- **Исправление:** до M1.2b — измерить cadence/jitter на реальном канале и задать timeout от них (или по client tick), исправить клиентский таймер (`QueryPerformanceCounter`/`steady_clock`).

### P2-3. Неточности доказательной базы DOC-44

- **Где/что:** (а) adapter test не выполняет reconnect, хотя DOC-44 так заявляет (§5 таблица); (б) `"thread_checks":true` — литерал в `network_input.cpp:31`, не значение `areThreadChecksEnabled()`; (в) reconnect в runtime покрыт только путём graceful leave → rejoin; путь «вытеснение ghost-пира» (`enet_host.cpp:285-291`, RECONNECTED без CLIENT_LEFT) не исполнялся, а при `maxClients=1` он вообще недостижим до ENet timeout старого пира; (г) независимость P1 не включает позицию/скорость P1.
- **Доказательства:** логи и строки выше; выводы DOC-44 по существу это не опровергает.
- **Исправление:** не переписывать DOC-44 (по заданию); учесть в описании M1.2b и в следующих тестах: логировать реальное `areThreadChecksEnabled()`, добавить reconnect в transport test и путь ghost-вытеснения при `maxClients≥2`, добавить позицию P1 в сравнение.

### P2-4. Диагностика возмущает тайминг native simulation

- **Где:** `bootstrap.cpp:85` (`FlushFileBuffers` на каждую запись, ≥3 записи/тик плюс каждый принятый/отклонённый ввод — на simulation thread); ENet `win32.c:32` (`timeBeginPeriod(1)` при `enet_initialize`, вызываемом из `startHost` на simulation thread) и `:40` (`timeEndPeriod` при stop).
- **Механизм:** синхронный flush на игровом потоке и смена глобального разрешения таймера процесса на время сети. Корректность приёмки не затрагивает (**INFERRED**), но тайминг-метрики (frame time, jitter, wall-clock timeout) из этих runs нельзя переносить на production; поток отклонённых вводов (например, в меню) напрямую нагружает simulation thread flush-ами.
- **Исправление:** для M1.2b — буферизованный журнал с flush вне тика или rate-limit отклонений; явно зафиксировать влияние `timeBeginPeriod`.

### P2-5. Наблюдаемость и hardening ввода

- **Где:** `enet_worker.cpp:580-583` считает generation mismatch в `inputsRejectedForeign`, комментарий `enet_worker.h:35` описывает его как slot mismatch; `enet_worker.cpp:589-594` и `network_input.cpp:101` — один `seq=UINT32_MAX` блокирует остаток собственной генерации.
- **Последствия:** только собственный слот / диагностика; соответствует пункту 3 задач DOC-44.
- **Исправление:** отдельный счётчик `inputsRejectedGeneration`; окно допустимого скачка sequence или сброс по таймауту — в рамках host acceptance hardening.

## 4. Проверено и дефектов не найдено

8-строчный fix Worker; порядок проверок в Session; один update/Commands на тик; отсутствие KEX/логгера в worker; принудительная affinity в Release с `abort`; отсутствие второго authoritative пути; порядок unbind и реальный join в обоих выходах; ограниченность ожидания при join; удержание Session/Bridge/trampolines до process reclaim; идемпотентный повторный unbind; один Spawn; native timeout/disconnect нейтраль и затухающая native инерция без ручной коррекции.

## 5. Остаточные UNKNOWN

- Пути выхода, минующие Shutdown hook (закрытие окна, crash) при живом worker.
- Поведение при малициозном flood: `ENetHostServer::poll` крутит `enet_host_service` пока есть события (`enet_host.cpp:95`), что может откладывать drain outbound; вне scope M1.2a.
- Значение 88.2341 XY движения P1 — не пересчитано.

## 6. Вердикт

**CONDITIONAL.** Нативная часть M1.2a (реальный P2, управляемый сетевым вводом на simulation thread, независимость сравниваемого состояния P1, один Spawn, корректный join и lifetime на обоих выходах) подтверждена исходниками и сырыми journals; P0 нет. M1.2b можно начинать при условиях:

1. P1-1 исправляется **первым шагом** M1.2b, до реальной публикации map identity/baseline, с регрессионным тестом по сценарию R1.
2. Пока не реализована admission policy (P2-1), host остаётся bind 127.0.0.1 + join token; выход за loopback — только после неё.
3. Timeout ввода и cadence клиента (P2-2) калибруются на реальном канале до любых выводов о качестве управления по LAN/VPN.
4. Тайминг-метрики runs M1.2a не используются как production-данные (P2-4).

## 7. Следующие действия (не более пяти)

1. Добавить `connectionGeneration` во внутреннюю адресацию KICK/MapChange/Baseline/MAP_READY, отбрасывать устаревшие unicast в worker; R1 — регрессионный тест.
2. Admission policy: немедленный KICK необслуживаемых slots, освобождение participantId по политике rejoin; R2 — регрессионный тест.
3. Измерить cadence/jitter клиента на Radmin VPN и задать timeout ввода от измерений; исправить таймер клиента.
4. Расширить transport/native тесты: настоящий reconnect в adapter test, ghost-вытеснение при `maxClients≥2`, позиция P1 в сравнении, runtime-значение thread checks в журнале.
5. Проверить, проходит ли закрытие окна через Shutdown hook (или добавить остановку worker на этом пути) до первого не-loopback сеанса.

Сохранённые материалы аудита: [docs/evidence/m1/M12A_AUDIT_45/](evidence/m1/M12A_AUDIT_45/). Новых игровых процессов не было; исходники проекта не изменялись.
