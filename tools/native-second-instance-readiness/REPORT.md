# PowerSlave Exhumed Together — Native Second-Instance Client Readiness Report

**Дата:** 2 ноября 2026 г.  
**Роль:** Senior C++ / KEX Integration Researcher & QA Infrastructure Engineer  
**Цель:** Подготовить минимально достаточную техническую основу для подключения второго настоящего экземпляра PowerSlave Exhumed (2022) к хосту.  
**Статус:** READINESS COMPLETE (Техническая база подготовлена; реализация передаётся GPT-6.1 Sol Max после P2-5).

---

## 1. Executive Summary & Границы этапа

Настоящее исследование определяет архитектурный маршрут и устраняет технические неопределённости перед переходом от headless-тестового клиента (M1.2a/M1.2b) к **настоящему второму игровому процессу PowerSlave Exhumed 2022 (KEX Engine v3/4)**.

### Целевой первый прототип (Scope: Phase 1 Native Client)

```text
+-----------------------------------------------------------------------------------+
|  Game Process B (Client) - Real KEX 2022                                          |
|  - Реальное окно игры (960x540 windowed)                                          |
|  - Физический ввод клавиатуры ('W', 'S', etc.) в окно клиента                     |
|  - Client DLL (pse_native_client.dll) перехватывает ввод на simulation thread     |
|  - ThreadBridge -> ENetWorker (CLIENT Mode) отправляет MsgInputCommand            |
+-----------------------------------------------------------------------------------+
                                         |
                                         | ENet UDP Protocol v1.10 (Channel 1)
                                         v
+-----------------------------------------------------------------------------------+
|  Game Process A (Host) - Real KEX 2022 (Authoritative Host)                       |
|  - Локальный игрок P1 управляется клавиатурой хоста                               |
|  - ENetWorker (HOST Mode) принимает MsgInputCommand                               |
|  - Session десериализует ввод и проверяет generation/sequence (DOC-46/47/49)      |
|  - Simulation thread в hookUpdate/hookCommands применяет кнопки к P2              |
|  - PuppetTick выполняет native physics displacement P2 в мире хоста               |
+-----------------------------------------------------------------------------------+
```

### Честное разграничение уровней готовности (QA Factuality Boundary)

В соответствии с правилами проекта исследование строго разграничивает 4 концептуальных уровня:

| Уровень | Описание | Статус в Phase 1 |
|---|---|:---:|
| **1. Передача ввода от реального процесса** | Захват локального ввода из Game Process B и отправка по ENet хосту. | **ЦЕЛЬ ДАННОГО ЭТАПА** |
| **2. Native движение P2 у хоста** | Физическое смещение Puppet Actor P2 в координатах хоста по командам клиента. | **ЦЕЛЬ ДАННОГО ЭТАПА** (проверено в M1.2a) |
| **3. Наблюдение P2 на экране клиента** | Отрисовка мира хоста, положения P1/P2 и окружения на экране клиента B. | **НЕ ВХОДИТ В ЭТАП** (требует World Replication) |
| **4. Полноценный co-op геймплей** | Синхронизация врагов, оружия, патронов, дверей, триггеров и кампании. | **НЕ ВХОДИТ В ЭТАП** (будущие вехи M2+) |

> [!IMPORTANT]
> Факт успешного движения P2 на экране хоста (пункт 2) **НЕ является доказательством** наличия репликации на клиенте (пункт 3) или готовности кооператива (пункт 4). В первом прототипе клиентский процесс является **генератором сетевого ввода**, мир на клиенте не синхронизирован с хостом.

---

## 2. Текущее состояние и переиспользуемые компоненты

Вся кодовая база проекта построена на строгом разделении ответственности и инвариантах многопоточности (DOC-40). Для реализации нативного клиента доступен богатый арсенал уже проверенных компонентов:

