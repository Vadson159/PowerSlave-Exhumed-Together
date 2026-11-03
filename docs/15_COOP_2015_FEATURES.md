# PowerSlave Exhumed Together — 2015 Co-Op Edition Mechanics Breakdown

**Document ID:** `DOC-15-COOP-2015-FEATURES`  
**Classification:** Reference Analysis & Feature Specification  
**Reference Binary:** `PowerSlave_EX_-_CO-OP_Edition\anubis.exe` (SHA-256 `488def7a7d0f676add9e2cf188639e13927a52cd6e27e58ab90fe48d4de1990f`)  
**Reference Source:** `PowerslaveEX-master-source-code`  
**Scope:** Deconstruction of core co-op gameplay systems from the 2015 Co-Op Edition (Spectator Mode, Inventory Persistence `.coop`, Late-Join Protocol, Scoreboard/Chat HUD, Spatial Voice Chat) and definitive architectural decisions for PowerSlave Exhumed Together.

---

## 1. Executive Summary & Architectural Decisions

The 2015 *PowerSlave EX Co-Op Edition* (developed for the public beta of *PowerSlave EX*) provided a complete 4-player cooperative campaign. A detailed reverse engineering and string analysis of `PowerSlave_EX_-_CO-OP_Edition\anubis.exe` reveals mature design patterns for handling player death, level transitions, inventory state persistence, and in-game communication.

Following explicit project requirements, the following definitive architectural determinations have been established for *PowerSlave Exhumed Together* (2022):

1. **Voice Chat:** **ПОЛНОСТЬЮ ИСКЛЮЧЁН (EXCLUDED).** Никакой встроенный голосовой чат (пространственный или обычный) не разрабатывается и не планируется. Игроки используют Discord или другие внешние VoIP-решения.
2. **Spectator Mode:** **ПОЛНОСТЬЮ ИСКЛЮЧЁН (REJECTED).** Режим наблюдения после гибели отклонён. Переключение камеры на союзников (`spectatorTarget`, chase camera) не реализуется.
3. **Ручное возрождение по клавише E:** **ПРИНЯТО (ACCEPTED).** После гибели персонажа игрок остаётся на экране смерти и возрождается на ТЕКУЩЕМ уровне по нажатию клавиши E (см. отдельный документ [`16_RESPAWN_DESIGN.md`](16_RESPAWN_DESIGN.md)).
4. **Сброс оружия при смерти:** При возрождении арсенал игрока не сохраняется, а сбрасывается до стандартного начального набора текущей кампании.

---

## 2. Spectator Mode on Death (2015 Reference)

### 2.1 2015 Historical Implementation Mechanics

*Verified Binary Strings (`anubis.exe` at offset `0x147600`–`0x147800`):*
- `%s DROWNED`
- `%s BURNED UP`
- `%s WAS KILLED BY %s`
- `(DEAD)`
- `player/john_death`

В моде 2015 года при падении здоровья до 0 или ниже:
1. **Death Sequence:** Игрок воспроизводил `player/john_death`. Коллизия отключалась (`COLLISION_NONE`), регистрировался некролог.
2. **Camera Uncoupling:** Локальная камера отвязывалась от высоты головы погибшего и переключалась на камеру выжившего союзника от третьего/первого лица.
3. **Target Cycling:** Нажатие кнопки атаки (`BC_ATTACK`) циклически переключало цель наблюдения:  
   `spectatorTarget = (spectatorTarget + 1) % numConnectedPlayers;`
4. **Respawn Timing:** Возрождение посреди уровня отсутствовало. Игроки возрождались автоматически только когда любой живой союзник достигал выхода с уровня.
5. **Level Wipe (Total Party Kill):** При гибели всех участников уровень перезагружался с начала.

### 2.2 Статус в PowerSlave Exhumed Together — ИСКЛЮЧЕНО / ОТКЛОНЕНО (REJECTED)

