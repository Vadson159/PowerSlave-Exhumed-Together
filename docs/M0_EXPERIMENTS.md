# M0 — экспериментальная реализация

**Финальная приёмка M0 PASS:** [DOC-27](27_M0_COMPLETION_REPORT.md), [manifest](evidence/m0/M0_ACCEPTANCE.json), [M1 handoff](M0_TO_M1_HANDOFF.md). Native lifecycle двух независимых игроков на exact build принят по совокупному Evidence. C23 добавил реальный projectile через смерть/E/new generation/impact/old-owner attribution/native release; C25 принял Direct Quit pin-until-exit, без навязанного world teardown. [C23](evidence/m0/C23_PROJECTILE_THROUGH_RESPAWN/RESULT.md), [C24 observer overflow PENDING](evidence/m0/C24_NATIVE_DIRECT_QUIT/RESULT.md), [C25](evidence/m0/C25_NATIVE_DIRECT_QUIT_BOUNDED/RESULT.md). C23 auxiliary120-alive marker incomplete не скрыт; эта дополнительная эвристика не заменяет mandatory lifecycle events. Не повторять успешные C19–C22 без нового технического основания. ENet/Thread Bridge/M1 code не трогались. Следующие старые срезы сохраняются как история; актуальные ограничения — финальный DOC-27.

**Последний фактический срез:** [C20 saved-Evidence PASS](evidence/m0/C20_CONTEXT_WORLD_EXIT/RESULT.md), [C21 aggregate PENDING/direct-Quit unbind](evidence/m0/C21_DIRECTED_PHYSICS/RESULT.md), [C22 directed physics PASS](evidence/m0/C22_DIRECTED_PHYSICS_FINITE/RESULT.md). C22 подтвердил7×120 native updates: обе directed collisions, wall/sector/floor/jump/landing, finite агрегаты, затем внешний+d8→0 до native Actor destructor/StateExit return, exact hooks и explicit script unload. Native direct-Quit destructor/free UNKNOWN; M0 не закрыт. C01–C20 не повторять без нового конкретного основания. Ниже сохранены предыдущие исторические срезы; актуальные ограничения — DOC-27/PROGRESS.

**Актуальное продолжение:** [DOC-27](27_M0_COMPLETION_REPORT.md), [C13 после Opus](evidence/m0/C13/RESULT.md), [C14](evidence/m0/C14_NATIVE_LIFETIME/RESULT.md), [C15](evidence/m0/C15_PASSIVE_PROJECTILE/RESULT.md), [C16](evidence/m0/C16_WALL_PROJECTILE/RESULT.md). DOC-34 заменяет DOC-32. C16 доказал настоящий Projectile и SourceOwner/ammo/damage P2, но natural release остаётся pending. Ранний alias «basic0x160 / SpawnProjectile1d50b0» ниже исторический и неверный: [native correction](evidence/m0_static_lifetime/RESULT.md) устанавливает initial DynLight binding. Нулевые refs не форсировать; C17 host-death gate пока отдельная проверка. M0 не закрыт.

**M0 не принят полностью. C05 подтвердил настоящее попадание второго native Player по Anubis:** штатная melee action → native InflictDamage, здоровье цели100→80, source/owner=P2. Первый Player сохранил graph и обычный ввод, игра штатно закрылась. После боя обнаружена удерживаемая target-ссылка, поэтому полное уничтожение объектов НЕ принято. Изоляция C05 также неполна для служебных Steam log/cache файлов; подробнее ниже.

Разрешение пользователя: runtime M0 только в отдельной копии/профилях внутри мода; оригинальная установка и сохранения read-only. Сеть/ENet и Steam-достижения не менялись. Использованы REA Frida17.23.2, исполняемый native CModule и точечная Ghidra12.2 по сохранённой базе. DLL на диске не создавалась; диагностический native код компилируется CModule внутри выбранного процесса. Это адаптер исследователя, не исходники KEX.

## Результаты и Evidence