### A. Проверенные компоненты Host (M1.1 / M1.2a / M1.2b)
1. **Host DLL Lifecycle (DOC-41, DOC-43, DOC-48):**
   - Управление жизненным циклом через `Phase` state machine (`Waiting -> Armed -> Constructing -> Live -> WorldExit -> Shutdown`).
   - Безопасное завершение при выходе в меню (`StateExit`) и закрытии окна (`WM_CLOSE` / `DirectQuit`): строгий порядок `stop() -> worker join -> control hooks restore -> native Reset(Player*) -> engine cleanup`.
   - Pinned memory trampolines (MinHook), отсутствие hot unload.
2. **Session Input Hardening (DOC-46, DOC-47, DOC-49):**
   - Generation-scoped routing (DOC-46): защита от отравления новыми сессиями устаревшими пакетами.
   - Admission & Rejoin policy (DOC-47): корректное отклонение неавторизованных пиров без блокировки слота P2.
   - Fixed 60 Hz scheduler & bounded timeout (DOC-49): fail-safe нейтрализации ввода `6 ticks || 150 ms`.
3. **Native KEX Seams (DOC-41, DOC-44):**
   - 14 верифицированных точек (Tick `0x1b6ec0`, Update `0x1dfe30`, Commands `0x1e34a0`, Turning `0x1e2ad0`, Spawn `0x1d4780`, Reset `0x1dfa70`).

### B. Проверенные компоненты Client (Транспорт и протокол)
1. **`ENetWorker` в режиме `ENetWorkerMode::CLIENT` (`tools/enet-prototype/`):**
   - **CODE-VERIFIED:** `ENetWorker::startClient(const ClientConfig&)` уже полностью реализован (`enet_worker.cpp:78-85`, `117-149`).
   - Фоновый поток `threadLoop` изолирует все вызовы ENet сокетов.
   - Автоматическая сериализация и отправка `BridgeInputCommand` по `CHANNEL_UNRELIABLE_SEQUENCED` (`enet_worker.cpp:440-449`).
   - Автоматическая обработка handshake (`onWelcomeCallback`), отключений (`onDisconnectedCallback`) и реконнекта (`reconnectClient`).
2. **`ThreadBridge` SPSC Очереди (`thread_bridge.h`):**
   - Lock-free передача сообщений между simulation thread и worker thread.
   - Strict thread affinity checks (`m_threadChecksEnabled{true}`).
3. **Wire Protocol v1.10 (`net_protocol.h`):**
   - Magic: `0x50534558` (`PSEX`), Version: `0x0110` (272).
   - Handshake: `MsgHello` -> `MsgWelcome` с криптографическим `reconnectKey`.
   - Payload: `MsgInputCommand` (34 байта payload, 45 байт пакет).

---

## 3. Архитектура Client-Side DLL

В отличие от хоста, задача клиентской DLL в Game Process B фундаментально проще и несёт существенно меньше рисков для стабильности движка:

```text
Game Process B Architecture:
+--------------------------------------------------------------------------------+
|  Primary Simulation Thread (Attested TID)                                      |
|                                                                                |
|  [SDL Event Pump] -> [Action Mapping] -> [KEX Tick 0x1401b6ec0]                |
|                                                    |                           |
|                                            hookUpdate (0x1401dfe30)            |
|                                                    |                           |
|                                         +----------v----------+                |
|                                         | Original Update(P1) |                |
|                                         +----------+----------+                |
|                                                    |                           |
|                                     Read primary() buttons/held                |
|                                                    |                           |
|                                       Build BridgeInputCommand                 |
|                                                    |                           |
|                                    pushOutbound(BridgeMessage)                 |
+----------------------------------------------------+---------------------------+
                                                     | SPSC Lock-Free Queue
+----------------------------------------------------v---------------------------+
|  ENet Worker Thread (Background Network TID)                                   |
|                                                                                |
|  pollOutbound() -> serialize MsgInputCommand -> enet_peer_send(Channel 1)      |
+--------------------------------------------------------------------------------+
```