Данная механика **не переносится** в наш проект по следующим причинам:
- Лишает погибшего игрока активного геймплея на время прохождения остатка уровня.
- Создаёт избыточную сложность отвязки камеры в KEX 2022 (`kexRenderView::Setup()` и матричные трансформации).
- **Вместо спектатора внедряется система ручного возрождения по клавише E на текущем уровне** (полная спецификация представлена в [`16_RESPAWN_DESIGN.md`](16_RESPAWN_DESIGN.md)).

---

## 3. Inventory & Campaign Save Architecture (`.coop`)

### 3.1 2015 File Structure

*Verified Binary Strings (`anubis.exe` at offset `0x147625`):*
- `.sav`
- `.coop`

В моде 2015 года кампания хоста сохранялась в `saves\save_%03d.sav`. Одиночные сохранения рассчитаны только на одного персонажа. Для сохранения кооперативной сессии создавался сайдкар-файл:
`saves\save_%03d.coop`

Бинарный формат содержал:
- `uint32_t magic` (`'COOP'`)
- `uint32_t version`
- `uint32_t num_slots` (до 4 слотов в 2015)
- Для каждого слота:
  - `char playerName[32]`
  - `uint32_t ownedWeaponsMask` (`PW_MACHETE`, `PW_PISTOL`, `PW_M60`, etc.)
  - `uint32_t ammo[NUM_WEAPONS]`
  - `uint32_t health`, `uint32_t armor`
  - `uint32_t artifactsMask` (`PA_SANDALS`, `PA_SHAWL`, etc.)

Общие сюжетные артефакты (Team Dolls, Ключи, Детали передатчика) сохранялись в глобальном файле хоста, а экипировка союзников — в `.coop`.

### 3.2 Адаптация для 2022 Exhumed (2–8 Игроков)
- Расширение пула слотов до 8 (`num_slots = 8`).
- **Правило сброса оружия при смерти:** В отличие от сохранения сессии между уровнями, при внутриуровневой гибели и возрождении по клавише E оружие игрока **сбрасывается до стандартного** начального комплекта (см. [`16_RESPAWN_DESIGN.md`](16_RESPAWN_DESIGN.md)).
- Сайдкары сохранений сохраняются исключительно в `profiles/coop_saves/save_NNN.coop`, не затрагивая 77 оригинальных файлов Steam.

---

## 4. Late-Join Protocol & Level Transitions

### 4.1 2015 Late-Join Flow

*Verified Binary Strings (`anubis.exe` at offset `0x147200`–`0x147600`):*
- `Co-op: level start #%d %s, slot %d of %d, input delay %d ticks`
- `Co-op: level start #%d %s, %d player(s), input delays %d/%d/%d/%d ticks`
- `Co-op: tick %d (checksum %08x) speed %.1f ticks/s (60 = full), stalled frames %d, input delay %d, prediction error avg %.2f max %.2f (%d), artifacts %02x/%02x/%02x/%02x`
- `%s (NEXT LEVEL)`

В моде 2015 года подключившийся посреди игры клиент получал статус `CLIENT_STATE_WAIT_NEXT_LEVEL` (`%s (NEXT LEVEL)` в статусе). Клиент отображал экран ожидания лобби, а полноценный спавн происходил при переходе на следующий уровень:
`level start #%d %s, %d player(s), input delays %d/%d/%d/%d ticks`

### 4.2 Адаптация для 2022 Exhumed
Этот протокол сохраняется для новых внешних подключений посреди уровня: поздний участник не спавнится в полузачищенной геометрии, а ожидает перехода уровня в лобби. Однако для **уже подключённых погибших игроков** применяется локальное возрождение по E без ожидания конца уровня.

---

## 5. In-Game HUD: Scoreboard, Chat & Ping

### 5.1 Controls & Bindings (2015 Evidence)

