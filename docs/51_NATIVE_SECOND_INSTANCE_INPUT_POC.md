# Native Second-Instance INPUT PoC — PARTIAL

2026-11-02. **Впервые наблюдена цепочка из второго настоящего KEX-процесса: native W/S sample → ThreadBridge → ENet UDP → Host Session → native P2 Update/Puppet movement. Общий PASS задания не присвоен: штатное OS-завершение хоста не прошло.** Клиент завершился штатно; хост потребовал внешнего OS cleanup после завершённых Worker join, native Reset и engine cleanup. Причина terminal hang **UNKNOWN**. Повторных основных native runs после этого не было.

[Итоговая машинная сверка](evidence/m1/NATIVE_SECOND_INSTANCE_51/RESULT_SUMMARY_FINAL.json), [исходник сверки](../experiments/m1/native_second_instance/review.py), [raw Host journal](evidence/m1/N51_HOST_01/native.jsonl), [raw Client journal](evidence/m1/N51_CLIENT_MAIN_01/native.jsonl), [Host OS trace](evidence/m1/N51_HOST_01/STATUS_EXIT.json), [Client OS trace](evidence/m1/N51_CLIENT_MAIN_01/STATUS_FINAL.json). Сверка проверяет согласованность одного опыта; это не новый набор игровых прогонов.

Это input generator внутри настоящей игры. Мир клиента независим от мира хоста; world replication, multiplayer camera, combat/campaign и полноценный кооператив не реализованы. Wire v1.10, DOC-50 predicate, timeout `6 ticks || 150 ms`, host admission, native P2 lifecycle и control hooks сохранены. После отчёта работа остановлена.

## 1. Критерии задания

| Критерий | Фактический результат |
|---|---|
| Два настоящих KEX-процесса | Да: Host PID34728, Client PID27724; независимые paths/CWD/profiles и OS identities |
| Свежие согласованные DLL | Да: новая Host DLL с DOC-46/47/49/50 и новая Client DLL; hashes ниже |
| Native input tap | Да: post-original Update, held forward/back из штатного Player, TID9668 |
| ENet handshake | Да: authoritative slot1/gen1, INPUT control-domain ready, empty baseline |
| slot/generation/sequence | Да: gen1, INPUT1…11855; Worker ingress rejects foreign/stale/gen/jump =0 |
| Отправка из игрового Client B | 11855 local dispatch и **11855 successful synchronous WSASendTo INPUT returns** с PID27724/TID1148 |
| Принятие Host Session | 11855 accepted; каждый связан с dispatch и OS UDP по `(slot,generation,input_seq)` |
| Настоящее движение P2 | 4563 native updates/Commands/Puppet ticks; отдельный Actor/Player; W и S реально перемещали P2 |
| Hold → Release → Neutral | W78 updates, затем108 neutral; S78, затем2025 neutral; последняя30-sample XY step каждого release interval =0 |
| P1 independence | 4563 равных before/after сравнений; graph/input/inventory/weapon/health сохранены; P1 position неизменна, health200 |
| Native faults / affinity | Зарегистрированных faults/blocked/affinity violations0; это не доказательство причины terminal hang |
| Штатное завершение обоих | **НЕ ПРОЙДЕНО:** Client normal exit0; Host exit-code query0 при unsignaled process wait, затем внешний TerminateProcess |
| Оригинал / protected profiles | PASS: original77, host copy77, client copy77, полный original snapshot, защищённые профили неизменны |
| Evidence / manifests | Сохранены raw journals, OS traces, frozen sources/DLL/PDB, failures, regressions, config и integrity manifests |

**Ограничение текущего двухоконного стенда:** одного `cl_engineNoSleep 1` недостаточно. Пока Client был foreground, Host принимал сеть, но штатный Player Update пропускался. Для bounded proof на том же запуске потребовались синтетические `WM_ACTIVATEAPP/WM_ACTIVATE/WM_SETFOCUS` только проверенному HWND хоста. Client при вводе оставался actual foreground. Это явная Windows test fixture, не готовая политика фоновой активности мода.