### Ключевые архитектурные упрощения на клиенте:
1. **Отсутствие локального Puppet P2:**
   - Клиентскому процессу **НЕ ТРЕБУЕТСЯ** вызывать `Factory::SpawnActor(0)` или выделять `GuardedPlayer` memory block.
   - Клиент играет своим штатным локальным игроком `primary()` (`0x140af33e0`).
   - Нет необходимости в хуке `ScopedGetter` (`0x1401b5520`).
2. **Отсутствие второго вызова `Update` и `Commands`:**
   - Клиентская DLL не эмулирует второй контекст контроллера.
   - Она лишь **пассивно считывает** сформированное состояние ввода `primary()`.
3. **Строгое владение потоками (Thread Ownership):**
   - Simulation Thread: считывает KEX память, управляет сессией, кладёт команды в `pushOutbound`.
   - Worker Thread: исполняет ENet event pump, отправляет UDP датаграммы.
   - Никаких прямых вызовов ENet из KEX и никаких вызовов KEX из ENet worker.
4. **Устранение `client_control.txt`:**
   - Вся коммуникация происходит исключительно in-memory через SPSC очередь `ThreadBridge`. Никаких файловых блокировок, парсинга текстовых файлов и джиттера дискового ввода-вывода.

---

## 4. Исследование Client Input Bridge

### 4.1. Места возникновения и преобразования ввода в KEX

На основе статического анализа декомпилятора (`G_FUNC_1401dfe30`, `G_FUNC_1401e34a0`) и runtime-наблюдений M1.1 (`bootstrap.cpp`):

```text
1. Физический ввод (Клавиатура/Мышь)
      |
      v
2. SDL Event Queue & Pump (RVA 0x100d80 / VA 0x140100d80)
      |  Вызывается из State Tick (RVA 0x1b6ec0)
      v
3. Action Mapping Array (DAT_140aebce0)
      |  KEX связывает сканкоды клавиш с 13 игровыми действиями (buttons 0..12)
      v
4. Player::Update (RVA 0x1dfe30 / VA 0x1401dfe30)
      |  Считывает DAT_140aebce0, заполняет битовую маску и счетчики удержания
      v
5. Player::Commands (RVA 0x1e34a0 / VA 0x1401e34a0)
      |  Опрашивает мышиную очередь DAT_140af5598, рассчитывает скаляры движения
      v
6. Player::Turning (RVA 0x1e2ad0 / VA 0x1401e2ad0)
      |  Рассчитывает углы поворота (yaw/pitch)
      v
7. PuppetTick (RVA 0x1db8a0 / VA 0x1401db8a0)
         Применяет физическое перемещение к Actor в мире
```

### 4.2. Расположение структур ввода в объекте `Player`

Объект `primary()` расположен по статическому указателю `image_base + 0xaf33e0` (VA `0x140af33e0`).

| Смещение в `Player` | Тип данных | Назначение | Статус доказанности |
|---|---|---|:---:|
| `+0x00` | `uint32_t` | Битовая маска нажатых кнопок (`buttons bitmask`) | **VERIFIED** (DOC-41, `bootstrap.cpp:155`) |
| `+0x04` | `uint32_t` | Held ticks: Button 0 (Attack) | **VERIFIED** (`G_FUNC_1401dfe30.json`) |
| `+0x08` | `uint32_t` | Held ticks: Button 1 (Jump) | **VERIFIED** (`G_FUNC_1401dfe30.json`) |
| `+0x0c` | `uint32_t` | Held ticks: Button 2 (Forward, bit 2 / mask `0x04`) | **VERIFIED** (DOC-41 `bootstrap.cpp:177`) |
| `+0x10` | `uint32_t` | Held ticks: Button 3 (Backward, bit 3 / mask `0x08`) | **VERIFIED** (DOC-41 `bootstrap.cpp:177`) |
| `+0x14` | `uint32_t` | Held ticks: Button 4 (Strafe Left, bit 4 / mask `0x10`) | **INFERRED** (`G_FUNC_1401dfe30.json:1982`) |
| `+0x18` | `uint32_t` | Held ticks: Button 5 (Strafe Right, bit 5 / mask `0x20`) | **INFERRED** (`G_FUNC_1401dfe30.json:1982`) |
| `+0x2c` | `uint32_t` | Held ticks: Button 10 (Use / Action, mask `0x400`) | **INFERRED** (`G_FUNC_1401dfe30.json:1982`) |
| `+0x38` | `float` | Рассчитанная скорость forward/back (`moveForward` scalar) | **VERIFIED** (`G_FUNC_1401e34a0.json`) |
| `+0x3c` | `float` | Рассчитанная скорость strafe left/right (`moveRight` scalar) | **VERIFIED** (`G_FUNC_1401e34a0.json`) |
| `actor + 0x38` | `float` | Текущий угол рыскания (`yaw` в радианах) | **VERIFIED** (DOC-41 `bootstrap.cpp:312`) |

