# П23 — UI/session и native candidate

**REVIEW, режим В; финальная native приёмка отдельно.** Решение пользователя
2026-11-06: отдельная v1.13 поверх v1.12, независимая приёмка. Прежние версии
не менять. [Отчёт DOC-84](../../../../docs/84_P23_UI_LOBBY_CHAT_AUTOMAP.md).

- `model.hpp`: поля меню (имя, числовой IPv4/bind, порт, token), список до 8
  игроков, bounded notices/chat64, фильтрация UTF-8/поколений/эпох, value-only
  маркеры союзников с отдельным local slot, цветами/DEAD/elevation/stale filter.
- `protocol.hpp`: strict codecs Request/Broadcast/Roster/Pose; недопустимые строки
  отвергаются до сериализации, не обрезаются; parser проверяет trailing bytes.
- `lobby.hpp`: реальный ENet bridge-mode lobby/chat без mock gameplay ticks.
  Автор чата определяется только authenticated peer ledger. 2 сообщения/секунду
  на поколение, дедупликация request/broadcast, полный roster для late join,
  replay до 64 строк **только от ещё актуальных slot/generation/name**.
- `../wire113`: новые изолированные headers и четыре изменённых `.cpp`; остальные
  source files компилируются из принятой v1.12. Директории v1.10–v1.12 неизменны.

Wire additions (little endian, общий header PSEX/0x0113/тип/u32 sequence):

| ID | Направление | Payload |
|---|---|---|
| 0x60 | authenticated client → host | u64 request seq, u32 connection gen, u16 byte length + UTF-8 text |
| 0x61 | host → authenticated clients | u64 host seq, u8 slot, u32 gen, string31 name, string360 text |
| 0x62 | host → authenticated clients | u64 roster seq, u32 epoch, u8 count≤8; slot/u32 gen/string31 name/u8 state/u16 ping |
| 0x63 | host → authenticated clients | u8 slot, u32 generation/epoch/sequence, float x/y/z/yaw radians, u64 host sampleNs |

Чат/roster — reliable channel0, Pose — unreliable sequenced channel1. Проверяются
направление, канал и ENet reliable flag до callback. Pose не создаёт reliable
очередь позиций; client TTL использует локальное время получения, без сравнения
часов разных компьютеров. Абсолютный возраст отправленного пакета без clock sync
не установлен. Хост дополнительно проверяет свой sampleNs на собственных часах.
Чат: ≤90 Unicode scalar values и ≤360 UTF-8 bytes; имя ≤31 bytes. Participant ID:
ASCII letters/digits/underscore/hyphen,1…48. Повторный HELLO аутентифицированного
peer не меняет ledger. Ключ реконнекта сохраняется только для того же endpoint
и participant; при смене scope очищается. Новый host start получает новый session
ID и чистый ledger; terminal client transport уничтожается после receive-loop.

WELCOME v1.13 сохраняет поля v1.12, но **после mapName** добавляет u8
`nativeIdentityPresent`. Если 1 — далее прежняя NativeMapStructure; если 0 —
структура отсутствует и mapName обязан быть `LOBBY`. Затем u64 reconnect key.
Ненулевая native identity для настоящей карты по-прежнему обязательна. Stage
`LOBBY` не означает MapReady и не разрешает native gameplay/Spawn. Ни fake TOMB
counts, ни автоматический downgrade не используются.

Потоки: endpoints/Model принадлежат NETWORK; `shared_p23.hpp` передаёт копии Model
через mutex, команды чата — bounded queue16. Sim/UI не вызывают ENet. Native
worker получает позы и Alive/Dead из принятого post-world snapshot хоста.
Public `transport()` — диагностический seam, не thread-safe API.
Свежий UI consumer после потери сессии начинает с reset, очищая старые сообщения,
слоты и маркеры.

Проверки:

```powershell
python -X utf8 -B experiments/pse_together/tools/run_offline.py --build-dir b84 --out <NEW_WORKSPACE_DIR>
ctest --test-dir b84 -C Release -R '^p23_' --output-on-failure
```

CMake выбирает реально назначенный Preferred non-loopback IPv4 только для
one-PC ENet smoke и передаёт его через `P23_TEST_LOCAL_IP`. Для отдельного запуска
`p23_lobby_live.exe` эту переменную нужно задать самому. Нет удалённого peer,
изменения firewall, публичного сервера или доказательства П5/двух ПК.

Native adapter `../native/p23/ui_native.cpp`: F8 форма, T/Enter/Esc чат,
удержание backslash — таблица. Собственный GDI bitmap в D3D11 deferred command
list; ExecuteCommandList(TRUE) сохраняет immediate context игры. Только D3D11
HUD; Vulkan HUD не реализован. Native Arrow — точный local-call seam, после
оригинальной стрелки рисует союзников из value-only Model. Новых Actor нет.
Диагностические DLL сохраняют принятый scope двух native игроков/TOMB/одной
жизни мира, разные роли host/client. 8endpoint — отдельная ENet проверка.
Profile-guard permit разрешает только public интерфейс; native gameplay требует
полного isolation+Sim tick+M11PermitTest+MapReady. StateExit восстанавливает Arrow,
очищает сессию и оставляет public UI до DirectQuit. Новая сессия требует restart.
Реальные screenshots/raw observations/restores: docs/evidence/m1/P23_NATIVE.
Открыта независимая приёмка В финального native кандидата.
Не подключать пример native ABI из DOC-12: третий параметр Arrow — float/XMM2;
поправки и точные VA/RVA/file offsets — DOC-84.
