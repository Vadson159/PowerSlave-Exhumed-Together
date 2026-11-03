# M0 — отчёт о проверке нативного жизненного цикла

Дата: 2026-11-01. **Финальная приёмка: M0 PASS для проверенных native lifecycle контрактов двух независимых игроков на указанной сборке.** Основание — совокупность runtime Evidence, а не компиляция/exit0/одна статическая гипотеза. Можно переходить к M1 в границах [минимального handoff](M0_TO_M1_HANDOFF.md); реализация M1/M2/ENet/Thread Bridge в этой работе не начиналась. Оригинальные игровые файлы, защищённые сохранения, старые co-op исходники и сетевой трек не изменялись.

## Финальная приёмка после C22

До этого блока уже были готовы C19 (native AI-смерть P2/E-respawn при pending старом поколении), C20 (external unbind до teardown двух поколений, script unload/solo reentry) и C22 (directed physics/finite metrics/world exit). C17 отдельно подтвердил минимальный P1 restart gate с продолжающимися P2/AI ticks. Они повторно не запускались. Точные последние пробелы — live projectile через respawn и достаточный Direct-Quit lifetime contract.

**[C23 PASS](evidence/m0/C23_PROJECTILE_THROUGH_RESPAWN/RESULT.md):** настоящий native Amun projectile продолжил движение при смерти P2 и E-respawn, сохранил SourceOwner старого поколения, столкнулся с новым Puppet и нанёс native damage139 с атрибуцией generation1. Позже Projectile Actor destructor штатно освободил SourceOwner/refs1→0; оба Puppet destructor returns, внешний handle0 до world teardown, exact hooks/guards и explicit script unload подтверждены. P1 оставался200 с прежними node/owner/inventory. SourceOwner/target/refcount/flags вручную не менялись.

Дополнительная C23 эвристика «120 тиков живого нового тела» **не пройдена**: после настоящего grenade hit новое тело получило native source-null79 damage и погибло на нижнем Z. Событие incomplete сохранено, причина падения отмечена INFERRED. Нельзя объявлять все auxiliary checks PASS. Для обязательного projectile contract достаточно реально наблюдённых flight→impact→damage→native release;120 стабильных новых updates отдельно уже проверены C19. Ненужного повторного броска для зелёного marker не было.

**[C24 PENDING](evidence/m0/C24_NATIVE_DIRECT_QUIT/RESULT.md):** слишком широкий passive observer записывал render-temp arenaAECC60, переполнил provider capture; retained export сохранил доступные records, но не восстановил утраченную полноту. Exit0 не принят как lifecycle proof. [C25 PASS](evidence/m0/C25_NATIVE_DIRECT_QUIT_BOUNDED/RESULT.md) с ограниченным observer — единственный необходимый повтор этой незавершённой проверки:519 messages/not truncated, native external Reset/handle0 и exact control-hook restoration до service shutdown; обе engine arena cleanup вернулись, StateExit/actor arenas/owned destructors не вызывались, ticks после quiesce0; затем native fc580→ExitProcess при pinned контексте/коде и целых guards.

[STATIC shutdown contract](evidence/m0_static_final_acceptance/RESULT.md) показывает native `_exit(0)` и различие engine/actor arenas. На проверенном Direct Quit **не требуется насильно добиваться Puppet destructor/free**: достаточно остановить scheduling, отвязать внешний Player до service cleanup и удерживать callback storage/code до окончания процесса. Handle0 сам по себе не разрешает early free: Puppet.owner может ещё ссылаться на Player. Для продолжения процесса остаётся accepted per-level StateExit→destructors→script unload путь C20/C22/C23.

| Обязательный критерий M0 | Решение / фактическое Evidence |
|---|---|
| Отдельный aligned/ready Player, штатные Puppet/node/owner, scoped GetPlayer без повреждения P1 | PASS: C01 constructor/binding; новые независимые contexts C19/C22/C23/C25 |
| Независимые controller/commands/health/weapon states, корректный native ABI/simulation thread | PASS: C01/C05/C16/C19/C22; exact prefixes/TID/once-only journals в новых runs |
| Native weapon damage/ammo и настоящий projectile | PASS: C05 melee100→80, C16 cobra16→15/SourceOwner/damage, C23 native Amun flight/impact/old-generation attribution |
| Floor/wall/sector и встречные player collisions | PASS: C22,7×120 updates/finite metrics/оба направления/independent jump+landing |
| Смерть P2 и E-respawn без global restart, отдельное новое поколение при pending старом | PASS: C13/C19 и C23, native start/health/kit/viewZ/owner, P1 сохранён |
| Смерть хоста не перезапускает мир и не останавливает P2/AI simulation | PASS bounded: C17,90 native updates после смерти через60-tick restart threshold, камера continuation сохранена |
| Native holder release, безопасный world teardown и завершение внешнего контекста/hooks | PASS: C19/C20/C22/C23, release refs1→0 и реальные destructor returns до script unload |
| Direct Quit при живом P2 без преждевременного освобождения callback кода/данных | PASS: C25; достаточный native process-exit contract, Actor destructor на этом пути не требуется |