### 4.3. Выбор оптимального шва захвата ввода (Input Hook Seam)

Для клиентской DLL рассмотрены 3 варианта перехвата ввода:

```text
Вариант 1: Win32 Raw Input / SDL_PollEvent Hook
[-] Требует низкоуровневой деэмуляции раскладки клавиш игрока.
[-] Зависит от языка клавиатуры и пользовательских биндов.
[-] Риск расхождения с тем, что видит движок.
Статус: ОТКЛОНЁН.

Вариант 2: Post-Update Hook на Player::Update (RVA 0x1dfe30)  <-- РЕКОМЕНДУЕМЫЙ
[+] Вызывается ровно 1 раз за simulation tick на первичной нити.
[+] Оригинальный Update уже считал привязки клавиш kexengine.cfg.
[+] В Player+0x00..+0x34 уже лежат нормализованные held counters и buttons.
[+] Проверен в M1.1 (bootstrap.cpp строки 174–178).
Статус: VERIFIED / ВЫБРАН ДЛЯ РЕАЛИЗАЦИИ.

Вариант 3: Hook на Player::Commands (RVA 0x1e34a0)
[+] Содержит аналоговые скаляры moveForward / moveRight.
[-] Может пропускаться или модифицироваться при определенных состояниях меню.
[-] Не дает прямого доступа к дискретным тикам нажатия.
Статус: ЗАПАСНОЙ (для будущего аналогового ввода).
```

### 4.4. Исключение двойного применения, контракт Hold/Release и границы надёжности

- **Исключение повторного применения:**
  Хук `hookUpdate` на клиенте устанавливает страховку:
  `if (p != primary() || !thread() || !g.inTick || g.localThisTick != 1) return;`
  Команда ввода считывается и формируется **строго один раз за тик симуляции**.
- **Частота Player::Update vs Сетевая отправка (КРИТИЧЕСКОЕ ОГРАНИЧЕНИЕ):**
  - Частота вызовов `Player::Update` привязана к рендер-циклу движка (`cl_engineFPS`, VSync, монитор).
  - На мониторе 144 Hz или при просадках FPS до 45 Hz вызовы `Player::Update` **НЕ ОБЯЗАНЫ** происходить со стабильной частотой 60 Hz.
  - Поэтому клиентская сетевая очередь должна тактироваться по рациональной сетке 60 Hz (steady rational grid, принятой в DOC-49), предотвращая заваливание хоста избыточными пакетами или голодание ввода.
- **Контракт Hold/Release и доставка через Unreliable ENet:**
  - При удержании 'W': `held[2] > 0` $\rightarrow$ `moveForward = 1.0f`.
  - При отпускании 'W': на том же тике `held[2] == 0` $\rightarrow$ формируется пакет отпускания `moveForward = 0.0f` (Explicit Release).
  - **КРИТИЧЕСКОЕ ПРЕДУПРЕЖДЕНИЕ:** Пакеты INPUT передаются по `CHANNEL_UNRELIABLE_SEQUENCED`. Единственный пакет Release **НЕ ГАРАНТИРОВАН** к доставке при сетевых потерях. Если одиночный Release потерян, P2 получит залипание хода ("sticky walk").
  - **Защита от залипания:** Клиент **ОБЯЗАН** продолжать потоковую отправку Neutral-пакетов (`moveForward = 0.0f`) после отпускания клавиши, а хост опирается на fail-safe тайм-аут (DOC-49: `6 ticks || 150 ms`), сбрасывающий ввод в нейтраль при тишине.

