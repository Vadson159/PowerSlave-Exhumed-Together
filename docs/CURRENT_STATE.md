# Текущее состояние

> Обновлено 2026-11-03, после DOC-58B.2 и задачи S0.1. Это короткая точка входа: подробности — в [PROGRESS](PROGRESS.md) (журнал) и [ROADMAP](ROADMAP.md) (план). Обновлять после каждого завершённого блока, если меняется общая картина. Правила работы — [AGENTS.md](../AGENTS.md).

## Коротко

Хост — авторитетный listen server на KEX 2022. Второй игрок на хосте — настоящий нативный Player/Puppet, им управляет ввод клиента по ENet. У клиента свой экземпляр игры; игрок хоста в нём показан визуальным прокси (пока заглушка — мумия Actor 1007). Мир не реплицируется: враги, двери и предметы у хоста и клиента независимые. Готовность — около 20% по объёму (оценка, [ROADMAP §7](ROADMAP.md)).

## Что работает (в узких границах)

- Второй независимый игрок на хосте: движение, коллизии, оружие, урон, смерть и респаун, выход ([DOC-27](27_M0_COMPLETION_REPORT.md)).
- Своя DLL поднимает P2 в нативном тике ([DOC-41](41_M1_NATIVE_BOOTSTRAP.md)).
- Ввод клиента из второго процесса игры двигает P2 на хосте; хост симулирует в фоне ([DOC-51](51_NATIVE_SECOND_INSTANCE_INPUT_POC.md), [DOC-53](53_HOST_BACKGROUND_SIMULATION.md)).
- Снапшоты игроков хост → клиент приходят бит-в-бит; проверяются карта, эпоха и MapReady ([DOC-55](55_NATIVE_PLAYER_SNAPSHOT_POC.md), [DOC-56](56_NATIVE_ACCEPTANCE_ADDENDUM_D1.md)).
- Прокси игрока хоста на клиенте: спавн, движение, интерполяция, переход сектора 259 → 123, удаление; при выходе в титульный экран KEX сам уничтожает живой прокси ([DOC-58B](58B_NATIVE_NETWORK_VISUAL_PROXY_INTEGRATION.md), [DOC-58B.2](58B2_NATIVE_ACTOR_OWNERSHIP_UNLOAD.md)).
- Автономный ENet-прототип с репликацией мира (мок, 42/42) — к игре не подключён ([DOC-29](29_ENET_FINAL_ACCEPTANCE.md)).

Сетевые прогоны в игре (DOC-51…58B.2) — два процесса на одном ПК, один прокси, карта TOMB (Tomb of Ramses). Прогоны M0 и M1.1 шли в одном процессе.

## Замороженные артефакты

Хеши пересчитаны по файлам 2026-11-03 (проверка Sonnet), кроме `source_sha256` адаптера: он взят из журналов прогонов и по файлу не воспроизводится.

| Что | Путь | SHA-256 |
|---|---|---|
| EXE игры (тестовые копии; оригинал — только чтение) | хост: `experiments/m0/game/`, клиент: `experiments/m1/native_second_instance/game/` | `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d` |
| Host DLL (DOC-56) | `experiments/m1/native_map_ready/build/Release/pse_m11_bootstrap.dll` | `2b66f075752b2863add8ee59fb768e2943a77d888982deafda67d13c042ecdf7` |
| Client DLL (DOC-58B) | `b58/Release/pse_native_client58b.dll` | `3249b735829c15a9f7eda8053068ea49a1ff295dc4c96bc5e78cd8bd32c1db1e` |
| Адаптер прокси (Frida JS) | `experiments/m1/native_network_visual_proxy_58b/adapter58b.js` (файл) | `6aec12aceb739af68f1c01ca6c2974085b8ff02f6ebb232c90477f61f93b81ee` |
| Адаптер, собранный на прогон | `adapter_bound.js` в evidence прогона (58B.2: `docs/evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2/N58B2_CLIENT_01/`) | `e67acccf291d6ec5063ac87fd80df73fcb7a8609b29265852881cde1454d8690` (58B.2); журнальный `source_sha256` тела адаптера в DOC-58B и 58B.2 — `808297bac9b8d7d2af43939f7eef3846b602cf2a169173991875d00039803aec` (`runner58b.py:31`) |
| Автомат состояний прокси (DOC-58B.2, проверен только offline) | `experiments/m1/native_actor_ownership_58b2/src/visual_proxy_58b2.hpp` | `86846b756bf50eefda988a76510b28379a3d5c32dfddb6978d5835a593b5a330` |
| Wire v1.10 | `tools/enet-prototype/include/net_protocol.h` | `8272aa3ef7bcd9e555e8a7b6bc12a1fe320b378dc34c8edbe6b38891e3bbac0f` |

## Открыто

- Реконнект и повторный Spawn прокси, смена карты с живым прокси, hazard «Remove в полёте» — S1.1–S1.2.
- Gates DOC-58B: порталы (58C-04) и 7 прокси (58C-06) — DEFERRED, InsertActor (58D-08) — PARTIAL; G1 — VERIFIED только для одного выхода в титульный экран.
- QA DOC-58C в корне референсов (`TEST_RESULTS.json`: 24/32, 7 FAIL, 1 NVR) не разобран — S1.1.
- Свой игрок клиента не сверяется с хостом — S2. Общего мира нет — S3.
- Адаптер прокси живёт во Frida-JS, DLL грузится через тестовый стенд — S0.4 и S9.1.
- Теста на двух ПК не было — S1.6. Не начаты: правила кооператива, кампания, достижения, 3–8 игроков, UI.

## Следующие шаги

- S0.1 принята (проверка Sonnet 5.5, 2026-11-03).
- S1.1 — DOC-58C (Opus max). Параллельно: S3.1 — ADR-2 «мир клиента» (Opus max, второе мнение — Gemini max) и S0.3–S0.5 — единая база.
- S0.2: git заведён в корне мода (исходники и docs; evidence, игра, профили, сборки вне git; [DOC-59](59_S0_2_GIT_SOURCES.md)).
- Нужно решение пользователя D7 (облегчённые проверки) — [ROADMAP §6](ROADMAP.md).

## Как запускать

- Лаборатория DOC-58B: `build58b.py` собирает CMake + VS 2022 x64 в `b58/` (по умолчанию цели `pse_native_client58b` и `proxy58b_contract`); `workflow.py baseline | integrity <TAG>` — baseline и проверка целостности (запуск через `python -X utf8 -B`). Аналогичные скрипты DOC-58B.2 — в `experiments/m1/native_actor_ownership_58b2/` (сборка в `b582/`).
- В игре: REA MCP и `runner58b.py` / `input58b.py` (или версии `58b2`) со свежими именами прогонов. Ловушки стенда — [ROADMAP, приложение A](ROADMAP.md).