## 2. Проверка Gemini handoff

Проверены именно влияющие на реализацию утверждения; общая кампания M0/KEX reverse не повторялась. [Native contract review](evidence/m1/NATIVE_SECOND_INSTANCE_51/NATIVE_CONTRACT_REVIEW.md), [exact-file prefixes/assembly audit](evidence/m1/NATIVE_SECOND_INSTANCE_51/NATIVE_CONTRACT_AUDIT.json), [QA review](evidence/m1/NATIVE_SECOND_INSTANCE_51/QA_REVIEW.md), [adversarial QA results](evidence/m1/NATIVE_SECOND_INSTANCE_51/QA_ADVERSARIAL_RESULTS.json).

- `primary()` — inline Player по **runtime base+0xaf33e0**, instance — base+0xaf33a0. Preferred VA не используется как фиксированный runtime pointer. Сохранённые exact-build assembly и accepted adapter имеют приоритет над текстом Gemini.
- Post-Update подходит для W/S, но ровно один Update на каждый Tick не гарантирован. Tick содержит дополнительные skip branches; отсутствие Update сбрасывает sample. Сохранены thread/inTick/duplicate/graph/gameplay/alive gates.
- Утверждение о привязке Update к render/VSync не доказано. В опыте samples/active native updates около60Hz; Worker независимо выдаёт60.0027Hz. Config `cl_engineFPS 180` — только настройка, фактический render/present cadence **UNKNOWN**, не источник сетевого такта.
- `-userpath` и env не изолируют все ранние записи. Переиспользованы прежние KnownFolder/early-path redirects и bounded native write-open guard. Отдельные CWD, AppID files, saves/config/logs/auxiliary paths обязательны.
- Отсутствие CreateMutex imports не доказательство двойного запуска. Реальное одновременное существование двух EXE/окон/рендерящих локальных миров подтверждено OS и captures.
- `steam_appid.txt` — прежний разрешённый development-launch механизм. Оба процесса записали штатное `Unable to initialize Steam`, как accepted DOC-49. Steam process27668 наблюдался; успешный Steam IPC/online context/проверка лицензии этим опытом **не доказаны**. Steam binaries/API/checks не патчились, fake success и DRM обход не выполнялись. [Наблюдение](evidence/m1/NATIVE_SECOND_INSTANCE_51/STEAM_OBSERVATION.json), stdout каждого run.
- CLIENT Worker существовал, но queued INPUT **перезаписывался текущими slot/gen**. Это реальный reconnect risk; добавлен identity guard до dispatch. READY уже использовал generation guard DOC-46.
- `M12A_INPUT_ONLY` — существующий диагностический ControlDomain, не название реальной KEX-карты. Его readiness допустима без утверждения world synchronization. Новый клиент проверяет этот domain/epoch1 и single empty baseline; неизвестный domain fail-closed.

Gemini offline suite повторно дал18/18 ожидаемых fixture outcomes. Однако fake client SHA, invented marker, missing shutdown и neutral-only native samples могли давать ложный PASS; допустимый coalesced Session sequence jump давал ложный FAIL. Analyzer не отличает enqueue от actual UDP и не доказывает OS/build origin. Исходные Gemini файлы и Evidence не редактировались. Их PASS не использовался как приёмка этой реализации.

## 3. Реализованная архитектура и минимальный diff

Новая изолированная реализация: [native_second_instance](../experiments/m1/native_second_instance/README.md), [client.cpp](../experiments/m1/native_second_instance/src/client.cpp), [CMake](../experiments/m1/native_second_instance/CMakeLists.txt).

```text
Client KEX simulation TID9668
  original Tick → original Player Update → checked passive W/S capture
  → BridgeClientInputSample (scalar DTO; one producer)
  → ThreadBridge outbound SPSC
Client ENet Worker TID1148
  → latest sample cache → fixed rational 60Hz → MsgInputCommand
  → existing ENetClientNode / unreliable sequenced v1.10
Host Worker TID6252
  → existing authenticated slot/gen/DOC50 ingress → coalesced Bridge DTO
Host simulation TID3964
  → existing Session → accepted native P2 controller/Commands/Puppet traversal
```

