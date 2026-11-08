# pse_together

Единая кодовая база кооператива: одна DLL (`pse_together.dll`), роль хоста или клиента выбирается переменной
`PSE_ROLE` (`pe-play --role host|client` выставляет её сам).

| Часть | Где |
|---|---|
| Протокол, ENet-воркер, мост потоков | `src/net` (`net_protocol.h` — версия протокола) |
| Сессия хоста (ввод P2, снимки, карта) | `src/session/network_input.cpp` |
| Хост / клиент / точка входа DLL | `src/native/host.cpp`, `client.cpp`, `dll_main.cpp` |
| Прокси игрока хоста на клиенте | `src/native/native_proxy.cpp`, `src/proxy/` |
| Меню, лобби, чат, автокарта | `src/native/ui_native.cpp`, `src/ui/` |
| Загрузчик (SDL2-shim, broker, `pe-play`, защита записи) | `src/loader/` |
| Установка копии игры + `host.bat`/`join.bat` | `tools/p27.py install` |
| Смена мира (Restart Level, чекпоинт) | `worldTransition()` в `client.cpp`, `native_proxy::rearm()` |

Сборка и тесты — в `AGENTS.md`. Подробности удалённых форков — в git-теге `pre-collapse`.