**Обязательных native blockers в принятом scope не осталось.** [Формальная запись приёмки](evidence/m0/M0_ACCEPTANCE.json) привязывает критерии к хешам сохранённого Evidence и полным summaries C20/C22/C23/C25. Отдельный offline integrity validator не выводит gameplay PASS из сборки: он проверяет неизменность этого review manifest и его runtime Evidence. Все игровые процессы закрыты.

Изменения финального блока: отдельный bounded Amun/E fixture; пассивная атрибуция retired generation; bounded Direct-Quit observer с проверкой engine/actor arenas и финального native exit; офлайн сверка и формальная запись приёмки. Native lifecycle primitives исправленного C20/C22 не заменялись; никаких ручных ownership repairs, force-teardown или новых сетевых механизмов не вводилось. Baseline исходников/документов сохранён [до изменений](evidence/m0/M0_FINAL_ACCEPTANCE_20261101/BASELINE.json).

## История актуализаций до финальной приёмки

Следующие PENDING/UNKNOWN относятся к моменту соответствующих исторических runs. Они сохранены, а финальное решение и реально перенесённые ограничения находятся выше и в конце отчёта. Raw поля `full_m0=false` отдельных probes не являются общей оценкой совокупности всех принятых контрактов.

**C22 directed physics PASS в bounded contract; M0 PENDING.** [C22 RESULT](evidence/m0/C22_DIRECTED_PHYSICS_FINITE/RESULT.md), [машинная сверка](evidence/m0/C22_DIRECTED_PHYSICS_FINITE/RESULT_SUMMARY.json). Семь фаз по120 updates: P2 wall/sector259→258, native blocking P2→P1 и P1→P2 (min center distance≈204.21,116 clips в каждом направлении), независимые jump/landing Z1152→1236.57→1152, health200 обоих. Floor/distance/extrema finite. Затем native Reset очистил внешний+d8 **до** Actor destructor return и original StateExit return; exact hooks restored, guards/P1 целы, explicit gameplay script unload=true. Общий destructor observer после переноса принят runtime. [C21](evidence/m0/C21_DIRECTED_PHYSICS/RESULT.md) остаётся PENDING по aggregate diagnostics: в данном Frida QJS Infinity оказался NaN. Null/first-finite seeds исправлены и приняты именно C22; первый ошибочный mock/raw сохранён. C21 direct Quit отдельно подтвердил live-P2 native Reset/handle0/hooks, но не Puppet destructor/free — только этот более узкий scope PASS.

**C20 PASS в bounded contract; M0 PENDING.** [C20 RESULT](evidence/m0/C20_CONTEXT_WORLD_EXIT/RESULT.md), [офлайн сверка](evidence/m0/C20_CONTEXT_WORLD_EXIT/RESULT_SUMMARY.json). Сохранённый status13 подтверждает native Reset внешнего Player+d8→0 **до** двух Puppet Actor destructor returns и возврата original StateExit1b6670. P1 graph/health/inventory сохранены непосредственно при unbind, SourceOwner старого снаряда освобождён native refs1→0, guards/hooks целы. Затем explicit gameplay-script unload=true и native solo reentry/W/12 920 ticks при health200. Exit0 — только дополнительное завершение, не lifecycle proof. C19 missing-unbind исправлен для проверенного menu-exit пути. Initial zero-time test save load не открыл уровень (UNKNOWN), новый пустой слот использован без перезаписи старого. C20/C01–C19 не повторялись; C21/C22 — новые независимые процессы с отдельными profiles/gates/frozen sources.

Приоритет задаёт [DOC-34](34_OPUS_SOL_M0_ACTIONABLE_HANDOFF.md), заменяющий DOC-32; обоснование — [DOC-33](33_OPUS_COOP_SOURCE_CRITICAL_REVIEW.md). Опасные рекомендации DOC-30–32 не применяются. Сохранённые исходники старого мода — архитектурный референс, не код KEX2022 и не runtime-доказательство.