KEX StateTick остаётся **единственным SIMULATION owner** Bridge по DOC-40. Отдельного scheduler thread с этой ролью нет. Worker не читает KEX memory и не получает Player/Actor pointers. Client не создаёт GuardedPlayer/P2, не вызывает Factory/Reset/weapon functions, не меняет Player memory и не читает `client_control.txt`.

Изменены только три existing shared files: `enet_worker.h`, `enet_worker.cpp`, `thread_bridge_dto.h`. [Полный diff](evidence/m1/NATIVE_SECOND_INSTANCE_51/SOURCE_CHANGES.patch). Добавлены:

1. Внутренний best-effort `BridgeClientInputSample`: stamped slot/gen, ordinal, sample steady time, simulation tick, valid flag, forward scalar. Wire format не меняется.
2. **Default-OFF** CLIENT-only latest-sample cadence; existing HOST/default loop сохраняется. Fixed epoch rational grid, high-resolution waitable timer/fallback blocking wait, ENet service ≤4ms, один command за due slot, просроченные slots пропускаются. Нет пачки catch-up команд или busy-spin.
3. Fresh sample age≤100ms, matching slot/gen, increasing ordinal, finite scalar −1/0/+1, sample после matching READY. Invalid/missing/stale sample даёт повторную Neutral на60Hz. W+S вместе —0. Wire buttons/other axes/angles нейтральны.
4. Queued INPUT identity guard вместо laundering; opt-in RECONNECT guard; receive-context generation у client MapChange/Baseline для отказа stale control. Host ingress/DOC-50 и Session executable predicates не ослаблены.
5. Необязательный callback **local dispatch**, явно не называемый UDP proof. Фактический UDP наблюдается отдельно на Windows API.

Новый client journal buffered; flush примерно раз500ms и на lifecycle. Старый Host per-record FlushFileBuffers сохранён как существующее ограничение. Для bounded diagnostic run сохранены полные sample/command identities; long-term logging/soak не приняты.

## 4. Input hook / native lifetime contract