| Опыт | Наблюдение | Ограничение |
|---|---|---|
| A01/A02 | Runtime prefixes/base, первый Player graph | Ранние stdout/stderr открываются до -userpath; часть логов попала вне workspace в профиль sandbox account |
| A03 / M0-A | Исправленное перенаправление наблюдаемых log/cache paths; native первый Player | Не полный filesystem audit/lifecycle test |
| A03 / M0-B | Второй Spawn забирает singleton handle; оба owner одинаковы | Сам по себе не создаёт независимого controller |
| C01/C1 | Отдельный408-byte Player context, native Reset/Spawn,300 Puppet ticks, retirement | Конструктор без ready bindings недостаточен |
| C01/C2 |420 controller calls каждому, противоположные команды/поворот, разные sectors/health | Выявлена null weapon animation |
| C01/C3 | Native weapon-ready,420 collision calls каждому, отдельные melee actors/owners, retirement | Удары вдали от цели; hit не доказан |
| C01/C4 | Только armed; Spawn не начался | Tick перестал наблюдаться, primary в SleepEx; точная причина UNKNOWN |
| C02 | Подтверждён отдельный CodexSandboxDesktop при input desktop Default; menu tick существует без foreground | Позже окно hung; горячий SleepEx observer — дополнительный confounder, не доказанная причина |
| C03/C04 | Неуспешный запуск через provider, game PID не записан; C04 WinError6 stdin | Launcher stdin=DEVNULL исправлен и проверен в C05; это не failure Full Player |
| C05 | Default/input desktop, user-active карта и tick; P2 native melee hit Anubis100→80; P1 обычный ввод; native exit0 | После Remove оба node refs1, enemy.target→P2; Steam auxiliary writes вне workspace |

[A01](evidence/m0/A01/RESULT.md), [A02](evidence/m0/A02/RESULT.md), [A03](evidence/m0/A03/RESULT.md), [C01 с подробными C1–C4](evidence/m0/C01/RESULT.md). [Файлы/SHA](evidence/m0/M0_FILES.json), [валидация целостности](evidence/m0/M0_VALIDATION.json). SHA EXE `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`; observed base0x140000000, адреса вычислять от module base.

Актуальный полный опыт: **[C05/RESULT](evidence/m0/C05/RESULT.md)**, [машинная сводка](evidence/m0/C05/RESULT_SUMMARY.json), [retained graph](evidence/m0/C05/retained_graph.json), [outside-write facts](evidence/m0/C05/OUTSIDE_WRITE_OBSERVATIONS.json). Исследование конструктора и A01–A03 не повторялись. C05 reused C01 exact-build constructor Evidence. Новых Ghidra analysis/decompile запусков не было; только read-only четыре instruction windows уже известных getter/setter anchors для объяснения retained refs.

В C05 первый Player получил200→160 от двух штатных атак AI; не утверждать, что health остался неизменным. Node/owner не перепривязаны, P2 не использовал оружие P1; после снятия hooks первый Player двигался/поворачивался обычным вводом. P1 hardware builders сохранялись; production13-button sampling всё ещё требует отдельной проверки.

## Существенные дополнения к handoff

Native initializer RVA1ae110 жёстко пишет singleton и повторно не вызывался. Read-only witness подтвердил все408 bytes сразу после него. Версионный адаптер создаёт собственную нулевую память с guards: +80=-1, +e0=self, +160/+178=interpolant vtable RVA5137c8. Ready-фаза: +168→self+13c, +180→self+134. Без неё native Reset RVA1dfa70 попадает в m_pRefValue assert. Native inventory helper RVA1dfb40 использует загруженные weapon definitions. Для оружия дополнительно native RVA20f9e0(P2+e0,mode0); animation pointer не копируется из первого Player. Singleton presentation registry не расширялся; второй view/HUD UNKNOWN.

GetPlayer RVA1b5520 — пятибайтный leaf. В проверенном nested-native сценарии JS/standard replacement не выбрали P2; replaceFast отказался. Рабочая версия использует ровно5 байт rel32 redirect к собственной near bridge/native resolver. Bytes строго проверяются, установка/восстановление — на simulation thread в конце tick. Resolver выбирает P2 только в scoped Spawn; native код пишет node/owner/refcounts. Нет memcpy живого Player или ремонта object pointers. Это version-locked diagnostic hook, не production hook manager.

Нативный controller adapter вызывает original Player Update последовательно для двух contexts и подаёт synthetic commands через замены hardware builders. Native13-button hardware loop всё ещё выполняется дважды; настоящие input queues/mouse не приняты. Actor traversal сама обновляет Puppets. Память Player pinned до выхода процесса; общая деаллокация и скрытые ссылки UNKNOWN.

## Воспроизведение

**Актуальная процедура после C05:** обычный `exec_command` запускает child на служебном Codex desktop. Для интерактивного повторения использовать существующий REA provider `start_frida_session(mode=spawn,program=C:\Python314\python.exe,argv=[-B,<abs experiments/m0/provider_launcher.py>,<новый run>])`, затем `resume_frida_session` для Python parent. Parent создаст game child suspended и запишет launch.json. Не создавать run directory самостоятельно до launcher: он требует новое имя. Provider stdin теперь явно DEVNULL; stdout/stderr остаются внутри workspace.