[C13 оформлен по существующим данным](evidence/m0/C13/RESULT.md), [машинная сверка](evidence/m0/C13/RESULT_SUMMARY.json): RUNTIME OBSERVED — контролируемая смерть P2,90 dead skips/нормальных Puppet ticks при живом P1, два bounded retirement и пересоздание в том же Player-контексте. Повтор опыта не выполнялся. Exit0 и detached зафиксированы, но способ завершения/штатный native shutdown не подтверждены. Context/script unload в C13 не выполнялся.

**Уточнён критерий lifetime:** Remove не обязан немедленно уничтожать объект. При живых держателях refs>0 — pending. Требуется установить штатный release держателя и наблюдать освобождение после его ухода. Снятие ссылок только ради refs0 не является исправлением. C08/C10 остаются ограниченными fixture-опытами; C08 не установил последнего держателя enemy.node, а C10 прошёл с обоими refs0 и без projectile events. SetSourceOwner-null остаётся статически проверенной, но не исполненной веткой; её принудительный вызов не является обязательством M0.

**Новое STATIC VERIFIED исправление собственной разметки:** [узкий native audit](evidence/m0_static_lifetime/RESULT.md) проследил initial PE ctor RVA1d50b0 к **kexSlaveDynLight** (allocator0xe8), а не к Projectile. Число0x160 в прежнем callsite не доказывало размер объекта. Реальный Projectile vtable52dad8: callback1e4a80 освобождает+28/+1c0, deleting dtor1e45e0 делегирует Actor dtor19f210, который освобождает SourceOwner+148; Actor Spawn1a02f0 также освобождает+148 при reuse. Runtime timings/семантика всех рёбер ещё UNKNOWN. Старые сырые данные сохранены, а их ошибочные alias не использовать как класс/атрибуцию. Будущая проба разделяет классы и не вызывает SetTarget/SetSourceOwner ради refs0.

Новые bounded runs: [C14](evidence/m0/C14_NATIVE_LIFETIME/RESULT.md) не достиг pickup; [C15](evidence/m0/C15_PASSIVE_PROJECTILE/RESULT.md) подтвердил native P2 pickup, но AI атаковал P1 и probe остановился до выстрела. [C16](evidence/m0/C16_WALL_PROJECTILE/RESULT.md) подтвердил реальный Projectile vtable52dad8, SourceOwner→P2, расход cobra16→15 и native damage обоим независимым Puppet с атрибуцией P2. Это не directed enemy-hit/AI ranged test. После Remove/Reset старый P2 pending refs1 с действительным projectile holder; принудительное снятие ссылок не выполнялось. Natural release/reuse/destructor не наблюдался даже в сохранённом exit capture; process exit0 не закрывает этот contract. По всем трём runs protected snapshots и original77 files unchanged (offline audit14:14Z). Изоляция — bounded API guard; не полная OS sandbox.

[C17 host death](evidence/m0/C17_HOST_DEATH/RESULT.md) — bounded RUNTIME OBSERVED: шестибайтовый dead-branch jump1dffd6 обошёл две GI transition requests, сохранив update/weapon/timer/camera continuation. После смерти P1:90 P1/P2 native updates,90 тиков каждого Puppet/AI, P2 жив200/движется17.32, GI3/0, death_timer90, camera fields104/finite; карта не перезапущена. Exact gate bytes восстановлены на native SDL shutdown до exit0. Это controlled Damage, не фактическая AI-смерть хоста; успешная AI-атака по P2 в этом интервале не наблюдалась. P2/enemy retire pending, storage pinned. Следом E-respawn с pending старым Puppet/стартовым комплектом/камерой, directed collisions, native level unload и контекст/hooks; пока обязательные gates не пройдены, M0 не закрывать.