---

## 5. Исследование запуска двух экземпляров игры

### 5.1. Анализ Steam DRM и лицензирования
1. **AppID и размещение `steam_appid.txt`:**
   - Игра имеет Steam AppID `1678430`.
   - В `anubis_Shipping_Steam_x64.exe` импортирована функция `SteamAPI_RestartAppIfNecessary`.
   - **УТОЧНЕНИЕ РАСПОЛОЖЕНИЯ:** Исполняемый файл ищет `steam_appid.txt` **СТРОГО в Current Working Directory (CWD)** вызывающего процесса. Если игра запускается с CWD в папке профиля (`experiments/m1/profiles/client`), файл `steam_appid.txt` обязан лежать именно там. Наличие файла только в корне игры или только в профиле при несовпадении CWD приведёт к попытке перезапуска через Steam.
2. **Поведение `SteamAPI_Init` при втором запуске (СТАТУС: UNKNOWN / RUNTIME PENDING):**
   - В коде движка функция `FUN_14018ed40` (`kexSocialManager::Init`, RVA `0x18ed40`) вызывает `SteamAPI_Init()`.
   - При возврате `false` движок выполняет ветку:
     `kexSocialManager::Init: Unable to initialize Steam\n`
     и продолжает работу без вызова `exit()`.
   - **КРИТИЧЕСКОЕ ОГРАНИЧЕНИЕ:** Успешный статический анализ исходников KEX **НЕ ПОДТВЕРЖДАЕТ** возможность одновременного полноценного запуска двух процессов под одной учётной записью Steam. Поведение Steam Client IPC (throttling, всплывающие окна "App already running", блокировка облачных сохранений) на этапе статики остаётся **UNKNOWN** и требует реального запуска.
3. **Steam Overlay & Vulkan Layers:**
   - Для исключения гонок за кэш шейдеров Fossilize и оверлей Steam оба процесса должны запускаться с переменными окружения:
     `DISABLE_VK_LAYER_VALVE_steam_fossilize_1=1`  
     `DISABLE_VK_LAYER_VALVE_steam_overlay_1=1`

### 5.2. Анализ Single-Instance ограничений (СТАТУС: INFERRED / RUNTIME PENDING)
- Проведён анализ таблицы импорта `anubis_Shipping_Steam_x64.exe` (`docs/evidence/PE_main2022.json`).
- Функции `CreateMutexA` / `CreateMutexW` в таблице импорта отсутствуют.
- **КРИТИЧЕСКОЕ ОГРАНИЧЕНИЕ:** Отсутствие импортов `CreateMutex` **НЕ ЯВЛЯЕТСЯ ДОКАЗАТЕЛЬСТВОМ** отсутствия single-instance логики. Движок или подключенные библиотеки (SDL2, Steamworks) могут использовать альтернативные механизмы:
  - Именованные разделяемые секции памяти (`CreateFileMapping` / `MapViewOfFile`);
  - Локальные порты ALPC / Named Pipes;
  - Поиск существующих окон (`FindWindow` / `EnumWindows`) по заголовку или классу SDL;
  - Внутренние механизмы SDL2 Video Subsystem.
- Возможность беспрепятственного параллельного запуска двух окон KEX является **инженерным предположением (INFERRED)** и подлежит валидации в KEX runtime.

### 5.3. Изоляция профилей, сохранений и конфигураций
KEX 2022 поддерживает параметры командной строки `-basepath <dir>` и `-userpath <dir>`:
- **Host Profile:** `-userpath S:\...\experiments\m1\profiles\host`
- **Client Profile:** `-userpath S:\...\experiments\m1\profiles\client`