По child PID attach Frida, сгенерировать observer config и isolation (`generate_observer.py`, `bind_probe.py <run> isolation`). **Полный observer и constructor witness не загружать заново.** Загрузить generated_isolation.js и отдельный readiness.js bound с cfg из observer config + recorded simulation_tid; SleepEx hook disabled. Resume child через `provider_action.py <run> resume`, запущенный тем же REA provider. От parent/helper Frida detach после завершения соответствующей операции, не от живого gameplay probe. Helper не меняет desktop/привилегии, просто наследует уже существующий provider context.

Пользователь щёлкает тестовое окно и загружает New Game/Normal/Tomb of Ramses. До gameplay probe сохранить status: state3/pending0, tick entries/exits увеличиваются, actual TID совпадает с primary, foreground=owned active/focus HWND. State3 без tick недостаточен. При неинтерактивном desktop сообщить конкретное действие пользователю; не повторять слепую активацию и не вызывать gameplay с worker thread.

Перед повтором combat **сначала принять новую изоляцию**: launcher теперь выключает два optional Valve Vulkan layers по их установленным disable_environment keys; OS-path adapter дополнен Steam overlay/log/cache paths. В C05 эта правка ещё не действовала, runtime acceptance её НЕ подтверждён. Сохранять успешные outside-workspace write opens; при них прекращать эксперимент до gameplay.

Точная команда binding после readiness (переиспользует C01, не повторяет constructor research):

```powershell
& 'C:\Python314\python.exe' experiments/m0/bind_probe.py <новый_run> enemy --reuse-constructor-evidence docs/evidence/m0/C01/status_01_constructor.json
```

Mode enemy сохраняет original P1 input, управляет P2, создаёт штатного AT_ANUBIS=1 в наблюдаемой позиции/sector P2, даёт одну атаку и логирует native ray/damage. Он **не вызывает Damage напрямую**. Сохранить generated_enemy.js и raw load/status. После опыта не unload contexts при ненулевых refs; normal `provider_action.py <run> ui close` и exit.json, затем Frida close. Все C05 процессы уже завершены. Следующие команды ниже — историческая схема C01; новый старт выполнять по этому дополнению.

Все текущие процессы/Frida sessions завершены. Bound scripts со старым PID загружать нельзя. Каждый run имеет новое имя; launcher отказывается его перезаписывать. Подготовленная копия — обычные файлы, не hardlinks.

```powershell
& 'C:\Python314\python.exe' experiments/m0/validate_results.py
& 'C:\Python314\python.exe' experiments/m0/launch.py --run C02
```

Launcher оставляет test process **suspended** и ждёт его выхода. Отдельно:

```powershell
& 'C:\Python314\python.exe' experiments/m0/generate_observer.py C02
& 'C:\Python314\python.exe' experiments/m0/bind_probe.py C02 witness
```

REA MCP: `start_frida_session(mode=attach,pid=<launch.json>)`; `load_frida_script(session_id,source_kind=file,path=<observer_bound.js>)`, затем generated_witness.js. Сохранить полные tool JSON в C02. После успешного load выполнить `resume.py C02`.

Навигация: `ui.py C02 capture`, `ui.py C02 key --key RETURN`; стартовый экран/Steam message → main menu → New Game → empty slot0 → Normal → Tomb of Ramses → level. В текущих профилях понадобилось6 RETURN; ориентироваться по screenshot/state. Capture/input адресованы только owned HWND. Сохранить frida_session_status с identity, constructor model_matches=true, state3 tick на primary TID. Без tick не вызывать gameplay из другого потока.

M0-B: `bind_probe.py C02 B`; загрузить generated_B.js один раз, сохранить before/after и остановить disposable run. Он намеренно забирает singleton; не ремонтировать pointers. Full Player испытывать с нормальным первым Player в новом run.

Full Player: после witness/baseline выполнить `bind_probe.py <run> isolation`, загрузить generated_isolation.js **до** unload observer. Затем выгрузить observer: его GetPlayer/PlayerUpdate instrumentation конфликтует с probe. Генерировать mode `lifecycle`, `controller` или `weapon` и загружать ровно один; сначала lifecycle, затем новый run weapon. Исторический C01 испытывал их последовательно с pinned contexts; fresh run с одним probe проще проверять.

