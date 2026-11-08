# DOC-88 — П16 / S4.5: native респаун на E, гибель команды, оружие после возрождения

2026-11-07. **PASS в проверенных границах, режим Б** (исполнитель и проверяющий — Claude по указанию пользователя; независимой приёмки В не было). Продолжение [DOC-87](87_P16_RESPAWN_AND_WEAPON_POLICY.md) (там — offline автомат и кандидат weapon-only пикапа). EXE SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`.

## Требование (D14)

[Решение пользователя](evidence/m1/P16_RESPAWN/DECISIONS.md): после смерти оружие сбрасывается до стартового набора; **оружейный пикап не пропадает с карты при подборе и даёт только оружие, без патронов**; после смерти его можно подобрать снова с того же пикапа.

## Что сделано в этой сессии

Работа Codex после DOC-87 (host native respawn, client revive, TPK-журнал, weapon hook) была не задокументирована и остановилась на run03 (client `p16_confirm_start_geometry`, процессы оборваны без restores). Дефекты найдены и исправлены в четырёх циклах (каждый неудачный прогон сохранён как evidence и завершён штатно, exit 0 обоих процессов):

| Run | Дефект | Исправление | Evidence |
|---|---|---|---|
| 04 | поздний W на хосте: `p4_spawn_expired`, P1 остался на якоре | прогон закрыт без результата, повтор | [RUN04_ABORTED](evidence/m1/P16_RESPAWN/RUN04_ABORTED.md) |
| 05 | `p16_respawn_deferred`: труп P2 и пикап рядом с якорем, запрос терялся, клиент ждал вечно | второе кольцо кандидатов (смещение 100), повтор отложенного запроса (лимит 3600 тиков) | [RUN05_ABORTED](evidence/m1/P16_RESPAWN/RUN05_ABORTED.md) |
| 06 | клиент: `p16_start_geometry_rejected why=z` (клиент снял z=1153 до оседания игрока, хост прислал 1152) | допуск z 4 единицы; высоты пола/потолка считаются нативными вызовами в обеих точках, диагностические события `p16_start_geometry_rejected`/`p16_start_support_mismatch` | [RUN06_ABORTED](evidence/m1/P16_RESPAWN/RUN06_ABORTED.md) |
| 07 | `blocked p7_own_admission`: P7 Authority остался Disconnected после E, новое поколение получило Stale | свежий `ss7::Authority` после клиентского commit; заголовок P7 не менялся | [RUN07_ABORTED](evidence/m1/P16_RESPAWN/RUN07_ABORTED.md) |

Изменённые исходники (`experiments/pse_together/src/native/`): `lifecycle_p16.hpp` `6179485e…3621`, `host_respawn_p16.inc` `4f982c49…7f05`, `host_p16.cpp` `e2b7cfb5…3e29`, `client_respawn_p16.inc` `4ecc4184…c566`. Принятые П7 DLL, wire v1.10–v1.13 и старые targets не менялись.

## Прогон run08 (критерии A–D)

`P16_HOST_08` (pid 33916) и `P16_CLIENT_08` (pid 37456); сборка `b92` (`build.log` без `b60`), [OFFLINE17](evidence/m1/P16_RESPAWN/OFFLINE17/RESULT.json) **38/38**, gate [PRE_NATIVE08](evidence/m1/P16_RESPAWN/PRE_NATIVE08.json). DLL: host `aa8fd32d17bf16dab2e2d1d46d75f53e28dc160c56a5db1533ae1c1f04e0a70a`, client `873ca761f4ee564fffe5c6d3aadac85993b4a7da0b586e7b6874427428f3ba75`. Журналы: `P16_HOST_08/native.jsonl`, `P16_CLIENT_08/native.jsonl`, [RUN08_RESULT](evidence/m1/P16_RESPAWN/RUN08_RESULT.md). События хоста перечитаны отдельно проверяющим по сырому журналу.

| Критерий S4.5 | Наблюдение (RUNTIME OBSERVED) |
|---|---|
| Оружие после возрождения, D14: weapon-only, пикап остаётся | host seq 19644: слот 1, оружие 3, `persistent:true`, ammo до выдачи = после (`[0,60,38,15,65,16,250,6]`), pickup `…5FA0` / node `…8D58` |
| Возрождение на E | смерть слота 1 seq 23429; E на клиенте (физическая клавиатура ОС) rec 12682; host request 24954 → `p16_respawn_committed` 24957 (поколение 2; 200 HP, оружие 3, пистоль 60, тяжёлое 0, `world_reloaded:false`); клиент: `p16_client_confirm_native` 12708, `p16_client_respawn_committed` 14832, повторный `p7_own_admitted` 14836 |
| Тот же пикап после возрождения | host seq 38825: тот же pickup и node, поколение 2, ammo до выдачи `[0,60,0,0,0,0,0,0]` без изменений |
| Гибель всей команды без reload | смерть слота 0 seq 46279, слота 1 seq 46788, `p16_total_party_kill` 46794; E на хосте: request 52245 → commit 52248 (слот 0, поколение 2); E на клиенте rec 31488 → host commit 56057 (слот 1, поколение 3), клиент commit rec 34038; одна эпоха карты, сообщений `p16_level_unload` нет |

RestartGate (патч 6 байт `0x1dffd6`, не даёт оригинальному «use после смерти» перезагрузить уровень) восстановлен exact6. Ключевые native gates после респауна (read-only probe, `writes:0`, review receipt) пройдены; story-поля (keys/artifacts/…) не менялись.

## Завершение и restores

- Run08: оба процесса exit 0 (`exit.json`, `OS_SIGNALED`), `p16_restart_restore exact6:true`, `p16_weapon_hook_restore`=1, 4 контрольных сайта хоста и клиента exact16, 14 клиентских P7 hooks exact16, REA-сессии закрыты, процессов игры нет. [INTEGRITY08](evidence/m1/P16_RESPAWN/INTEGRITY08/RESULT.json) **25/25**.
- Observer: в run08 passive REA observer не выгружался до выхода. Закрыто отдельным host-only [run09](evidence/m1/P16_RESPAWN/RUN09_RESULT.md) (`P16_HOST_09`, тот же frozen host DLL, gate PRE_NATIVE09): сайты `block_dispatch 0xc5310`, `scene 0x1efb40`, `level_unload 0x1c90e0` — до загрузки exact16, с observer изменены, после `unload_frida_script` (`unloaded:true`) снова exact16; exit 0. [INTEGRITY09](evidence/m1/P16_RESPAWN/INTEGRITY09/RESULT.json) **13/13**. Сообщение `p16_lifetime_observer_restored` не получено (его шлёт внутренний таймер observer); доказательство — побайтовая сверка. `p16_restart_restore` в run09 нет (gate ставится при привязке P2/команды) — подтверждён run08.
- Оригинальные 77 файлов игры и обе независимые копии побайтово равны манифесту; оба EXE `b8f131037…`; frozen artifacts и источники gates не изменились.

## Границы и открытое

- **Смерти и пикап в прогоне — диагностические фикстуры** (`PSE_P16_HostFixture`: нативный урон 2000 единиц; пикап типа 44, созданный на P2). Нативный урон врага и подбор штатного пикапа карты не наблюдались; hook срабатывает на любом нативном `OnTouch` с определением `weaponSlotToGive` в 1…7.
- **Хост выдаёт weapon-only только на своём мире.** Мир клиента независим (S3 не сделан): оригинальный пикап клиентского мира при касании клиентом по-прежнему удаляется и даёт патроны локально; хостовое состояние (owned/ammo) затем авторитетно. Требование D14 для общего мира закроет S3.
- Weapon-only для локального P1 хоста — статически в roster (slot 0), в игре наблюдался только слот 1; респаун слота 0 на E и TPK наблюдались.
- Один ПК, два процесса, карта TOMB, sector 259, 2 игрока (Counts 2–8 — только offline). Cross-PC и difficulty не проверялись.
- **Защищённая папка Saved Games:** в ней 7 файлов вместо 5 в базе П7 — `consoleHistory.txt` и `kexengine.cfg`, впервые видны в `protected_after` прежнего (Codex) run02 от 2026-11-07 02:28. Запуски 08/09 их не меняли (before == after == now). Источник появления не установлен; файлы не тронуты — решение за пользователем.
- Режим Б: независимой приёмки В не было; для нативных lifecycle-изменений по AGENTS.md возможен подъём до В — оставлено на решение пользователя.
