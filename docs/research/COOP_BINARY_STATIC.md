# Отдельный бинарник PowerSlave EX Co-Op Edition

> Финальный срез: отдельный x86 импорт завершился и сохранил COOP2015 project, G_INVENTORY, G_STRINGS_XREFS, G_ENTRYPOINTS и 20 dossiers entry/export paths в `docs/evidence/ghidra_coop2015`. Entry VA 0x00507761, RVA 0x107761 действительно декомпилирован. Эти primitive exports/entry не доказывают gameplay/net semantics. Дальнейший анализ этого референса остановлен по актуальному ограничению scope; ниже строковые hints и README отделены от runtime UNKNOWN.

Исследуется только поставленный `S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_EX_-_CO-OP_Edition/anubis.exe`. Он не является исполняемым файлом Exhumed 2022. Исполнение игры, подключение к сети, patching и injection не выполнялись. Файлы референса не изменялись.

## Идентичность и источники

VERIFIED по [PE_coop2015.json](../evidence/PE_coop2015.json): SHA-256 `488def7a7d0f676add9e2cf188639e13927a52cd6e27e58ab90fe48d4de1990f`, размер 3 762 176 байт, PE32/x86 (`Machine=0x14c`, optional header `0x10b`), preferred image base `0x400000`, entry RVA `0x107761`, preferred VA `0x507761`, file offset `0x106b61`. DYNAMIC_BASE установлен; фактический runtime image base неизвестен. Ни один preferred VA этого документа не является адресом исполняемого процесса.

VERIFIED: supplied kit имеет три файла, в `john.kpf` только три sprite members; C/C++ исходников модификации в этом каталоге нет. Это ограниченная проверка данной поставки, а не утверждение об отсутствии исходников где-либо ещё. [REFWORLD_COOP_INVENTORY](../evidence/REFWORLD_COOP_INVENTORY.json)

VERIFIED статическое наблюдение импортов: `OpenAL32.dll`, SDL2, FFmpeg libraries, OPENGL32, **WS2_32.dll**, USER32, ADVAPI32 и KERNEL32. WS2_32 содержит 19 import entries: два по именам `getaddrinfo`/`freeaddrinfo`, остальные по ordinals. Для raw ordinals в PE-манифесте не подставляются придуманные имена. Наличие импортов не доказывает успешную сетевую сессию.

## Строки, которые действительно присутствуют в EXE

[COOP_STRING_ANCHORS](../evidence/COOP_STRING_ANCHORS.json) — ограниченная выборка из [STRINGS_coop2015](../evidence/STRINGS_coop2015.json). Исходный сканер фиксирует ASCII runs и ASCII-подмножество UTF16LE; это не exhaustive Unicode inventory. VA/RVA/offset строк — координаты байтов, **не адреса функций**.

| Preferred VA / RVA / file offset | VERIFIED: строковый anchor | Допустимый вывод |
|---|---|---|
| `0x5482b4` / `0x1482b4` / `0x1470b4` | `-coopbots` | INFERRED: предусмотрен путь тестовых игроков; алгоритм спауна нужно проверять по xrefs/функции. |
| `0x5482c0` / `0x1482c0` / `0x1470c0` | `Co-op test: %d bot player(s)` | Число в сообщении не устанавливает число работающих независимых игроков. |
| `0x548588` / `0x148588` / `0x147388` | Level-start log с slot, player count, input delay ticks | INFERRED: per-slot и tick/input-delay механизм; формат protocol/runtime semantics ещё не доказан одной строкой. |
| `0x548708` / `0x148708` / `0x147508` | Tick log с checksum, 60 ticks/s, stalled frames, input delay, prediction error и четырьмя artifact values | INFERRED: проверка симуляции/предсказания; по строке нельзя установить lockstep, rollback, host authority или tick cadence во время работы. |
| `0x548544` / `0x148544` / `0x147344` | `VERSION MISMATCH - USE THE SAME BUILD` | Есть сообщение о несовпадении версии; алгоритм и охват сравнения неизвестны. |
| `0x548824` / `0x148824` / `0x147624` | `.coop` | Есть sidecar suffix; формат, ключи игроков, атомарность записи и совместимость UNKNOWN до анализа функций. |
| `0x789350` / `0x389350` / `0x387750` | RTTI `.?AVkexNetWaitLoop@@` | VERIFIED наличие type-name bytes. Жизненный цикл этого типа требует анализа конструктора/vtable/call sites. |

VERIFIED: найдены двадцать ASCII path strings вида `D:\Mods\pslavex\src\kex3_anubis\source\...cpp`, включая `game/game.cpp`, `game/world.cpp`, framework/session.cpp. Это встроенный текст вероятного пути сборки, не доступные файлы исходников и не аутентифицированная provenance. Поиск по этой выборке не обнаружил собственного `net.cpp`, но такой ограниченный отрицательный результат не даёт карту файлов или функций проекта.

## Что сообщает README отдельно от EXE

[REFWORLD_COOP_README](../evidence/REFWORLD_COOP_README.json) содержит заявления автора: public beta 2015, до четырёх игроков, UDP26100, host-controlled map/save, UPnP, spectator после смерти, team inventory, voice/chat. Их присутствие в README — VERIFIED; работоспособность во время исполнения — UNKNOWN. Строки и импорты EXE независимо подтверждают наличие сетевых/кооперативных элементов в артефакте, но не все обещания README.

## Перенос на Exhumed 2022

INFERRED: этот EXE полезен как источник проверяемых идей разделения player slots, world events, controller/input и сохранений. Прямое использование его адресов, ABI, C++ layout или бинарных patch bytes для x64 Exhumed 2022 недопустимо: измеренная архитектура здесь **x86**, а целевая игра анализируется отдельно. Наличие coop в старой ветви не доказывает возможность создать вторую независимую player entity в конкретной сборке 2022.

UNKNOWN: исходный неизменённый 2015 binary baseline, точное соответствие открытому дереву исходников и все изменения автора мода. Для сравнения функций нужен baseline с SHA-256; source similarity и одинаковые строки недостаточны для установления двоичного соответствия.