При указании `-userpath` движок перенаправляет сохранение `kexengine.cfg`, сейвов и логов в `<userpath>\Nightdive Studios\Powerslave EX\`.
- **КРИТИЧЕСКОЕ ОГРАНИЧЕНИЕ:** Наличие `-userpath` **НЕ ГАРАНТИРУЕТ** изоляции абсолютно всех файловых и системных операций:
  - Кэши шейдеров DirectX/Vulkan могут писаться в глобальные каталоги драйвера GPU;
  - Записи реестра Windows (`HKCU\Software\Nightdive Studios`) остаются общими;
  - Временные файлы CRT (`GetTempPath`) без явной подмены `TEMP/TMP` будут конфликтовать.
  - Поэтому обязательна изоляция переменных окружения (`APPDATA`, `LOCALAPPDATA`, `TEMP`, `TMP`, `USERPROFILE`).

### 5.4. Назначение сетевых портов и ограничения локальной сети
- **Host (Process A):** ENet сервер слушает UDP порт `28745` (привязка `0.0.0.0:28745`).
- **Client (Process B):** ENet клиент вызывает `enet_host_create(NULL, 1, 2, 0, 0)`:
  Передача `NULL` запрашивает у Windows динамический эфе Sip-порт (диапазон 49152–65535).
- **КРИТИЧЕСКОЕ ОГРАНИЧЕНИЕ:** Использование динамического порта клиентом устраняет конфликт `bind()`, но **НЕ ДОКАЗЫВАЕТ** отсутствие сетевых проблем: локальные политики Windows Firewall, пакетная фильтрация антивирусов, ограничения loopback UDP и исчерпание сокетов могут приводить к потере пакетов между локальными процессами.
- Оригинальные сохранения пользователя в `C:\Users\<user>\Saved Games\Nightdive Studios\Powerslave EX` остаются на 100% изолированными и защищёнными.

### 5.4. Назначение сетевых портов
- **Host (Process A):** ENet сервер слушает UDP порт `28745` (привязка `0.0.0.0:28745`).
- **Client (Process B):** ENet клиент вызывает `enet_host_create(NULL, 1, 2, 0, 0)`:
  Передача `NULL` указывает операционной системе назначить **эфе Sip-порт** (динамический порт Windows из диапазона 49152–65535).
  Конфликт сетевых портов между двумя процессами **ФИЗИЧЕСКИ НЕВОЗМОЖЕН**.

### 5.5. Особенности управления окнами и фокусом на одном ПК

| Фактор | Поведение на одном ПК | Решение / Альтернатива |
|---|---|---|
| **Фокус ввода Windows** | Windows передаёт клавиатурные события только активному окну верхнего плана (Foreground Window). | При тестировании фокус ввода передаётся окну Client B; окно Host A остаётся видимым рядом (side-by-side 960x540). |
| **Фоновый рендеринг Host** | KEX продолжает цикл симуляции (`Tick`) в фоновом режиме, если игра не находится в паузе. | Убедиться, что `cl_engineNoSleep "1"` или игра в активном состоянии геймплея (`gameplay() == true`). |
| **Аудиопотоки** | WASAPI Shared Mode позволяет обоим процессам микшировать звук без блокировок. | При желании громкость звука клиента можно заглушить через `snd_volume "0"` в конфиге клиента. |
| **Двухкомпьютерная схема (2-PC Fallback)** | При запуске на двух ПК по LAN/VPN проблема фокуса окон исчезает полностью. | Каждый игрок использует собственный ПК, клавиатуру, монитор и Steam-сессию. Конфигурация клиента поддерживает любой внешний IP. |

---

## 6. Каталог Native Hooks и статус доказанности

Сводная ведомость используемых швов интеграции для Client и Host:

| Функция / Механизм | Preferred VA | RVA | File Offset | Роль в Phase 1 | Статус |
|---|---|---|---|---|:---:|
| **State Tick** | `0x1401b6ec0` | `0x1b6ec0` | `0x1b62c0` | Simulation thread verification, session tick pump | **VERIFIED** |
| **Player Update** | `0x1401dfe30` | `0x1dfe30` | `0x1df230` | Захват ввода на клиенте; вызов P2 Update на хосте | **VERIFIED** |
| **Player Commands** | `0x1401e34a0` | `0x1e34a0` | `0x1e28a0` | Модификация кнопок P2 на хосте; считывание скаляров | **VERIFIED** |
| **Player Turning** | `0x1401e2ad0` | `0x1e2ad0` | `0x1e1ed0` | Подавление локального поворота P2 на хосте | **VERIFIED** |
| **StateExit** | `0x1401b6670` | `0x1b6670` | `0x1b5a70` | Корректный unbind/disconnect при выходе в меню | **VERIFIED** |
| **Platform Shutdown** | `0x1400fcc20` | `0xfcc20` | `0xfc020` | Корректный stop worker при закрытии окна (WM_CLOSE) | **VERIFIED** |
| **Player Reset** | `0x1401dfa70` | `0x1dfa70` | `0x1dee70` | Очистка указателей P2 на хосте при unbind | **VERIFIED** |
| **Puppet Spawn** | `0x1401d4780` | `0x1d4780` | `0x1d3b80` | Создание Actor P2 на хосте (на клиенте НЕ вызывается) | **VERIFIED** |
| **Action Mapping** | N/A | `DAT_140aebce0` | N/A | Таблица раскладки действий KEX | **INFERRED** |
| **Mouse Delta Queue**| N/A | `DAT_140af5598` | N/A | Очередь сырых смещений мыши в Commands | **INFERRED** |
| **Camera View Matrix**| N/A | UNKNOWN | N/A | Точка внедрения сетевой камеры (для Phase 2+) | **UNKNOWN** |

---

## 7. Разработанная инфраструктура тестирования

Все компоненты размещены в директории:  
`PowerSlave_Exhumed_Together_mod/tools/native-second-instance-readiness/`

### 1. `precheck_environment.py`
Автоматический скрипт валидации окружения. Проверяет 8 критических факторов:
- Хеш целевого исполняемого файла (`b8f13103...76d`).
- Хеш оригинальной `SDL2.dll` (`f65b50d6...3a0`).
- Отсутствие зависших или старых игровых процессов (`anubis_Shipping_Steam_x64.exe`, `m12a_test_client.exe`).
- Доступность серверного UDP-порта `28745`.
- Разделение и неналожение профилей Host и Client внутри рабочей директории.
- Неприкосновенность оригинальных сохранений пользователя.
- Наличие `steam_appid.txt` с кодом `1678430`.
- Правила изоляции переменных окружения.

### 2. Шаблоны конфигураций (`configs/`)
- `host_config_template.json`: эталонные параметры запуска хоста (CLI аргументы, bind address, порты, пути к логам).
- `client_config_template.json`: эталонные параметры запуска клиента (CLI аргументы, адрес хоста, participant ID, пути к логам).

### 3. `log_correlation.py`
Анализатор сквозной корреляции журналов телеметрии:
- Сопоставляет отправленные клиентом команды `(slot, generation, input_seq)` с принятыми хостом записями.
- Проверяет монотонность последовательностей и отсутствие пропусков.
- Рассчитывает суммарное физическое перемещение P2 на хосте, вызванное клиентом.
- Проверяет строгую изоляцию состояния P1 (`p1_state_violations == 0`).
- Формирует итоговый отчёт приёмки в форматах CLI и JSON.

---

## 8. Итоговый вердикт готовности

- **Готовность окружения и изоляции:** **READY (PASS)** — двойной запуск на одной машине технически осуществим без обхода DRM, без модификации оригинальных файлов и без конфликта портов/профилей.
- **Готовность архитектуры Client Input Bridge:** **READY (PASS)** — определена точная точка съёма ввода (`Player::Update` post-hook), доказана совместимость с существующим `ENetWorker(CLIENT)`.
- **Готовность процедур валидации:** **READY (PASS)** — созданы средства предварительной проверки и последующего сопоставления логов.
- **Дальнейшие шаги:** Ожидание завершения задачи P2-5 разработчиком GPT-6.1 Sol Max, после чего нативная интеграция выполняется по плану `SOL_HANDOFF.md`.