EXE SHA256 для всех координат: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`. Имена ролей аналитические. ABI: принятые Win64 this-in-RCX `void(void*)` detours, Final — прежний no-argument/noreturn path. Скрытые альтернативные callers/exception exits этим опытом не исследовались. Псевдокод декомпилятора не является исходником.

| Client seam | Preferred VA | RVA | File offset |
|---|---|---|---|
| Tick |0x1401b6ec0|0x1b6ec0|0x1b62c0|
| Player Update |0x1401dfe30|0x1dfe30|0x1df230|
| StateExit |0x1401b6670|0x1b6670|0x1b5a70|
| Platform Shutdown |0x1400fcc20|0xfcc20|0xfc020|
| Engine Shutdown |0x1400a4c90|0xa4c90|0xa4090|
| Native FinalExit |0x1400fc580|0xfc580|0xfb980|

[Coordinate/source Evidence](evidence/m1/NATIVE_SECOND_INSTANCE_51/NATIVE_CONTRACT_AUDIT.json), [accepted native coordinates](evidence/m1/M11_IMPLEMENTATION_20261101/NATIVE_COORDINATES.json), saved `ghidra_2022/G_FUNC_1401b6ec0.json` / `ghidra_2022/G_FUNC_1401dfe30.json`. Точки вычисляются от фактического module base; exact16 checked до установки hooks. Новый KEX hook point не добавлен.

После original Update проверяются primary identity, attested TID/inTick, единственный localThisTick, instance state3/pending0, live reciprocal `Player+d8 → node+28 → Actor`, `Actor+10=node`, `Actor+1b8=Player`, Puppet vtable base+52d198, positive health и отсутствие dead bit. Только затем читаются buttons+0, forward held+0c, back held+10. На каждом Tick capture сначала invalid; если Update не состоялся, в сеть идёт invalid/neutral. Дополнительно actual foreground PID должен быть клиентом.

DLL проверяет exact path/SHA, pin через FROM_ADDRESS|PIN, CREATE_NEW journal и same-process primary-thread APC. DllMain только сохраняет HMODULE. Ни KEX operations, ни join в loader lock нет. StateExit/Shutdown останавливают Worker до restore Update и original engine cleanup. Native Player destructor/Reset клиент не вызывает. Session/module/storage/trampolines retained до OS reclaim; unload API/FreeLibrary отсутствуют.

## 5. Builds, manifests и preflight

[Согласование hash/source inputs](evidence/m1/NATIVE_SECOND_INSTANCE_51/BUILD_COORDINATION.json), [Host build manifest](evidence/m1/NATIVE_SECOND_INSTANCE_51/BUILD_MANIFEST.json), [Main Client manifest](evidence/m1/NATIVE_SECOND_INSTANCE_51/BUILD_MANIFEST_MAIN.json), [executed helper/review snapshot](evidence/m1/NATIVE_SECOND_INSTANCE_51/FINAL_HARNESS_MANIFEST.json), [completed harness с README](evidence/m1/NATIVE_SECOND_INSTANCE_51/FINAL_HARNESS_COMPLETED.json).

| Артефакт | SHA256 |
|---|---|
| Fresh Host DLL |`90412198bb08f52b0099426fc07b558aa3fadd2c0a88bfab901d03cc2e8f795f`|
| Main Client DLL |`4887fddaa4071321ab9a89fefb5bc8e41426fb58440c7137fd19759a917fdef3`|
| Initial smoke Client DLL |`556254b66709f11ffc7cc74e0665ca4b679c0e6df47b5d284389bbea6a5a3e30`|
| Host manifest bytes |`d797b5e2c268c91e6698f6cfdf9cabe46cdc66589410c8206243f71fb831b265`|
| Main Client manifest bytes |`96a500fb179367c5758b496f77ff4806817e11a07ed18f664371ef3cdce7f70f`|

MSVC19.44/VS2022/SDK10.0.26100.0, Release x64 C++20 /MT. Client and native adapter compile `/W4 /WX /permissive- /EHsc`; pristine vendored ENet/MinHook reused. Full configure/build logs and command results в evidence `CONFIGURE_*`, `BUILD_*`, `BUILD_SUITES_*`. Disposable-Python wrong-process preflight обеих DLL вернул193 до hooks/KEX/log creation. Старые принятые DLL DOC-41–50 не заменялись.

Host native bootstrap/profile и network_input.cpp/.hpp byte-identical pre-task sources. Host включает действующий DOC-50; историческая DOC-49 DLL не использовалась. После smoke logger fix изменились только Client DLL/PDB; Host DLL и все12 regression executables byte-identical. Core shared inputs обоих manifests совпадают. Runtime-loaded DLL/PDB заморожены отдельно в каждом run.

## 6. Последовательность runs и ошибки

Был **один Host process**, один startup-only Client smoke, затем один main Client process. Одновременно работали максимум две игры. Main не повторялся после terminal defect.

- [Host launch](evidence/m1/N51_HOST_01/launch.json): PID34728, simulation3964, `experiments/m0/game`, fresh profile `native_second_instance/profile/N51_HOST_01`. Host isolation PASS до Permit. Accepted spawn-on-ready сохранён: P2 создаётся после Client handshake/ready, не заранее посредством нового host path.
- [Startup smoke](evidence/m1/N51_CLIENT_01/launch.json): PID31804/TID11096. Secure `_wfopen_s` открыл journal без shared read; gate не мог прочитать файл. INPUT Permit/Worker/Spawn не было. Штатный WM_CLOSE, exact Update restore, no faults/foreign calls, normal OS exit0 в13:19:39.299Z. Исправлен только `_wfsopen(...,_SH_DENYWR)`; [diff](evidence/m1/NATIVE_SECOND_INSTANCE_51/SMOKE_LOG_SHARING_FIX.patch), [artifact comparison](evidence/m1/NATIVE_SECOND_INSTANCE_51/SMOKE_CORRECTION.json). Это диагностический startup defect, не скрытый повтор неизвестного ABI.
- [Main Client launch](evidence/m1/N51_CLIENT_MAIN_01/launch.json): PID27724, simulation9668, independent `native_second_instance/game`77 files, fresh `N51_CLIENT_MAIN_01` profile. Journal readable; [isolation PASS](evidence/m1/N51_CLIENT_MAIN_01/ISOLATION_GATE.json) до Permit.
- Первый async bind был вызван раньше появления launch.json и отказал `Unknown owned run`, без DLL/hooks. Повтор произведён только после появления проверенного launch.json; новый game process не создавался. [Harness errors](evidence/m1/NATIVE_SECOND_INSTANCE_51/HARNESS_ERRORS.md).
- Первый W/S window test прошёл native Client/UDP/Host acceptance, но P2 ещё отсутствовал из-за inactive Host Player Update. Этот этап сохранён как неполный proof; [первый input journal](evidence/m1/N51_CLIENT_MAIN_01/WINDOW_INPUT.json). Затем на **тех же PID/Session/gen** применена bounded HWND activation fixture и повторён именно input test после реального Spawn; игра не перезапускалась.
- В первой версии собственного offline review Host worker ошибочно выводился из startup bind TID3964. Existing startHost bind происходит на запускающем simulation caller. Actual service/closure TID6252 установлен по **тому же socket0xa58**, затем проверен OS join. Первый [review result](evidence/m1/NATIVE_SECOND_INSTANCE_51/RESULT_SUMMARY.json) сохранён; corrected final result не исправляет production ради QA.

## 7. Handshake, generation и sequence

Main Client Worker1148 →127.0.0.1:28745, Host Worker6252. [OS live identities/threads/endpoints](evidence/m1/NATIVE_SECOND_INSTANCE_51/OS_live_active_host.json). Конфликтов endpoints нет: fixed host socket и динамический client endpoint. Config/log/save paths независимы; оригинальные пользовательские profiles не копировались.

Client WELCOME13:22:15.583Z: authoritative slot1/gen1/epoch1/domain `M12A_INPUT_ONLY`. Ready отправлен после local gameplay witness; single empty domain baseline принят13:22:15.598Z, `world_synchronized:false`. Это готовность INPUT-сессии; локальный level клиента не объявлялся загруженной копией мира хоста. Reconnect key остаётся внутри Node/DTO memory и не печатается в diagnostic logs/UDP excerpts.

INPUT sequence принадлежит Worker, начинается с1 после authoritative WELCOME каждой generation. Samples sequence не задают; пропущенные grid slots его не увеличивают. На WELCOME/loss/reconnect очищаются sample/ready/sequence; old slot/gen DTO не relabel. При sequence≥UINT32_MAX−1800 или dispatch gap>20s запрашивается новая generation до дальнейших INPUT. Нет lifetime180s cap, wrap или новой wire semantics. Initial≤10800 и advance≤1800 остаются неизменными host predicates. Arbitrary INPUT-only blackout>host window не обещается восстанавливать без reconnect; ENet reliable ping сам по себе не доказывает INPUT delivery.

В main реально получены1…11855 в одной generation, около197.6s, **60.0027Hz** по одному Client steady clock. Это уже дольше headless180s cap; не длительный soak и не runtime billions/wrap test. Native reconnect в этом run не выполнялся; его low sequence/cache retirement проверены автономно. DOC-50 bounds проверяются на wire ingress, не повторно на coalesced Session DTO.

Каждый из11855 Host accepts связан с Client dispatch и успешным synchronous WSASendTo return по `(slot,generation,input_seq)` и forward. OS observer не сохраняет HELLO/WELCOME bytes/secret. Local enqueue не считается UDP send; UDP send не считается Host acceptance. В этом loopback run потерь INPUT не наблюдено; независимый loss не обязан сохранять равенство sets в общем случае. Межпроцессная задержка вычитанием часов не рассчитывалась.

## 8. Native P2 movement / Release / P1

После [Host synthetic activation](evidence/m1/NATIVE_SECOND_INSTANCE_51/RESULT_SUMMARY_FINAL.json) в13:24:40.242Z один accepted native Spawn:

| Identity | P1 | P2 |
|---|---|---|
| Player |0x140af33e0|0x7ffa6d0e20e0|
| Actor |0x61a723b0|0x61a72850|
| Node |0x61fae388|0x61facb20|
| Owner |свой Player|свой Player|
| Health |200|200|

Это не рендер-дубликат. Native factory/controller/Commands/actual Puppet world traversal — прежние accepted paths, без manual physics или P2 pointer repair.

Источник клавиш — **автоматизированный Windows PostMessageW test**, не физическая клавиатура. [Actual-foreground / keydown/up journal](evidence/m1/N51_CLIENT_MAIN_01/WINDOW_INPUT_ACTIVE_HOST.json), [helper completion](evidence/m1/N51_CLIENT_MAIN_01/WINDOW_INPUT_HELPER_ACTIVE_HOST.json). Foreground owner проверялся во время удержания; Client sample читался из KEX, не из Windows key-state API.

| Applied interval | Input seq | Native ticks | Updates | Net XY / path XY |
|---|---|---|---|---|
| W / native4 |10952…11029|33829…33906|78|741.984 /782.968|
| Release / native0 |11030…11137|33907…34014|108|181.082 /181.082 (coasting)|
| S / native8 |11138…11215|34015…34092|78|805.547 /805.547|
| Release / native0 |11216…11855, затем cached neutral|34093…36117|2025|39.301 /39.415 (coasting/collision)|

Release INPUT11030 и11216 имеют actual forward0, Host accepted, native drive0. Первые XY steps после release15.9689 и6.3540 не являются «несанкционированным вводом»: предыдущий hold и native inertia сохранены. В обоих neutral intervals последние30 XY steps точно0; native velocity/position не обнулялись adapter. Изменение Z при S/collision отражено raw before/after и не выдаётся за network camera/world synchronization.

4563 updates,4563 command calls,4563 Puppet ticks, уникальный native tick каждого application, finite positions/guards. Во всех4563 глубоких сравнениях P1 before==after: input32, node/Actor/owner, health, weapon/pending/animation, owned8/ammo8. P1 остался `[-7420,-6775,1152]`, health200. Это bounded preservation; отдельный physical P1 keyboard test/бой/переход не выполнялись.

## 9. Shutdown: доказано и не доказано

Client13:25:33.115Z: simulation9668 ждёт **Worker1148**, socket0xa28 закрывается на1148 return0, WaitForSingleObjectEx return0/GetExitCodeThread0. Затем native `client_worker_joined`13:25:33.117Z, exact Update restore, quiesced → engine cleanup → FinalExit → независимый [launcher exit0](evidence/m1/N51_CLIENT_MAIN_01/exit.json)13:25:33.253Z. Native/affinity faults0. Один terminal best-effort invalid sample остался outbound и явно посчитан retired1; он **не объявлен доставленным Release**. Уже доставленные neutral и Host disconnect/timeout path сохраняют fail-safe; Host оставался neutral после ухода Client.

Host13:25:56.215Z: OS join именно **Worker6252** (bound socket0xa58 closure return0 на6252, wait0/thread exit0); native `network_joined` до four exact control restores, одного exit Reset/handle null/P1 unchanged/guards и engine cleanup. Worker lost_on_stop/retired_outbound/resync/affinity0; rejects foreign/stale/gen/jump0. [Raw host OS sequence](evidence/m1/N51_HOST_01/STATUS_EXIT.json).

Но native FinalExit13:25:56.428Z **не завершил OS process в bounded наблюдении**. [Независимый WinAPI probe](evidence/m1/NATIVE_SECOND_INSTANCE_51/OS_TERMINAL_STATE.json)13:29:31: GetExitCodeProcess success/code0, **WaitForSingleObject(process,0)=258 WAIT_TIMEOUT**. CIM всё ещё видел PID34728. `Process.HasExited=true` и код0 поэтому не приняты как доказательство завершения. Запрос close Frida session также оставался незавершённым до OS cleanup. Причина — engine, instrumentation, native final/lifetime или иная — **UNKNOWN**; stack/caller виновник не установлен.

После сохранения этого Evidence выполнен только path-verified OS `TerminateProcess` существующего test PID34728, без KEX calls/destructors/unload/ремонта pointers. [Cleanup result](evidence/m1/NATIVE_SECOND_INSTANCE_51/OS_CLEANUP_ATTEMPT.json): return true/error0/process wait2s=0 в13:30:29.847Z. Последующий host `exit.json`0 относится к этому завершению и **не маскирует принудительный cleanup**. Запрос detach был начат уже после native FinalExit, но до process-signaled; точную непрерывность guard на terminal промежутке не утверждаем. Protected snapshots и file-only integrity проверены после фактического выхода.

[Final OS0 processes/endpoints](evidence/m1/NATIVE_SECOND_INSTANCE_51/OS_final_after_cleanup.json), [все22 sessions closed](evidence/m1/NATIVE_SECOND_INSTANCE_51/SESSIONS_CLOSED.json). Отсутствие сетевых/KEX ошибок до FinalExit не закрывает этот shutdown defect. Main run не повторялся, REA MCP не ремонтировался.

## 10. Автономные регрессии и integrity

Все существующие assertions сохранены. [Аргументы, exit codes, binary hashes и raw log paths](evidence/m1/NATIVE_SECOND_INSTANCE_51/REGRESSIONS.json).

| Проверка | Результат |
|---|---|
| pse_test_suite |46/46|
| pse_stress_test |11/11|
| pse_fault_injection |16/16; штатный adversarial abort preserved|
| pse_bridge_acceptance |11/11, affinity enforcement active|
| DOC-46 R1 live |не воспроизведён; current KICK сохраняется|
| DOC-47 R2 с argv r2 |PASS строгого live verdict|
| Generation routing contract |5 scenarios /330 checks|
| Admission/rejoin contract |4 scenarios /112 checks|
| DOC-49 timeout contract |default6/150 и candidates preserved, failures0|
| DOC-50 boundary contract |282 checks/failures0|
| DOC-50 live stress |12 checks/failures0; loss/delay/duplicates/reconnect|
| Новый native_client_contract |3 real-ENet scenarios,136 checks/failures0;59.988Hz dispatch, repeated stale neutral9, old queued INPUT не laundering, gen reset/old ready/reconnect guard|

Последний suite — DTO/ENet тест, не mock/native movement acceptance. UINT32 rotation/20s gap границы проверяются чистым production predicate, не выдуманным миллиардами runtime sends. Gemini offline18/18 — отдельно, с найденными false verdict defects выше.

[Protected baseline3776 files](evidence/m1/NATIVE_SECOND_INSTANCE_51/PROTECTED_BASELINE.json), [initial source baseline](evidence/m1/NATIVE_SECOND_INSTANCE_51/BASELINE.json), latest `INTEGRITY_*.json` и [client independent-copy manifest](evidence/m1/NATIVE_SECOND_INSTANCE_51/CLIENT_COPY.json). Итог: historical_changed0; existing source changes ровно3 перечисленных shared files; original installation snapshot и original77/host77/client77 совпали; все protected before/after/current равны. Fresh test profiles содержат разрешённые собственные configs/saves/logs; исходные пользовательские saves не копировались и не менялись. Guard — bounded NtCreateFile/NtOpenFile/CreateFileW observation, не полный OS sandbox; direct syscalls/inherited handles/renames/other processes не покрываются.

## 11. Граница результата и строго следующий шаг

**Native second-instance input/movement chain доказана; всё задание PARTIAL.** Без новой сети/репликации нужно установить причину Host terminal unsignaled process после native FinalExit и получить штатный OS exit в отдельно разрешённой проверке после доказанного исправления. До этого full PASS не присваивать. Для обычного двухоконного использования дополнительно остаётся явное ограничение Host activity fixture; его нельзя скрывать как production-ready behavior.

Новых native runs, world replication, multiplayer camera, combat, achievements или кампании автоматически не начинать.
