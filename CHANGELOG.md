# CHANGELOG

## 2026-11-08 — схлопывание форков в один бинарник

- Протоколы `wire111/112/113` → один `src/net` (версия v1.13: нативная идентичность карты, состояние игрока P7,
  чат/лобби P23). Добавлен единый `net_protocol.h`.
- `host_p7/p16/p23.cpp` → один `host.cpp` (трёхстороннее слияние: P7 + respawn/weapon pickup P16 + меню/HUD/автокарта P23).
  `client_p7/p16/p23.cpp` → один `client.cpp`. Три `network_input*.cpp` → один; обработчик respawn регистрируется
  хостом (`m12a::respawnHandler`), без символа-призрака между библиотеками.
- Одна DLL `pse_together.dll`: `dll_main.cpp` экспортирует `M11Prepare`/`M11PermitTest`/… и по `PSE_ROLE` передаёт
  управление `host_*` или `client_*` (два файла, приватное состояние). Загрузчик (broker v2) и `p27.py install` берут
  `pse_together.dll`, в `pe-together.ini` один `[payload] sha256=`.
- CMake: вместо 16 `pse_dll_*` и 33 тестов — `pse_together`, загрузчик и 11 тестов, которые отражают реальный код
  (respawn, self-state, weapon pickup, автомат прокси 58B/58B.1/58B.2, bounded proxy, P4 presentation, UI-кодек,
  lobby на реальном ENet).
- Удалены обёртки REA/Frida (`tools/runtime_*`, `pre_native_*`, `audit_*`, …), мок-тесты старых wire-версий.
- Не вошло в эту сборку: реконнект прокси при смене карты (P3) лежит в `experiments/pse_together/p3_reconnect/`,
  не собирается — слияние дало конфликт всей архитектуры `Session` (процесс-живой роутер + сессия на мир);
  P14 (ИИ врагов выбирает среди игроков) и P8 (спрайты) выключены; тесты сессии «headless domain» удалены —
  Welcome v1.13 требует нативную идентичность карты, их надо заменить одним тестом host+client.
- Состояние до схлопывания: git-тег `pre-collapse`.

## 2026-11-08 — smoke test объединённого бинарника (два процесса, один ПК, TOMB)

`p27.py install` (с `PSE_P27_BUILD_DIR=<билд>/p27`, иначе берётся старый broker) → `pe-play --role host` / `--role client`.
Меню F8 → CREATE/CONNECT, New Game → Normal → карта «Tomb of Ramses» на обеих сторонах (`tools/ui_act.py`, `tools/window_shot.py`).
Результат: загрузчик принял `pse_together.dll` в обоих процессах; клиент подключился (Welcome, MapReady, input domain);
P2 заспавнился на хосте и двигался от ввода клиента (61 шаг по логу, forward=1.0); оба процесса вышли с кодом 0,
хуки восстановлены, оригинал игры и Saved Games не изменены. Нюансы: P2 спавнится, только когда хост-P1 отошёл от якоря
(~>93 ед.) в течение 1800 тиков; прокси хост-P1 на клиенте отказывает (`P4_SPAWN_AABB_OUTSIDE_SECTOR`), когда P1 стоит у границы сектора 259.