Через ~11 активных секунд сохранить status: independent_binding=true, правильные actor/owner, guards=true, counts; в конце null node payload/refs0 и C_resolver_restored. Controller/weapon modes7 секунд active +3 retirement; lifecycle5+3. Не выгружать live probe: native owner/code не должны пережить память. После завершения `ui.py <run> close`; проверить exit/protected_after. При зависании сохранить state/errors, затем только `resume.py <run> --stop`; termination не засчитывает native shutdown.

API/CModule: [Frida reference](https://frida.re/docs/javascript-api/); KnownFolder adapter: [SHGetKnownFolderPath contract](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetknownfolderpath); test-copy steam_appid.txt: [Steam launch mechanism](https://partner.steamgames.com/doc/api/steam_api). SteamAPI_Init под sandbox account не прошла естественным образом; gates не патчились. Пользовательский Steam client не инструментировался и не завершался.

## Следующий gate и результаты аудита SetTarget/refcount

Actual melee hit подтверждён C05; проведён детальный аудит механизма удержания ссылок и изоляции.

**VERIFIED static + RUNTIME факты аудита:**
1. **SetTarget RVA0x1d35d0:** Win64 ABI `(kexSlaveActor* this, kexSlaveActor* pTarget)`.
   - `pTarget == NULL`: `mov rdx, [rcx + 0x28]` -> `mov [rcx + 0x28], 0` -> `lock xadd dword ptr [rdx + 0x40], -1` (атомарно декрементирует refcount целевого узла).
   - `pTarget != NULL`: `newTargetNode = [pTarget + 0x10]`. Если узел отличается: `[this + 0x28] = newTargetNode`, `lock inc dword ptr [newTargetNode + 0x40]`, и декрементирует refcount старой цели.
2. **Почему enemy держал P2 node:** в `kexSlaveAI::OnDamage` (RVA0x195050, строки 165–177) при получении урона AI переключает цель на атакующего: `[enemy + 0x28] = p2.node (0xc85b7d0)` и выполняет `lock inc dword ptr [p2.node + 0x40]`. При `reset(p2)` P2 освобождает собственный handle, снижая refs с 2 до 1, но ссылка от enemy удерживает узел.
3. **Кто держал enemy node (0xc85b818):** в C05 в 22:34:31.127Z зафиксирован урон по P2 от снаряда 0x2c4dcc40 (native caller 0x1401e6584). Статический анализ `SpawnProjectile` (RVA0x1d50b0) доказал: снаряд пишет `[projectile + 0x28] = enemyNode` и делает `lock inc dword ptr [enemyNode + 0x40]`. Активный снаряд удерживал refcount `enemy.node`.
4. **Условие деструкции в deferred sweep:** `FUN_1401b6360` вызывает деструктор `vtable[5]` (`~kexSlaveObject` 0x1d32f0 / `kexSlaveAI` 0x195000), возвращает актора в пул и обнуляет `node->m_pDataPtr` **ИСКЛЮЧИТЕЛЬНО** при `node->refcount == 0`. Так как оба узла имели `refcount == 1`, они не были уничтожены движком.
5. **Вспомогательные скрипты:** в `observer.js` добавлено удаление Win32 префикса `\\?\` для надёжного сопоставления путей логов/кэша Steam. `FUNCTION_CATALOG.json` синхронизирован (`validate_handoff.py` и `validate_results.py` оба PASS).

**Следующий минимальный gate для Sol Max:**
1. Принять исправленную изоляцию в новом Default/provider запуске (проверить 0 записей вне workspace).
2. В `experiments/m0/full_player_probe.js` объявить native `SetTarget`: `native(0x1d35d0, 'void', ['pointer', 'pointer'])`.
3. Перед вызовом `remove(enemy)` на потоке симуляции вызвать `SetTarget(enemy, NULL)`. Это снимет ссылку с `p2.node` (`refs: 1 -> 0`), позволив `FUN_1401b6360` завершить retirement P2 (`payload: 0x0, refs: 0`).
4. Для полного retirement enemy дождаться завершения/деспавна снаряда либо вызвать для него native `remove`.
5. Далее проверить projectile/ammo, death/respawn, directed collisions и level teardown. Proxy не требуется; Full Player остаётся приоритетом.

Full Player имеет runtime основание, Proxy остаётся резервом. A01/A02 sandbox logs и C05 Steam overlay log/new shadercache .foz вне workspace не удалялись и не восстанавливались без исходного snapshot. Original77 game files и protected save/config/userdata hashes проверяются отдельно; их неизменность не означает неизменность вообще всех service files. Native exit0 и один hit не доказывают весь M0.
