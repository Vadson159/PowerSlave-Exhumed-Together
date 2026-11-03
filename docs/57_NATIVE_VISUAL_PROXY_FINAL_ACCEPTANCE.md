# DOC-57 Final Acceptance — bounded PASS

2026-11-03 UTC. **DOC-57: bounded PASS** для одного stock `Obj_Ramses_Body_Rest` / Actor1007 на `maps/TOMB.MAP` exact-build KEX. Один цельный replay исправленного probe завершил Spawn → stability → обычное движение P1 → Remove → pooled retirement → движение P1 → WM_CLOSE / OS exit0. DOC-58 не начат; работа остановлена после DOC-57.

Решение основано на [18 acceptance checks](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/NATIVE_ACCEPTANCE_REVIEW.json), [полном native journal](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/N57_FINAL_01/native.jsonl), [10 попарных сравнениях всех полей](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/PER_FIELD_COMPARISON.json). Исторические [BLOCKED](57_NATIVE_VISUAL_PROXY_STEP0.md) и [PARTIAL](57_NATIVE_VISUAL_PROXY_CONTINUATION.md), их исходники и Evidence сохранены без изменения.

## Native acceptance

| Критерий | Наблюдение |
|---|---|
| Exact EXE | SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`, actual base `0x140000000`,18 prefix anchors |
| Factory ABI | Win64 RCX factory, EDX1007, R8 vec3*, XMM3 yaw0, entry stack+28 sector259; RAX Actor*; stack instruction bytes повторно сверены с диском |
| Процесс / simulation thread | Один fresh `N57_FINAL_01`, PID33460 / TID32052, принятый CREATE_SUSPENDED launcher, новый изолированный профиль |
| Baseline | native_preflight seq4: map/state3/next3/pending0, исходный P1 и единственный node sector259; полный BEFORE seq6 до Factory seq7 |
| Actor / ObjectNode | Единственный returned Actor `0x61aa7f90`, own node `0x61a922f0`, native factory1, type1007, sector259, XYZ `[-7424,-6880,1240]` |
| P1 ownership | Player `0x140af33e0`, node `0x61a94e68`, Puppet `0x61708f70`; own-node/owner/vtable сохранены во всех снимках |
| Видимость | Реальный непрозрачный sprite на игровом кадре, настоящие sprite submissions и20 vertices на наблюдённый submission; исчез после Remove |
| Stability | **1778 distinct native simulation steps**, отдельно **1778 Actor Tick callbacks**; счётчики не складываются |
| Dynamic membership | P1259→123 обычным W; reciprocal native lists подтверждены, в259 остался один Actor node, исходный P1 найден в текущем123; Actor не перемещался |
| Inventory/story |5 полных снимков,12 обязательных полей,10 попарных сравнений; различий0, UNKNOWN обязательных чтений0 |
| Remove / retirement | Ровно один native Remove; Drain/unlink/ref-release → OnRemove → PoolReturn → ObjectNode payloadnull/refs0/in_use0; освобождённый Actor не читался |
| После retirement | Обычный S вернул P1 из123 в259; original graph, health/weapon/ammo/story сохранены;3540 измеренных post-retirement steps |
| Камера | Отдельный read-only prepared-view callback на TID32052: eye `[-7420,-6775,1256]` = original Puppet interpolated XYZ + native Z terms `[0,104]` |
| Faults / exit | Observed native/lifetime/affinity faults0; Frida errors0, messages truncation false;18 KEX prefixes восстановлены; штатный WM_CLOSE, retained+duplicate wait0, OS exit0 |
| Integrity |16 834 исторических файла сверены; разрешены только изменения текущих PROGRESS/EVIDENCE_INDEX; original/copy и защищённые saves совпадают с baseline |

## Inventory field contract

[Полный контракт](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/INVENTORY_FIELD_CONTRACT.json) содержит14 exact-build dossiers с SHA-256, типы, ownership, инструкции и ограничения. Все перечисленные поля встроены в original P1 Player, health находится в original Puppet. Динамических inventory containers в этом обязательном контракте нет. Padding не сравнивается.

| Поле | Native storage / чтение | Наблюдённое значение во всех снимках |
|---|---|---|
| ankh_count | Player+84, int16, readS16 |0 |
| ankh_mask | Player+86,16-bit mask, readU16 |0 |
| artifacts | Player+90, uint32 mask, getter RVA1ae080 → content |0 |
| abilities | Player+94, uint32 mask, getter RVA1ae090 → content |0 |
| keys | Player+98,16-bit API reference/mask, getter RVA1ae0c0 → readU16 |0 |
| quest_items | Player+9a,16-bit API reference/mask, getter RVA1ae0a0 → readU16 |0 |
| team_dolls | Player+a4, uint32 mask, getter RVA1ae0b0 → readU32 |0 |
| current/pending_weapon | Player+88/+8c, int32 |0/0 |
| owned_weapons | Player+cc,8 отдельных uint8 slots |`[1,0,0,0,0,0,0,0]` |
| ammo | Player+bc,8 отдельных16-bit элементов |`[0,60,38,15,65,16,250,6]` |
| health | Puppet+17a, int16 |200 |

Leaf getters имеют ABI `pointer(pointer)`, возвращают borrowed inline reference. Probe проверяет8 bytes getter и returned address, затем читает содержимое поля. Все пять snapshot вызовов выполнялись read-only в attested simulation-thread StateTick scope со свежей проверкой P1 graph. Snapshot metadata включает binary hash, PID/TID, runtime base, native tick и P1 identity; каждое поле содержит имя, offset/getter, тип/размер, raw LE bytes, decoded value и статус. Равенство getter pointers не используется как доказательство равенства содержимого.

Снимки: [BEFORE](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/P1_STATE_BEFORE.json), [AFTER_SPAWN](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/P1_STATE_AFTER_SPAWN.json), [AFTER_MOVEMENT](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/P1_STATE_AFTER_MOVEMENT.json), [AFTER_REMOVE](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/P1_STATE_AFTER_REMOVE.json), [POST_RETIREMENT](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/P1_STATE_POST_RETIREMENT.json).

Все12 полей сохранялись на проверенной короткой траектории W/S в начальной комнате без стрельбы, переключения оружия, урона, pickup или наблюдённого story change. Дополнительные постоянные progression fields непосредственно в принятый контракт не установлены. Проверка чистого начального инвентаря не доказывает все возможные заполненные campaign states.

## Evidence и ошибки

- [Lifecycle journal](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/LIFECYCLE_JOURNAL.json), [sector observations](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/SECTOR_OBSERVATIONS.json), [visual hashes](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/VISUAL_EVIDENCE.json).
- [Live game frame](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/N57_FINAL_01/window_20261103_155120_334765.png), [после Remove](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/N57_FINAL_01/window_20261103_155144_257525.png). PNG получены из owned game window и просмотрены, без генерации/редактирования.
- [OS exit proof](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/N57_FINAL_01/OS_SIGNALED.json), [same-object handles](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/N57_FINAL_01/OS_HANDLE_IDENTITY.json), [полный REA final status](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/N57_FINAL_01/STATUS_EXIT.json), [17 sessions closed](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/SESSIONS_CLOSED.json).
- Первая offline fixture использовала несовместимые mock statuses PASS вместо OK/EQUAL и неполный mock snapshot. Исходные ошибки сохранены в [OFFLINE_01_ERROR](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/OFFLINE_01_ERROR.json); mock исправлен,15 проверок проходят. Ошибка не относится к KEX.
- Первый ui_start запущен до появления окна и получил `No visible window owned by test process`; raw traceback сохранён в provider_ui_start log. После появления окна повторён только menu input в том же процессе. Дополнительный Return на штатном level selector загрузил TOMB. Spawn не повторялся.
- Startup stderr содержит `kexSocialManager::Init: Unable to initialize Steam`. Игра продолжила нормальный gameplay и exit0; Steam integration/achievement functionality данным опытом не проверена и не менялась.
- `FRAME_before` получен на90ms позже Factory и уже показывает Actor: он **не используется как visual-before proof**. Настоящий baseline отсутствия экспериментального Actor — native_preflight seq4 и BEFORE seq6 до Factory seq7. Изображение сохранено с исходной меткой, ошибка timing не скрыта.
- Первый shell `git status` установил, что корень не Git repository. Integrity основана на SHA-256 baseline. Начальная ошибка синтаксиса orchestration JS не запускала shell/игру и исправлена до работы с файлами.

## Команды, границы и сохранность

Все task writes ограничены `S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod`. Новый [код](../experiments/m1/native_visual_proxy/final_acceptance/), [Evidence](evidence/m1/NATIVE_VISUAL_PROXY_57/FINAL_ACCEPTANCE/), профиль `experiments/m1/native_visual_proxy/profile/N57_FINAL_01/`, TEMP/TMP/scratch `experiments/m1/native_visual_proxy/scratch/final_acceptance/`. Использованы установленные Python/Node и существующий REA MCP/Frida/suspended-launcher/isolation/window workflow; новый injector и Ghidra workspace не создавались.

Offline commands из корня S: (TEMP/TMP предварительно указывают на scratch выше):

```text
C:\Python314\python.exe -X utf8 -B experiments/m1/native_visual_proxy/final_acceptance/workflow.py baseline
C:\Program Files\nodejs\node.exe experiments/m1/native_visual_proxy/final_acceptance/offline_test.js
C:\Python314\python.exe -X utf8 -B -c "import sys;sys.path.insert(0,'experiments/m1/native_visual_proxy/final_acceptance');from workflow import syntax;syntax()"
C:\Python314\python.exe -X utf8 -B experiments/m1/native_visual_proxy/final_acceptance/acceptance_review.py
C:\Python314\python.exe -X utf8 -B experiments/m1/native_visual_proxy/final_acceptance/workflow.py integrity
```

Actual argv/cwd/exit codes JS checks сохранены в `OFFLINE_*/RESULT.json` и `SYNTAX_*/RESULT.json`. Review создаёт новые outputs через exclusive create: не повторять поверх запечатанных Evidence. Native orchestration использовала REA spawn/resume установленного Python с `runner.py launch N57_FINAL_01`, затем attach к единственному KEX PID33460, load generated isolation/probe, existing resume/UI/OS helpers и mailbox1/2. Frozen bound probe/config/source hashes находятся в `N57_FINAL_01/PROBE_MANIFEST.json`; fresh PID/profile нельзя заменять историческими.

Production C++/DLL, Wire Protocol v1.10, ENet, DOC55 Snapshot, DOC56 MapReady, M0 lifecycle, DOC53 background policy, host authority, EXE/KPF и пользовательские saves не редактировались. Сборка DLL не выполнялась. Старые сетевые регрессии остаются историческими; в этой работе выполнены новые offline fixtures, syntax, exact ABI verification, native acceptance и integrity. Проверенная тестовая копия игры существовала в проекте; свежим является изолированный профиль, оригинальная установка read-only.

Изоляция bounded: guard наблюдает/запрещает external disk write-opens через установленные seams, errors/overflow0; это не полная OS sandbox и не доказательство отсутствия любых возможных внешних записей. Защищённые saves и игровые файлы проверены независимо по содержимому.

Оставшиеся UNKNOWN вне обязательного bounded PASS: общая C++ destructor/delete/free semantics pooled objects; точная C++ signedness ammo при полностью сравнённых raw16 contents; заполненные inventory states, другие карты/types, network visual proxy и transitions. Общая кампания и multiplayer visual synchronization этим PASS не принимаются. **К DOC-58 можно переходить отдельной следующей задачей; здесь DOC-58 не начат.**
