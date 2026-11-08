# П8 / S2.4 — персонажи от третьего лица и анимации

2026-11-04. Исполнитель: Codex / GPT-6.1 Sol. **REVIEW / PARTIAL, режим В**, повышен с Б: новые native lookup/bind ABI, проверка движковых animation resources и запись числового scale собственного пассивного Actor. Независимая приёмка — другая модель; внешний вид — пользователь. S2.4 **не закрыта**.

EXE SHA-256 для всех наблюдений и адресов этого отчёта: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`. Предпочтительная база VA `0x140000000`. Существующие исследования и принятые runs переиспользованы; новые runs имеют собственные профили и IDs.

## Результат и границы

Сделаны отдельные DLL-кандидаты `pse_dll_host_p8` / `pse_dll_client_p8`, флаг `PSE_FEATURE_P8`, сборка `b68`. Внутри существующего клиентского passive Actor вместо мумии показывается новый персонаж. На хосте к существующему P2/Puppet привязывается анимация без создания дополнительного Player/Actor. Внешний вид Full Player меняется только native animation setter; его штатный scale сохранён. Клиентский proxy получает числовой scale из профиля атласа; его refs, target, SourceOwner, флаги, размеры физического AABB и lifetime-контракт не правятся.

В процессе пользователь изменил художественное требование: **вместо простой перекраски John — бойцы и исследователи руин с египетским снаряжением**. Итоговая цель П8 использует два новых атласа, созданных встроенным ImageGen:

- [Боец экспедиции](../experiments/pse_together/assets/alternatives/expedition_ranger_v1.png): песочная форма, бронзовый скарабей, синий платок, рюкзак.
- [Разведчица руин](../experiments/pse_together/assets/alternatives/ruins_scout_v1.png): светлый головной платок, полевая сумка, пояс с инструментами, диагональная перевязь.

Каждый содержит 40 поз: walk 20, idle 5, shoot 8, collapse/death 7. Данные анимаций описывают 8 направлений с зеркалированием: 1/4/2/7 временных кадров. Новые PNG **не содержат bitmap-пикселей John**. Из DOC-13 переиспользована карта направлений и временных кадров. Исходные PNG сохранены побайтово; компилятор ресурсов создаёт только sprite rects, origins, animation text и scale profile. Это AI-generated кандидаты, не утверждение о ручной пиксельной работе или безупречной непрерывности рисунка.

Результат находится в [coop_sprites_expedition.kpf](../experiments/pse_together/assets/coop_sprites_expedition.kpf), [manifest](../experiments/pse_together/assets/coop_sprites_expedition.manifest.json). Промпты: [боец](../experiments/pse_together/assets/alternatives/expedition_ranger_prompt.txt), [разведчица](../experiments/pse_together/assets/alternatives/ruins_scout_prompt.txt). Запрошенный ImageGen размер был 1024×640, фактический — **1586×992**; builder использует фактические размеры и дробное разбиение 8×5, а не выдуманные 128×128 cells. Масштабы proxy: `0.903614` / `0.847458`; границы видимого рисунка определены по alpha ≥128, без перезаписи PNG.

**Открыто:** сейчас два силуэта, повторяемые по чётности слота. Это не восемь различимых скинов/цветов. Первоначальные восемь перекрасок John собраны и проверены как технический baseline run02, но после изменения требования пользователя они не являются финальным набором. Для закрытия S2.4 нужны различимые варианты всех слотов и визуальная оценка новых рисунков, origin/scale, восьми направлений/зеркалирования и непрерывности движения. 3–8 нативных игроков и 7 proxy относятся к П22 и здесь не проверялись.

## Реализация

- [player_animation.hpp](../experiments/pse_together/src/proxy/player_animation.hpp): только DTO. Приоритет death → attack (`buttons & 1`) → horizontal speed² >1 → idle; смерть защёлкивается до явного reset нового entity. Проверяются slot/entity/generation/connection/map/sequence/tick и конечность velocity. Ресурс выбирается по слоту; пользовательские подписи/маркеры не добавлялись.
- [sprite_binding.hpp](../experiments/pse_together/src/native/sprite_binding.hpp): native lookup и native bind, без ручной записи animation pointers. Проверяются префиксы exact EXE, число кадров, каждый delay, отсутствие actions и nextAnim, отличный от `-`. Ресурсные указатели живут только на стеке вызова; не кэшируются между мирами.
- [native_proxy.cpp](../experiments/pse_together/src/native/native_proxy.cpp): staged art DTO только после успешного маршрутизатора; apply только для того же уже применённого transform command и подтверждённого live Actor на Sim TID. При отказе bounded transform новая анимация ждёт; несовпадение sequence не является fault. Preview export публикует atomic slot/pose; native calls по-прежнему выполняются внутри принятого tick scope.
- [host.cpp](../experiments/pse_together/src/native/host.cpp): заполняет существующие wire v1.10 velocity/buttons/lifeState вместо defaults, привязывает P2-art в post-tick capture. Dead P2 пропускает Player Update, сохраняя обычные Puppet ticks по шаблону M0 C13/C19; respawn/новое поколение не добавлены.
- [build_expedition_sprites.py](../experiments/pse_together/tools/build_expedition_sprites.py), [expedition_profile.hpp](../experiments/pse_together/src/native/expedition_profile.hpp): 9 additive KPF members, 80 sprite rects, 16 animation resources. Stock definitions не переопределяются. Клиентский origin компенсирует принятый proxy offset +88 через animation origin; реальная высота игрока от хоста сохраняется.

Wire v1.10 не менялся. Семантические поля уже присутствовали в нём; seam `ProxyCommand` и read-only DTO128 остались прежними. П3 reconnect/unload, П4 placement/portals/buffer, П7 authority, П16 respawn и Steam не реализовывались этим пакетом. П8 не включает флаги П4/П14.

## Новые static evidence и native ABI

Имена в первом столбце — аналитические, не восстановленные исходные объявления. Псевдокод Ghidra не является исходным кодом. File offsets вычислены по секции `.text` exact EXE, отдельно от RVA.

| Аналитическая роль | Preferred VA | RVA | File offset | Evidence / наблюдение |
|---|---|---|---|---|
| Find animation by name | `0x14019ed10` | `0x19ed10` | `0x19e110` | [G_FUNC](evidence/m1/PSE_TOGETHER_P8/static/G_FUNC_14019ed10.json): Win64 RDX = NUL string; создаёт временный kStr, ищет в manager, возвращает resource или null. RCX в наблюдаемом теле не используется. Bounded calls в runs02/04 разрешили ресурсы. |
| Bind animation resource | `0x1401a1170` | `0x1a1170` | `0x1a0570` | [G_FUNC](evidence/m1/PSE_TOGETHER_P8/static/G_FUNC_1401a1170.json): RCX Actor, RDX resource; native stores +108, frame +178=0, progress +114=0; **немедленно вызывает actions frame0**. Runtime postconditions и продвижение кадров подтверждены в run04. |
| ChangeAnim string path | `0x1401a1240` | `0x1a1240` | `0x1a0640` | [G_FUNC](evidence/m1/PSE_TOGETHER_P8/static/G_FUNC_1401a1240.json): lookup → bind, сообщение об отсутствующем имени; кандидат напрямую его не вызывает. |
| Script ChangeAnim thunk | `0x1401a1360` | `0x1a1360` | `0x1a0760` | [G_INSPECT](evidence/m1/PSE_TOGETHER_P8/static/G_INSPECT_1401a1360.json): `MOV RDX,[RDX]`, jump `1a1240`. Registration из E014 переиспользована. Это LAB, не отдельная восстановленная функция. |
| Advance animation | `0x1401a1940` | `0x1a1940` | `0x1a0d40` | [G_FUNC](evidence/m1/PSE_TOGETHER_P8/static/G_FUNC_1401a1940.json): delay 65535 держит кадр; переходы по nextAnim и выполнение actions. Вызывается штатным Actor Tick, не новым hook. |

ABI выше установлен только в показанной области exact build; возвращаемое значение binder не используется, универсальная exception/ownership семантика не заявляется. Проверка всех кадров перед bind обязательна: один лишь вызов ChangeAnim не является чисто визуальным контрактом.

REA Frida работал. Static Ghidra provider REA сообщил `GHIDRA_INSTALL_DIR is not set`; REA не чинился. Для пяти точечных routine использован существующий direct Ghidra 12.2 `invoke_ghidra.py`, `-noanalysis -readOnly`, текущая база того же SHA; invocation/console/export сохранены в [static](evidence/m1/PSE_TOGETHER_P8/static). Сохранённый Ghidra Program содержит исторический input path оригинала, несмотря на выбор тестового EXE; идентичность подтверждается SHA, оригинал только читался.

## Проверки и runs

**VERIFIED offline:** [OFFLINE_04/RESULT.json](evidence/m1/PSE_TOGETHER_P8/OFFLINE_04/RESULT.json) — **27/27** наборов. [Pre-native run04](evidence/m1/PSE_TOGETHER_P8/PRE_NATIVE_GATE_RUN04.json) — **10/10**; отдельное дерево `b68`, MSBuild serial. Текущие legacy targets без П8 равны текущим `b60` по 7/7 маскированных секций. Это защита от нового регресса П8, **не** восстановление эквивалентности принятым DLL П1, нарушенной ранее П4/П14 (DOC-67 F1). Для П1 по-прежнему использовать frozen run03 DOC-61.

| Run | Изменившиеся условия / результат |
|---|---|
| OFFLINE_01 | Build FAIL: include path `thread_bridge_dto.h` в новом test target; исправлено добавлением `pse_core`. [build.log](evidence/m1/PSE_TOGETHER_P8/OFFLINE_01/build.log). |
| P8_HOST_01 | Первый `-file` с абсолютным путём: KEX добавил basepath перед ним; штатный stderr/stdout зафиксировал missing KPF. Окно/gameplay/gate отсутствовали, native test не разрешался. После пассивного capture завершён только этот owned процесс; [EMERGENCY_CLEANUP](evidence/m1/PSE_TOGETHER_P8/P8_HOST_01/EMERGENCY_CLEANUP.json). Не acceptance/exit0. |
| P8_HOST_02 / CLIENT_02 | Технический baseline на John, 8 перекрасок и preview состояний. Собственные profiles/gates, Spawn/Remove/deferred retirement/exact restores, оба exit0. Не финальная графика после изменения задания. [REA](evidence/m1/PSE_TOGETHER_P8/REA_RUN02.json). |
| P8_HOST_03 / CLIENT_03 | Новые атласы; выявлен fault `P8_SNAPSHOT_CORRELATION` после выхода host target за область. Проверка ошибочно требовала равенства staged/applied sequence даже после штатного Refused. Исправлено на defer. Host scale store также ограничен passive proxy; причины подъёма P1 нельзя приписать одной записи scale. Run03 не принят; оба процесса закрыты exit0, живой faulted proxy после потери weak identity вручную не удалялся. [REA](evidence/m1/PSE_TOGETHER_P8/REA_RUN03.json). |
| **P8_HOST_04 / CLIENT_04** | Финальный кандидат. Client camera отведена до сетевого подключения; два отдельных окна размещены OS MoveWindow после отказа безопасного focus fallback (`Caption point covered`). KEX auto-mount `mods/*.kpf`, без дублирующего `-file`. Итог ниже. [REA](evidence/m1/PSE_TOGETHER_P8/REA_RUN04.json). |

В runs02/03 одновременно использовались `-file` и обнаруженный auto-mount, пакет загружался повторно. В run04 это устранено. Additive KPF-файлы тестовых копий после работ удалены только после сверки их собственных хешей; основные пакеты сохранены в `assets/`. Оригинальная установка не использовалась для запуска.

**RUNTIME OBSERVED, bounded run04:** [RUN04_CHECKS.json](evidence/m1/PSE_TOGETHER_P8/RUN04_CHECKS.json), воспроизведение [review_p8.py](../experiments/pse_together/tools/review_p8.py) — **37/37** assertions по raw logs, не полная самоприёмка S2.4.

- Один собственный passive Actor. Обе артефактные DLL загружены DOC-51 loader; оба current-run isolation gates PASS до permit. 16 distinct resources разрешены, action/nextAnim guard пройден.
- Native read-only frame tokens дали **1/4/2/7 кадров у каждого из двух персонажей**, шаг адресов frame array `0x110`; всего 28. Death достигал frame6 и оставался там. Это реальное продвижение анимаций KEX, но переключение walk/shoot/death в этой проверке выполнялось **диагностическим atomic preview**, а не боем хоста. Сетевые combat/death transitions не объявлены принятыми.
- Renderer: **29 948** identity-matched sprite submissions, 0 ambiguous/unknown, 48 interpolation samples, 0 errors/foreign callbacks. 29 сравнений 12 полей клиента — EQUAL. Дополнительный probe читает только принятый DTO128; полный bound source — [observer_P8.js](evidence/m1/PSE_TOGETHER_P8/P8_CLIENT_04/observer_P8.js).
- Повторён конкретный новый regression-trigger run03: обычный host S с foreground handoff вынес target за допустимую область. `refused >0`, native proxy оставался live; animation fault отсутствует. [AFTER_REFUSAL](evidence/m1/PSE_TOGETHER_P8/RUN04_AFTER_REFUSAL.json).
- Один штатный Remove → native OnRemove/PoolReturn → deferred retirement, weak pointers null, затем 6 exact16 hook restores. Оба retained handles signaled с exit0; профили before/after равны, REA-сессии закрыты.
- [INTEGRITY_FINAL](evidence/m1/PSE_TOGETHER_P8/INTEGRITY_FINAL.json) = before: исходные **77/77**, обе EXE-копии и защищённые сохранения без изменений. Полный аудит оригинальной установки совпал.

Финальные DLL: host `b811e36a02508cb9b88a7a1b3ed30a3e387ff7763c8c81f7ccc66c301ba16797`, client `4e8b16a56cda66f0f87206a06b1b6643e936d59b801ffbedda96a79a17032848`. Использовать [run04 native_artifact host](evidence/m1/PSE_TOGETHER_P8/P8_HOST_04/native_artifact) / [client](evidence/m1/PSE_TOGETHER_P8/P8_CLIENT_04/native_artifact); `b68` может быть пересобран. Source snapshot — [PRE_NATIVE_GATE_RUN04_sources](evidence/m1/PSE_TOGETHER_P8/PRE_NATIVE_GATE_RUN04_sources).

Пакет новых спрайтов SHA-256 `72803415e0c37b97a56071406a00f31c8e03988d1641bf6226f4fee583db629f`. Исходные PNG и все KPF members имеют хеши в manifest; их pixels не ресайзились/не перекрашивались кодом.

## Что ещё требуется для S2.4

1. Визуальная оценка пользователем двух архетипов и рисунков движения; отдельные различимые варианты слотов 3–8. Сейчас слоты повторяют два архетипа, полной перекраски нового roster по слотам нет.
2. Игровая проверка всех восьми направлений/flip, кадров в разных освещениях и масштабов/точек опоры. Static animation rotation map не равна такому плейтесту.
3. Автоматические walk/attack/death переходы из реальных authoritative состояний, включая host death/respawn. Offline policy и native preview этого не доказывают. Respawn/новое поколение зависят от П3/П7/П16.
4. Placement-ограничение П4: в run04 настоящий P1 поднялся на P2; host Z≈1302, proxy Z≈1390 (=+88), floor1152. Поэтому новый спрайт виден над полом **по координатам хоста**, что не исправлялось фальшивым floor snap. Скриншоты не являются демонстрацией корректного grounded spawn.
5. Независимая проверка режима В новых native resource calls, guards/scale и финального source snapshot. Не запускать неизменённые завершённые runs повторно.

`validate_results.py` прошёл до и после native runs. Общий `validate_handoff.py` до и после вернул FAIL: **те же 117 ошибок**, включая старые broken links (`file:///...`, historical extracted scripts) и несовпадения исторических dossier. [Delta](evidence/m1/PSE_TOGETHER_P8/HANDOFF_VALIDATION_DELTA.json): добавленных/исчезнувших ошибок 0/0. Вне П8 они не исправлялись; оба raw вывода сохранены. Это не PASS всего репозитория. Пакетные ссылки — 0 broken; final integrity и `git diff --check` PASS.