Целевая сборка: `anubis_Shipping_Steam_x64.exe`, SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`. Runtime работает только с независимой копией `experiments/m0/game` и новыми профилями. В workspace нет Git-репозитория; worktree/diff Git недоступны. До изменений сохранён [снимок исходников и M0-документов](evidence/m0/M0_COMPLETION_20261101/BASELINE.json); старые успешные bound sources/results не перезаписаны.

## Что было готово

- A03/B подтвердил factory ABI и singleton theft при обычном втором Spawn.
- C01/C1–C3 подтвердили независимые Player/Puppet/owner/node, native ready/reset/spawn, отдельные команды/движение/health/weapon actions и три bounded retirement без combat holders.
- C05 подтвердил настоящий P2 melee→Anubis100→80, сохранение P1 graph/input и native exit0. Полный combat teardown не прошёл; contexts оставались pinned. Изоляция C05 пропустила native Steam log/cache writes.

## Найденные блокеры и исправления

- В C08 fixture native SetTarget-null на simulation TID подтвердил снятие одного ребра target→P2 без ручных decrements. После DOC-34 этот принудительный вызов убран из актуальной passive lifetime пробы: живые держатели сохраняют свои рёбра до штатного release.
- Timer ошибочно завершал наблюдение при refs!=0. Теперь retirement принимается только при null payload/refs0, целых guards/P1; иначе pending/UNKNOWN.
- Добавлены явное16-byte alignment, exact native prefixes, once-only journal native calls, identity invalidation после pool return, getter restoration в construction finally, установка/снятие controller hooks на simulation thread и native TID/state guards.
- Добавлены write-open guard NtCreateFile/NtOpenFile, прямой SavedGames API-return с CoTaskMemAlloc, перенаправление относительных логов, записи raw startup gates и запрет генерации gameplay probe без текущего isolation PASS. Guard — ограниченный диагностический механизм, не полная OS sandbox.
- Исправлена собственная прежняя трактовка RVA1d50b0: initial PE binding создаёт DynLight размером0xe8; прежний вывод о basic projectile0x160 недействителен. Actor Remove на DynLight запрещён. C05/C08 не установили final holder enemy node.
- После native world teardown C19 внешний Player+d8 оставался dangling. Поправка вызывает один native Reset **до** original StateExit, сохраняет original cleanup и pins до окончания owned lifetimes; C20/C22 подтвердили порядок/unload. На direct Quit C21 подтвердил только unbind/hooks; native destructor/free неизвестен.
- Native command duration counters теперь хранятся независимо для P1/P2 в private adapter state: позднее нажатие начинается с1, release обнуляет. CModule mock12 PASS; C21/C22 jump samples подтвердили0→1→2→3→4→0 в реальной игре. Directed observer отказался от Infinity seeds после non-game QJS воспроизведения NaN.

## Реальные новые проверки

| Проверка | Результат | Evidence |
|---|---|---|
| Python AST / JS syntax / pure path policy | PASS:14 Python,10 JS,22 policy cases на момент precombat | [checks](evidence/m0/M0_COMPLETION_20261101/SOURCE_CHECKS_precombat.json) |
| Native guard, disposable non-game process | PASS: workspace create разрешён, external create→ACCESS_DENIED, read→native PATH_NOT_FOUND | [smoke](evidence/m0/M0_COMPLETION_20261101/ISOLATION_SMOKE/SMOKE_RESULT.json) |
| C06/C07 startup | Gates FAIL, gameplay probe не запускался; native exit0; ошибки сохранены | [C06](evidence/m0/C06/RESULT.md), [C07](evidence/m0/C07/RESULT.md) |
| C08 guarded startup / map load | PASS в наблюдаемом API scope, успешных внешних write opens0, errors0 | [gate](evidence/m0/C08/ISOLATION_GATE.json), [readiness](evidence/m0/C08/status_03_map_ready.json) |
| C08 P2 native hit / target release | PASS: hit100→80, SetTarget-null refs2→1, P2 deferred retirement null/0, P1 graph/guards целы | [C08](evidence/m0/C08/RESULT.md), [raw](evidence/m0/C08/status_04_combat.json) |
| C08 complete combat cleanup | NOT PASSED: enemy node refs1; sourceOwner edge actor-impact→enemy наблюдалась до pooling | [late graph](evidence/m0/C08/retained_read_only.json) |
| C08 shutdown | Native exit0; protected snapshots равны; contexts до выхода pinned | [exit](evidence/m0/C08/exit.json) |
| C09 profile check | Общий test-copy save обнаружен, overwrite не подтверждён, gameplay binding отклонён | [C09](evidence/m0/C09/RESULT.md) |
| C10 native combat / unload | Оба node null/refs0, hooks restored, gameplay script выгружен, P1 W/tick продолжался; native exit0 | [C10](evidence/m0/C10/RESULT.md) |
| Native controller CModule mock | PASS6 branches на собственной памяти; engine functions/hooks0 | [raw](evidence/m0/M0_COMPLETION_20261101/CONTROLLER_NATIVE_MOCK.json) |
| C11 death attempt | Не дошёл до окна/тика; death source не загружался; завершён принудительно | [C11](evidence/m0/C11/RESULT.md) |
| C12 startup | Native guard preflight прошёл, gameplay readiness не достигнута; death source не загружался, процесс завершён принудительно | [C12](evidence/m0/C12/RESULT.md) |
| C13 P2 death/recreate | BOUNDED RUNTIME OBSERVED: два цикла на одном Player, P1 цел, node null/0 после native retirement; full_m0=false | [результат](evidence/m0/C13/RESULT.md), [raw](evidence/m0/C13/status_04_death_recreate.json) |
| C13 завершение | Exit0/detached наблюдались; способ завершения и orderly native shutdown UNKNOWN | [exit](evidence/m0/C13/exit.json), [post-exit](evidence/m0/C13/status_05_post_audit.json) |
| C20 external context/world exit | PASS bounded: handle0 до двух Puppet destructor returns, native holder release, script unload и solo P1 reentry/W | [RESULT](evidence/m0/C20_CONTEXT_WORLD_EXIT/RESULT.md) |
| C21 directed / direct Quit | Physics PENDING из-за null aggregates; direct-Quit unbind/hooks PASS, Puppet destructor/free UNKNOWN | [RESULT](evidence/m0/C21_DIRECTED_PHYSICS/RESULT.md) |
| C22 directed physics | PASS bounded:7×120 updates, floor/jumps/wall/sector и обе directed player collisions, конечные метрики | [RESULT](evidence/m0/C22_DIRECTED_PHYSICS_FINITE/RESULT.md), [raw](evidence/m0/C22_DIRECTED_PHYSICS_FINITE/status_03_physics.json) |
| C22 native world exit / script unload | PASS bounded: native Reset→handle0→Actor destructor return→StateExit return; exact hooks/guards/P1 сохранены; explicit unloaded=true | [raw](evidence/m0/C22_DIRECTED_PHYSICS_FINITE/status_04_world_exit.json), [unload](evidence/m0/C22_DIRECTED_PHYSICS_FINITE/unload_06_gameplay.json) |

Последняя [offline integrity validation](evidence/m0/M0_VALIDATION.json) после закрытия C22: PASS, original77/copy77 файлов неизменны, protected profile snapshots всех runs (включая C21/C22) равны, сохранён final detached raw, ошибок артефактов/ссылок нет. Это проверка Evidence и изоляции в указанном scope, **не автоматическое принятие M0**. Все игровые процессы закрыты. ENet/сетевые документы не изменялись.

C09 выявил неполноту именно campaign-profile isolation: новый -userpath не изолировал относительные saves в общем CWD. В C10 CWD перенесён в новый профиль с собственным AppID file; настоящий save появился внутри C10, старые test-copy saves не изменились. Более ранняя C08 startup-gate приёмка не покрывала этот случай.

Native guard rewrite отдельно проходит диагностический smoke до загрузки в игру. Первый native smoke обнаружил неправильные writable static globals CModule; host detached без результата. Исправление обязано использовать extern Memory.alloc storage и проверку до установки hooks. Это не ошибка Player/Puppet и не выполненный death test.

## Проверки M1 и границы принятого M0

M0 принят как native feasibility/lifecycle milestone на указанной exact EXE сборке,2 players/изолированные profiles/один набор fixtures. Это не готовый сетевой кооператив, production DLL или приёмка всей кампании. Контролируемый lifecycle/world-exit detach и Direct-Quit pin-until-exit — разные проверенные контракты; arbitrary mid-level hot unload остаётся unsupported.

Допустимые следующие gates M1:8-player capacity; реальные LAN/VPN/network sessions и command delivery; presentation/HUD/input UX (включая physical E и P1 respawn); безопасная spawn placement policy; полная кампания/сюжетные interactions и все level transitions; save/load compatibility (initial zero-time test save C20 остаётся UNKNOWN); весь арсенал/много одновременно летящих снарядов/длительные игровые сессии. Ничто из этого не объявляется уже проверенным.

C17 принял продолжение native P2/AI simulation после смерти P1; успешная AI-атака по P2 в этом конкретном интервале не наблюдалась. Combat targeting после смерти хоста и длительный death-timer behavior проверять при gameplay integration M1. Это UNKNOWN поведения вне bounded gate, не обнаруженный дефект lifecycle и не основание перепроводить уже успешный M0.

**Решение:** M0 PASS в перечисленных обязательных native контрактах; минимальный M1 handoff подготовлен. Переход к работе M1 возможен с указанными ограничениями, сама интеграция M1/ENet/Thread Bridge сейчас не выполнялась.
