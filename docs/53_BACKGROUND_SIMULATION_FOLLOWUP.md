# DOC-53 follow-up — A1–A4 и gate к DOC-55

**FUNCTIONAL PASS; полный DOC-53 остаётся PARTIAL.** Исторические DOC-53 и Evidence сохранены. [Gate](evidence/m1/DOC53_FOLLOWUP/DOC55_GATE.json) разрешает только snapshot transport PoC. Новый опыт: Host40004/Sim31800, Client16008/Sim36376, exact EXE `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`.

## A1 — 44/46

VERIFIED SOURCE: ENetClientNode хранит local state в m_lastSnapshot. RespawnConfirm очищает m_respawnRequested, но не меняет snapshot. Поэтому подтверждённое Host возрождение временно сосуществует с DEAD view клиента. Test9 снова разрешает запрос по этой устаревшей view; Host возвращает REJECT_NOT_DEAD (2), сохраняя авторитетность и единственный respawn. Это не потеря подтверждения. Native bridge-mode dispatch этот non-bridge клиентский обработчик не использует.

Нижняя/равная snapshotSequence отбрасывается до m_lastSnapshot assignment; такой старый snapshot не перезаписывает уже принятую новую ALIVE snapshot. RespawnConfirm не является snapshot watermark: более поздно доставленный pre-respawn DEAD snapshot ещё может сохранять DEAD view до ALIVE. Патч Gemini, синтезирующий ALIVE из confirm, не применялся. Новые cross-channel ordering guarantees не заявляются.

Первый pristine запуск здесь46/46 без fixture. Две трассы фактического Windows ожидания показали host poll ~1–2ms/client sweep ~8–11ms и затем host poll ~15–16ms/client sweep ~107–109ms; оба pristine trace запуска прошли. Следовательно, timer resolution влияет на число обслуживаний, но сам по себе не доказан универсальной первопричиной исторического44/46. NtQueryTimerResolution — системное наблюдение, не доказательство эффективной per-process quantum. Никаких запросов изменения timer resolution не выполнялось.

Отдельная test-only задержка только snapshot550ms над настоящим ENet воспроизвела44/46: Confirm при DEAD, повторный request, Host rejection2, затем ALIVE. Это контролируемое воспроизведение механизма, не повтор исторических условий. Исправлен только harness: bounded predicate до2000ms ждёт matching slot/epoch/deathSequence confirmation и фактический ALIVE matching entityGeneration/health200/starter weapons+ammo. Исходные assertions сохранены и дополнены. После patch стандартная suite трижды46/46; с той же snapshot-delay fixture46/46. Production client/Host/transport не менялись.

## A2/A3 — настоящие положительные native inputs

Использованы stock action0/held+04 и Commands RVA1e34a0/Turning RVA1e2ad0 из сохранённых exact-build dossiers. Mouse queue-derived float fields+40/+44 отдельно от итоговых command deltas+38/+3c. VA=runtime base+RVA; фактическая base140000000. Read-only telemetry добавлена в уже существующий Update seam; новых KEX hooks нет. Статические dossiers: ghidra_2022/G_FUNC_1401dfe30.json, G_FUNC_1401e34a0.json, G_FUNC_1401e2ad0.json. Декомпиляция не является исходниками KEX.

SendInput Mouse Left дал27 положительных samples: buttons1, heldAttack1…27. SendInput relative mouse дал20 положительных samples; пример yaw0.0250000004/pitch0.00937500037, mouseX2/mouseY0.75. Keyboard turning не использовался. В обоих опытах затем реальная foreground передача Client, нулевые P1 команды, продолжающиеся P2 W/S, возврат Host и отсутствие replay. [Строгая сверка](evidence/m1/DOC53_FOLLOWUP/NATIVE_REVIEW_20261102_233900_442718.json).

K/Attack fixture не дал positive native counter. Строка bind k "attack" использует stock cfg grammar, но её фактическая загрузка UNKNOWN. Ошибка сохранена; этим тестом PASS не обосновывается. Штатный Mouse Left дал независимый положительный результат. Тестовая привязка удалена из disposable profile после exit; production bindings и пользовательские профили не менялись.

Один run: принятых INPUT 8020; native P2 Updates 8705, background 867. Все P1 comparisons совпали, unique P2 ticks/commands подтверждены, faults/affinity0. Оба retained OS process objects и duplicate handles signaled0/exit0; native worker join до restore/Reset/engine exit. Forced termination0. Все20 REA sessions закрыты.

## A4 — реальные регрессии и исправления harness

pse_test_suite46/46 ×3 после patch; stress11/11; fault16/16; bridge acceptance11/11; background37 checks; R1/R2; routing330/admission112; timeout default6ticks/150ms; sequence282 и live12; native-client3scenarios PASS. Exact argv, stdout, exits, binary hashes и неизменённые ошибки находятся в [Evidence](evidence/m1/DOC53_FOLLOWUP/).

Первый bridge acceptance дал10/11: A10 движение0.2 после фиксированных250ms. Controlled host180ms stall показал:250ms ещё не означает6 host ticks; нейтрализация наступила после шести исполнявшихся ticks, затем позиция стабильна. В harness добавлены client stop acknowledgement и bounded new-neutral-timeout wait;300ms strict stationary assertion сохранена. Следующий run A10 PASS, но A3 FAIL: два respawn подтверждены, retry request ещё не был отправлен (client requests2/duplicates0). Retry основан на60ticks, поэтому1300ms не является барьером. A3 теперь ждёт фактический новый Host duplicate counter до5000ms; итоговые idempotency/generation assertions сохранены. Финальный run11/11, retries наблюдались после62/63 client ticks. Причина конкретного исходного A10 schedule не измерена; controlled stall не выдаётся за точную историческую причину. Mock/transport production sources не менялись.

## Изменения, ошибки и ограничения

Existing source changes: bootstrap.cpp (input telemetry only), test_suite.cpp (state waits), test_bridge_acceptance.cpp (event waits). [Diff](evidence/m1/DOC53_FOLLOWUP/SOURCE_CHANGES.patch), [90 source hashes / DLL hashes](evidence/m1/DOC53_FOLLOWUP/BUILD_MANIFEST.json), [baseline13033 files](evidence/m1/DOC53_FOLLOWUP/BASELINE.json), final integrity/gate. Host DLL20470c249d3af8a234a0be649f63b878d63f14a0f44b907cc57dc50e4ef1f12c; Client retained4887fdda…def3. Initial build used top-level suite target unavailable under EXCLUDE_FROM_ALL; corrected nested transport path. One Python inspection typo, early bind before launch.json, premature client UI helper и PENDING gameplay gate сохранены как harness errors. Никаких повторных game launches или скрытых retries с прежними условиями.

Оригинальная установка77/copies77/full original/protected snapshots совпали; isolation guards errors/overflow0, успешных external write-opens0. Guard не является полным OS sandbox. Native Steam init failure сохранён; achievements/licensing не менялись. Focus-byte lease+319, original pause gates, READY/generation, ENet/input sequence/timeout unchanged.

UNKNOWN: старый terminal hang; полная native loading/reconnect/cutscene/minimized/world-exit матрица,8players; exact Esc pause predicate; почему private K binding не активировался. Эти ограничения не являются обнаруженным blocker для transport-only DOC55. Visual proxy, camera/world/combat/campaign replication не начинались.
