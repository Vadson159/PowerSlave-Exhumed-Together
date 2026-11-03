# Бинарный манифест

VERIFIED: основной EXE обнаружен по фактическому файлу и PE-заголовкам, а не по имени продукта в ресурсах. Полные таблицы импортов/экспортов, PE-секции, debug directory, флаги и времена файлов сохранены в [E005_BINARY_MANIFEST.json](evidence/E005_BINARY_MANIFEST.json). Версии Windows FileVersionInfo — в [E008](evidence/E008_WINDOWS_VERSION_INFO.json). Прочитано без загрузки/исполнения DLL.

| Объект | Размер, байт | PE | SHA-256 |
|---|---:|---|---|
| `anubis_Shipping_Steam_x64.exe` | 11051520 | x86_64 | `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d` |
| `SDL2.dll` | 1561088 | x86_64 | `f65b50d693484d5d5a2bb8df1cf520628dd744e99e9a937bb936839b990943a0` |
| `steam_api64.dll` | 264480 | x86_64 | `67b094d16ab0f1216ed94fc95db1281b75c2ebf1e71e583bb0545a0fb73d7765` |
| `xaudio2_9redist.dll` | 846720 | x86_64 | `ccc5357be7353c42c4d5e86c5fc354d0f5d204d3875c56c0d76ee4b9f2de2151` |
| Референс `PowerSlave_EX_-_CO-OP_Edition\anubis.exe` | 3762176 | x86 | `488def7a7d0f676add9e2cf188639e13927a52cd6e27e58ab90fe48d4de1990f` |

Первые четыре файла находятся в корне установленной игры. Референсный EXE находится в каталоге источников. Полные абсолютные пути и ISO UTC LastWriteTime/creation time содержатся в JSON, а не сокращены до имён. Дата копирования/установки и COFF timestamp не доказывают дату выпуска. У главного EXE и co-op EXE FileVersion/ProductVersion отсутствуют (`null`); номер патча 2022 этим исследованием не установлен. SDL2 сообщает 2.0.14.0, Steam API — FileVersion 06.06.99.59, XAudio — 1.0.2008.23001; это данные их version resources, без проверки происхождения.

Координаты главного EXE:

| Поле | Значение |
|---|---|
| Machine / OptionalHeader | AMD64 `0x8664` / PE32+ `0x20b` |
| Preferred image base | `0x140000000` |
| Entry RVA / preferred VA | `0x454a70` / `0x140454a70` |
| Entry file offset | `0x453e70` |
| DYNAMIC_BASE | false по PE-заголовку |
| Координаты co-op EXE | x86 PE32, image base `0x400000`, entry RVA `0x107761`, file offset `0x106b61` |

VERIFIED: EXE экспортирует 21 имя, включая `asCreateScriptEngine`, `asGetLibraryVersion`, `asGetActiveContext`, `asAllocMem` и два GPU preference data symbol. Это сильный статический якорь AngelScript. Внешнего DLL с реализацией gameplay в обнаруженной поставке нет: найдены EXE и три DLL, перечисленные выше. Это не исключает динамические загрузки системных/других компонентов.

VERIFIED: основной EXE импортирует SDL2 (99 символов), Steam API (11), XAudio2 redist (4), D3D11/DXGI/D3DCompiler, Win32/CRT, WS2_32 (13). Эти зависимости указывают на платформенный ввод/окно, звук, графику и сетевые возможности библиотеки; INFERRED — конкретное применение. WS2_32 и строки `kexNetwork` не доказывают готовую многопользовательскую симуляцию. Co-op EXE использует x86 SDL2/OpenAL/OpenGL/FFmpeg и WS2_32 (19), поэтому бинарные адреса и C++ layouts нельзя переносить из него в x64 2022.

VERIFIED: debug directory содержит RSDS/PDB path `D:\SupFiles\Coding Projects\Nightdive\powerslaveex\kex3_anubis\bin\x64\anubis_Shipping_Steam_x64.pdb`, GUID bytes и age=2. Это строка из бинарника, не существующий доступный исходник/PDB; Ghidra сообщает, что PDB не найден.

Игровые данные: [E006_GAME_FILES.json](evidence/E006_GAME_FILES.json) — полный обнаруженный список; [E007_KPF_INVENTORY.json](evidence/E007_KPF_INVENTORY.json) — 2194 ZIP-члена `PowerslaveEX.kpf`, размер архива 169831036, SHA-256 `d34b63551713bf46c4215632d9b5f92b452791023937fe7d22a196bff9ff4bbd`. CRC всех членов не проверялись; для 22 выбранных текстовых файлов чтение/декомпрессия/CRC проверены, member SHA-256 сохранены в [E010](evidence/E010_KPF_SELECTED_TEXT.json). Вне KPF есть movies/music и документация с картами-примерами; формат крупных map payloads пока не разобран.

Окружение/ограничения: [E011](evidence/E011_ENVIRONMENT.json), [TOOLCHAIN_BLOCKERS.md](research/TOOLCHAIN_BLOCKERS.md). MCP REA 6.7.0 доступен, его Ghidra provider не прошёл проверку; прямой Ghidra fallback учитывается отдельно. Не путать наличие инструмента с успешной декомпиляцией.