From the 2015 binary string table:
- **Chat Key:** `T` (`say <text> (co-op only)`). Открывает строку ввода текста (до 90 символов). По Enter отправляется `SAY: %s: %.90s`.
- **Scoreboard Key:** `\` (Backslash). Отображает статусную панель мультиплеера.
- **Scoreboard Content:**
  - `CO-OP HOST (UDP %d) - %d FRIEND%s CONNECTED`
  - Строки игроков: `[PING] [NET_NAME] [HEALTH / STATUS]`
  - Статусы: `HOST`, `(DEAD)`, `(LEFT)`, `(NEXT LEVEL)`
  - Пинг: `Co-op: %s ping %d ms`.

### 5.2 Implementation Seam in 2022 Exhumed
- Отрисовка HUD через шрифтовой рендерер движка (`kexFont` / `FUN_1400d41e0`).
- Перехват клавиш `\` и `T` для отображения таблицы участников и текстовых сообщений.
- Статус погибшего игрока отображает `DEAD (Press E)` вместо `(DEAD/SPECTATING)`.

---

## 6. Spatial Voice Chat Architecture (2015 Reference)

### 6.1 2015 Voice Engine Analysis

*Verified Binary Strings (`anubis.exe` at offset `0x147900`):*
- `<NewPortMappingDescription>PowerSlave EX Co-op</NewPortMappingDescription>`
- `AddPortMapping`, `DeletePortMapping` (UPnP port mapping)
- `voice volume`, `mic volume`, `voice on/off`, `Mic Test`
- `Dead players can listen but not talk`

В 2015 году использовалась модель затухания OpenAL с фильтрацией низких частот через закрытые порталы секторов и автоматическим открытием портов роутера через UPnP SOAP.

### 6.2 Статус в PowerSlave Exhumed Together — ПОЛНОСТЬЮ ИСКЛЮЧЕНО (EXCLUDED)

По прямому указанию пользователя встроенный голосовой чат **полностью исключён** из текущих и будущих планов разработки мода:
- Никакого пространственного голосового чата.
- Никакой передачи звука микрофона по сети.
- Никаких голосовых кодеков (Opus), буферов джиттера, фильтров окклюзии и меню настроек микрофона.
- Игроки используют Discord или другие внешние приложения.

---

## 7. Evidence & Decision Classification

| Feature / Property | Status in 2015 Mod | Decision for Exhumed Together | Governing Document |
|---|---|---|---|
| **Spectator Mode** | `VERIFIED static` (obituaries, target cycling) | **EXCLUDED / REJECTED** | [`16_RESPAWN_DESIGN.md`](16_RESPAWN_DESIGN.md) |
| **Manual Respawn on [E]** | Absent (level-end respawn only) | **ACCEPTED / PRIMARY** | [`16_RESPAWN_DESIGN.md`](16_RESPAWN_DESIGN.md) |
| **Reset Weapons on Death** | Absent (weapons retained in `.coop`) | **ACCEPTED / PRIMARY** | [`16_RESPAWN_DESIGN.md`](16_RESPAWN_DESIGN.md) |
| **Save File Suffix `.coop`** | `VERIFIED static` (string table `0x147625`) | **ACCEPTED (Sidecar in mod dir)** | [`15_COOP_2015_FEATURES.md`](15_COOP_2015_FEATURES.md) |
| **Late Join on Next Level** | `VERIFIED static` (`%s (NEXT LEVEL)`) | **ACCEPTED for mid-game joins** | [`15_COOP_2015_FEATURES.md`](15_COOP_2015_FEATURES.md) |
| **Scoreboard (`\`) & Chat (`T`)** | `VERIFIED static` (`say <text>`, `cl_coopbinds`) | **ACCEPTED** | [`15_COOP_2015_FEATURES.md`](15_COOP_2015_FEATURES.md) |
| **Spatial Voice Chat** | `VERIFIED static` (UPnP, OpenAL filter) | **EXCLUDED COMPLETELY** | N/A (External Discord) |
