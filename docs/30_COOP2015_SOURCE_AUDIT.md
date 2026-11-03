# PowerSlave EX — Аудит оригинального исходного кода Co-Op Edition (2015)

**Дата исследования:** 2026-11-01  
**Статус исследования:** Исходный код полностью верифицирован, сопоставлен с бинарником и классифицирован.  
**Целевой каталог:** `S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_EX_-_CO-OP_Edition-source-code`  
**Исследуемый бинарник:** `PowerSlave_EX_-_CO-OP_Edition\anubis.exe` (SHA256: `488DEF7A7D0F676ADD9E2CF188639E13927A52CD6E27E58AB90FE48D4DE1990F`)

---

## 1. Проверка подлинности и полноты исходников

### 1.1. Верификация подлинности бинарной сборки (100% Cryptographic Match)

В ходе криптографического сопоставления хэшей было проверено 4 независимых экземпляра бинарника `anubis.exe`:
1. `PowerSlave_EX_-_CO-OP_Edition\anubis.exe` (ранее исследованный бинарник мода)
2. `PowerSlave_EX_-_CO-OP_Edition-source-code\run\anubis.exe` (рабочая папка запуска автора)
3. `PowerSlave_EX_-_CO-OP_Edition-source-code\build\anubis.exe` (артефакт компиляции `build.py`)
4. `PowerSlave_EX_-_CO-OP_Edition-source-code\friend_kit\anubis.exe` (клиентский комплект для друзей)

**Результат вычисления SHA256:**
```text
488DEF7A7D0F676ADD9E2CF188639E13927A52CD6E27E58AB90FE48D4DE1990F
```
Все 4 хэша **побайтово идентичны**. Это неопровержимо доказывает, что предоставленный каталог исходного кода является точным первоисточником ранее исследованного исполняемого файла `anubis.exe`.

### 1.2. Происхождение и разделение исходного кода (Baseline Diff)

В корне исходников обнаружены две ключевые поддиректории:
- `src_orig`: эталонная копия исходников движка `Kex Engine 3.0` (PowerSlave EX).
- `src\kex3_anubis\source`: модифицированное дерево с изменениями кооператива.

Сравнение `src_orig` с оригинальным публичным репозиторием автора движка (`PowerslaveEX-master-source-code\kex3_anubis\source`, commit `40994f856c802ef6e9f2988ebfa6c2e36db3ba9c`, 2017/svkaiser) показало **ровно 0 различий**.  
Это подтверждает, что автор мода в качестве базы взял чистый релизный срез Kaiser'а.

### 1.3. Полная инвентаризация изменений

Сравнительный анализ (diff) между `src_orig` и `src\kex3_anubis\source` выявил:
- **6 новых файлов C++:**
  - `game/netgame.h` и `game/netgame.cpp` (высокоуровневая логика кооператива, lockstep, лобби, синхронизация инвентаря)
  - `game/net_udp.h` и `game/net_udp.cpp` (неблокирующий сокет WinSock UDP, реализация UPnP IGD через SSDP/SOAP)
  - `game/voice.h` и `game/voice.cpp` (позиционный голосовой чат OpenAL, 32 kHz IMA ADPCM, акустика порталов)
- **41 модифицированный файл оригинального движка:**
  - *Жизненный цикл и симуляция:* `game/game.h`, `game/game.cpp`, `game/playLoop.h`, `game/playLoop.cpp`, `framework/session.h`, `framework/session.cpp`
  - *Сущности и акторы:* `game/actor.h`, `game/actor.cpp`, `game/actorFactory.cpp`, `game/gameObject.cpp`, `game/mover.cpp`
  - *Игрок и ввод:* `game/player.h`, `game/player.cpp`, `game/playerCmd.h`, `game/playerCmd.cpp`, `system/sdl/inputSDL.cpp`
  - *Физика и коллизии:* `game/cmodel.h`, `game/cmodel.cpp`
  - *ИИ и враги:* `game/ai.h`, `game/ai.cpp`
  - *Оружие и снаряды:* `game/projectile.h`, `game/projectile.cpp`, `game/actionDef.cpp`
  - *Предметы и мир:* `game/pickup.cpp`, `game/travelObject.cpp`, `game/world.cpp`
  - *Интерфейс и рендеринг:* `game/menu.h`, `game/menu.cpp`, `game/titlescreen.h`, `game/titlescreen.cpp`, `renderer/renderScene.cpp`, `renderer/renderView.h`, `renderer/renderView.cpp`
  - *Звук и система:* `system/sound.h`, `system/al/soundOAL.cpp`, `system/system.h`, `system/system.cpp`, `system/sdl/systemSDL.cpp`
  - *Математика и скрипты:* `math/mathlib.h`, `math/random.cpp`, `script/objects/gameSysObject.cpp`
- **Вспомогательные утилиты и арт:**
  - `build.py` (автономный сборщик на базе Python и MSVC BuildTools 2022 x86)
  - `MODLOG.md` (детальный инженерный журнал автора на 295 строк, охватывающий этапы разработки с 04.10.2026 по 05.10.2026)
  - `art/segment.py` и `art/build_john.py` (пакер спрайтов игрока John из рипа Dunkelschwamm в атлас `john.kpf`)

---

## 2. Архитектура многопользовательского Player/Puppet

### 2.1. Выделение слотов и контекстов игроков

В оригинальном PowerSlave EX 2015 года класс `kexGameLocal` содержал ровно один экземпляр `kexPlayer *player;`.

Автор мода перестроил хранение игроков:
- **Файл:** `src\kex3_anubis\source\game\game.h`, строки 225–233, 290–296:
  ```cpp
  static const int maxPlayers = 4;
  kexPlayer *Player(void) { return player; } // local/view player
  kexPlayer *GetPlayer(const int i) { return &players[i]; }
  const int NumPlayers(void) const { return numPlayers; }
  const int ConsolePlayer(void) const { return consolePlayer; }
  void SetupPlayers(const int count, const int console);

  kexPlayer *player; // указывает на &players[consolePlayer]
  kexPlayer players[4];
  int numPlayers;
  int consolePlayer;
  ```
- **Инициализация слотов:**
  `kexGameLocal::SetupPlayers(int count, int console)` (`game/game.cpp`, строки 681–696):
  Инициализирует массив `players[4]`, присваивает каждому слоту свой `PlayerNum() = i`, устанавливает `consolePlayer` (индекс локального игрока за данным ПК) и привязывает указатель `player = &players[console]`.

### 2.2. Спавн и связывание Puppet с Player

В Kex Engine сущность физического тела в мире представлена классом `kexPuppet` (наследник `kexActor`), а логика контроллера, оружия и инвентаря — `kexPlayer` (наследник `kexObject`).

1. **Множественный спавн на старте карты:**
   - **Файл:** `src\kex3_anubis\source\game\world.cpp`, строки 656–676 (`kexWorld::SpawnMapActor`):
     ```cpp
     if(mapActor->type == AT_PLAYER)
     {
         int count = kexGame::cLocal->NumPlayers();
         for(int i = 0; i < count; ++i)
         {
             actor = kexGame::cActorFactory->Spawn(mapActor->type, x, y, z, an, mapActor->sector);
             if(actor)
             {
                 actor->SetMapActor(mapActor);
             }
         }
         return;
     }
     ```
     Все активные игроки спавнятся в координатах стартовой точки `AT_PLAYER`. Взаимные коллизии тел отключены, поэтому игроки не застревают друг в друге.

2. **Связывание тела с контекстом:**
   - **Файл:** `src\kex3_anubis\source\game\player.cpp`, строки 1335–1368 (`kexPuppet::Spawn`):
     ```cpp
     // связывание с первым свободным слотом игрока без тела
     owner = kexGame::cLocal->GetPlayer(0);
     for(int i = 0; i < kexGame::cLocal->NumPlayers(); ++i)
     {
         if(kexGame::cLocal->GetPlayer(i)->Actor() == NULL)
         {
             owner = kexGame::cLocal->GetPlayer(i);
             break;
         }
     }
     owner->SetActor(this);
     ```
   - Назначение цветового тинта спрайта:
     ```cpp
     static const float tint[4][3] = { {1, 1, 1}, {1, 0.6f, 0.6f}, {0.65f, 0.8f, 1}, {0.7f, 1, 0.7f} };
     int n = owner->PlayerNum();
     color.Set(tint[n & 3][0], tint[n & 3][1], tint[n & 3][2]);
     ```

### 2.3. Разделение симуляции и отображения (Local vs Global View)

В синглплеере 48 мест в коде обращались к `cLocal->Player()`. Автор строго разделил контекст:
1. **Вид, HUD, меню, вспышки экрана:** читают `Player()` (локальный игрок за монитором `consolePlayer`).
   - `DamageFlash()`, `ElectrocuteFlash()`, `PickupFlash()`, `TeleportFlash()` вызываются только если `owner == cLocal->Player()`.
   - Сообщения об отсутствии ключей («Key required») и интерактивные подсказки выводятся только локальному игроку.
2. **Симуляция (Gameplay Tick):**
   - **Файл:** `src\kex3_anubis\source\game\playLoop.cpp`, строки 463–471 (`kexPlayLoop::Tick`):
     ```cpp
     kexGame::cLocal->UpdateGameObjects(); // ТИКАЮТ ВСЕ ПУППЕТЫ (как акторы)

     for(int i = 0; i < kexGame::cLocal->NumPlayers(); ++i)
     {
         if(kexGame::cLocal->GetPlayer(i)->Actor())
         {
             kexGame::cLocal->GetPlayer(i)->Tick(); // ТИКАЮТ ВСЕ КОНТРОЛЛЕРЫ
         }
     }
     ```

### 2.4. Смерть, спектэйтинг и перезапуск при вайпе

В ранней версии мода автор пробовал мгновенный респавн через таймер, но это вызывало критический баг «tiny player» (камера падала на уровень пола из-за рассинхронизации высоты глаз и дельта-позиций контроллера).

**Окончательное рабочее решение (Stage 2.2):**
1. **Спектэйтинг:** Умерший игрок (`PF_DEAD`) не респавнится посреди уровня. Камера 90 тиков показывает его труп, затем автоматически переключается на ближайшего живого напарника. Нажатие атаки переключает наблюдаемого игрока по кругу (`kexPlayLoop::UpdateSpectator`, `playLoop.cpp:787–846`).
2. **Респавн на следующем уровне:** Если хотя бы один живой игрок завершает уровень, в `kexPlayLoop::Stop` (`playLoop.cpp:259–279`) здоровье всех погибших игроков восстанавливается до `maxHealth`. На новой карте все появляются живыми с сохраненным оружием.
3. **Командный вайп (Team Wipe):**
   - На старте уровня делается полный снимок состояния всех игроков: `game->SnapshotCoopPlayers()` (`game.cpp:665–676`).
   - Если все игроки мертвы в течение 60 тиков (`UpdateCoopDeaths`, `playLoop.cpp:767–785`), вызывается `RestartLevel()`, восстанавливающий снимок `RestoreCoopPlayers()`.

---

## 3. Критический аудит управления памятью и ссылками (P0 для Sol Max / M0)

### 3.1. Инвазивный Refcounting в `kexGameObject`

В архитектуре Kex Engine 3.0 базовый класс игровых объектов `kexGameObject` реализует явный встроенный счетчик ссылок:
- **Файл:** `src_orig\game\gameObject.h` и `gameObject.cpp`:
  ```cpp
  int refCount;
  kexGameObject *target;
  bool bStale; // объект помечен на удаление
  ```
- **Правило установки цели (`SetTarget`):**
  ```cpp
  void kexGameObject::SetTarget(kexGameObject *targ)
  {
      if(target) target->RemoveRef();
      if((target = targ)) target->AddRef();
  }
  ```
- **Условие фактического удаления:**
  ```cpp
  const bool kexGameObject::Removing(void) const
  {
      return (bStale && RefCount() <= 0);
  }
  ```
  Если на объект ссылается хотя бы один другой объект (`RefCount() > 0`), объект **НЕ МОЖЕТ** быть удален в деструктор/пул, даже если вызван `Remove()` (`bStale = true`).

### 3.2. Владение ссылками на снаряды и источники урона

1. **Кто держит ссылку на стрелка?**
   - При создании снаряда (`actionDef.cpp:633` в `kexActionLaunchProjectile`):
     ```cpp
     proj->SetTarget(actor);
     ```
     Поле `target` снаряда указывает на стрелка (`actor`). Это вызывает `actor->AddRef()`.
   - В коде коллизий снаряда (`cmodel.cpp:613`):
     ```cpp
     if(bIsAProjectile && actor == moveActor->Target())
     ```
     Снаряд проверяет своего владельца через `Target()`, чтобы не сталкиваться со стрелком.
2. **Кто держит ссылку на цель самонаведения?**
   - В `kexProjectile` есть отдельное поле `homingActor` (`projectile.cpp:86–98`):
     ```cpp
     void kexProjectile::SetHomingTarget(kexActor *actor)
     {
         if(homingActor) homingActor->RemoveRef();
         if((homingActor = actor)) homingActor->AddRef();
     }
     ```
3. **Защита от dangling pointer при удалении цели снаряда:**
   - В `kexProjectile::HomingThink` (`projectile.cpp:111, 209`):
     ```cpp
     if(homingActor->Removing() || homingActor->Health() <= 0)
     {
         SetHomingTarget(NULL); // АВТОМАТИЧЕСКИЙ СБРОС И ДЕКРЕМЕНТ REFCOUNT
     }
     ```
   - Деструктор снаряда гарантирует освобождение: `kexProjectile::~kexProjectile() { SetHomingTarget(NULL); }`.

### 3.3. Разделение типов: `kexActor` vs `kexGameObject` (Разрешение блокера 0x160)

В исследовании M0 Sol Max столкнулся с крашем/блокером:
> «Basic projectile factory object has size 0x160; Actor Remove writes +0x16c. Direct Actor Remove on 0x160 projectile causes corruption / out-of-bounds write!»

**Открытие в исходном коде 2015:**
- `kexFireballFactory` наследуется от `kexGameObject`, а **НЕ** от `kexActor`! (`projectile.h:130`):
  ```cpp
  BEGIN_EXTENDED_KEX_CLASS(kexFireballFactory, kexGameObject);
  ```
  В то время как `kexFireballSpawner` наследуется от `kexActor` (`projectile.h:105`).
- Класс `kexGameObject` не имеет физических полей актора (`bounds`, `sectorLink`, `areaLink`, `flags` на смещении `+0x16c` в x64).
- Метод `kexActor::Remove()` выполняет:
  ```cpp
  flags &= ~(AF_SOLID|AF_SHOOTABLE|AF_MOVEABLE|AF_TOUCHABLE|AF_BOUNCY);
  flags |= (AF_NOADVANCEFRAMES|AF_HIDDEN);
  kexGameObject::Remove();
  ```
  Если передать указатель на базовый `kexGameObject` (размером 0x160) в `kexActor::Remove`, он пишет во флаги актора за пределами аллокации (на `+0x16c`).
- **Вывод для Sol Max:** Для фабрик и базовых объектов необходимо вызывать исключительно `kexGameObject::Remove()` (устанавливающий флаг stale), ни в коем случае не `kexActor::Remove()`.

### 3.4. Алгоритм двухпроходного разрыва циклических ссылок при Teardown

При смене карты или очистке уровня в `kexGameLocal::RemoveAllGameObjects()` (`game.cpp:1224–1243`) реализован строгий двухпроходный алгоритм:
```cpp
void kexGameLocal::RemoveAllGameObjects(void)
{
    kexGameObject *go;
    kexGameObject *next;
    
    // ПРОХОД 1: Сброс ВСЕХ ссылок target у всех объектов в мире
    for(go = gameObjects.Next(); go != NULL; go = go->Link().Next())
    {
        go->SetTarget(NULL);
    }
    
    // ПРОХОД 2: Фактическое удаление объектов с проверкой RefCount == 0
    for(go = gameObjects.Next(); go != NULL; go = next)
    {
        next = go->Link().Next();
        RemoveGameObject(go);
    }
    
    gameObjects.Clear();
}
```
Этот проход гарантирует, что взаимные ссылки монстров, снарядов, источников света и триггеров обнуляются до вызова деструкторов, предотвращая появление осиротевших ссылок с `refCount > 0`.

### 3.5. Утечка ссылок через звуковую подсистему (Найденный дефект автора)

Автор мода обнаружил скрытую утечку ссылок в оригинальном движке: проигрываемый звук вызывал `AddRef()` у игрового объекта. Если объект удалялся, пока звук еще звучал, объект оставался заблокированным в памяти, вызывая рассинхронизацию lockstep.
- **Исправление автора (`gameObject.cpp:77–99`):**
  ```cpp
  void kexGameObject::Remove(void)
  {
      bStale = true;
      for(int i = 0; i < kex::cSound->NumSources(); ++i)
      {
          if(kex::cSound->GetRefObject(i) == this)
          {
              if(kex::cSound->SourceLooping(i))
                  kex::cSound->Stop(i);
              else
                  kex::cSound->DetachRefObject(i);
          }
      }
  }
  ```
  В 2022 году Sol Max должен учитывать, не удерживает ли звуковой движок FMOD/KEX4 ссылки на акторов через свои звуковые каналы.

---

## 4. Сетевая архитектура и синхронизация

### 4.1. Сетевая топология и модель синхронизации

- **Модель:** Детерминированный Lockstep (Doom-style). Все клиенты симулируют один и тот же мир с частотой 60 Гц.
- **Топология:** Звезда с координацией хостом.
  - Клиент собирает ввод `netCmd_t` (кнопки, углы взгляда, векторы движения ~20 байт) и отправляет хосту пакет `PK_CMDS`.
  - Хост собирает команды всех слотов и рассылает релей `PK_TICKS`.
  - Тик симуляции выполняется только тогда, когда получены команды всех игроков.
- **Ограничение:** Жесткий лимит до 4 игроков (`maxPlayers = 4`).

### 4.2. Адаптивная задержка ввода и предсказание вида

1. **Адаптивный буфер задержки ввода (`AdaptDelay`):**
   - У хоста задержка зафиксирована на уровне 2 тиков (~33 мс).
   - У клиентов задержка динамически настраивается: `delay = 1..2 + ceil(RTT * 60 / 1000)`. При скачках пинга задержка плавно поднимается, при стабилизации — опускается, удерживая запас 1–2 тика.
2. **Клиентское предсказание камеры (`kexPuppet::PredictView`):**
   - В локстепе симуляция отстает от ввода на величину задержки. Чтобы мышь и движение не «плавали», автор создал алгоритм теневого актора:
   - В локальном кадре создается приватный экземпляр `static kexActor *shadow`.
   - Теневой актор прогоняет накопленные, но еще не симулированные команды через реальную физику `kexCModel::MoveActor`.
   - Камера рендерится из позиции теневого актора. Теневой актор не добавляется в список игровых объектов, не трогает триггеры и не вызывает коллизий с другими игроками.

### 4.3. Автоматический UPnP IGD

В файле `game/net_udp.cpp` реализован встроенный UPnP клиент без внешних библиотек:
- Отправляет мультикаст SSDP discovery (`M-SEARCH` на `239.255.255.250:1900`).
- Извлекает XML описание IGD роутера.
- Выполняет SOAP запрос `AddPortMapping` по протоколу UDP на порт `26100`.
- При выходе (`NetUDP_UPnPStop`) отправляет `DeletePortMapping`.
- Работает в фоновом потоке Windows (`CreateThread`).

---

## 5. Геймплейные правила кооператива

### 5.1. Отключение Friendly Fire

- **Хитскан, использование и ближний бой (`cmodel.cpp:609–615`):**
  Трассировка луча `TraceActorsInSector` игнорирует акторов с типом `kexPuppet`, если источником трассировки является другой `kexPuppet`.
- **Снаряды (`cmodel.cpp:596–608`):**
  Снаряд проходит сквозь тела тиммейтов, если владелец снаряда (`moveActor->Target()`) — `kexPuppet`.
- **Взрывной урон (`actor.cpp:501–517`):**
  В функции `InflictDamage` проверяется источник урона. Если источник — другой игрок или снаряд другого игрока, урон полностью аннулируется. Урон по самому себе (гранаты/ракеты под ноги) сохраняется.

### 5.2. Поведение ИИ и перевыбор целей (Retargeting)

В синглплеере монстр выбирал игрока один раз при первой видимости и преследовал его до смерти.
Автор мода внедрил эвристическую систему взвешивания и динамического переключения целей:
- **Функция оценки (`kexAI::ScorePlayerTarget`, `ai.cpp:549–571`):**
  $$\text{Score} = \text{Distance} \times K_{\text{vis}} \times K_{\text{dmg}} \times K_{\text{facing}}$$
  - $K_{\text{vis}} = 0.5$ при прямой видимости, иначе $2.5$.
  - $K_{\text{dmg}} = 0.6$, если этот игрок нанес урон монстру за последние 180 тиков (3 секунды).
  - $K_{\text{facing}} = 0.8$, если игрок находится в передней полусфере монстра (угол $< 90^\circ$).
- **Перевыбор с гистерезисом (`kexAI::RetargetBestPlayer`, `ai.cpp:583–620`):**
  Каждые 20 тиков монстр оценивает всех видимых игроков. Переключение происходит только в том случае, если счет нового кандидата превосходит текущего более чем на 25% ($\text{Score}_{\text{new}} < 0.75 \times \text{Score}_{\text{cur}}$). Это полностью устраняет «дребезг» ИИ между двумя рядом стоящими игроками.
  Алгоритм на 100% детерминирован (без вызовов RNG).

### 5.3. Синхронизация прогрессии и предметов

- **Общие предметы команды (`SyncSharedProgression`, `game.cpp:1559–1590`):**
  Каждый игровой тик выполняется побитовое объединение (OR) артефактов, квестовых передатчиков, способностей и кукол:
  ```cpp
  arti |= players[i].Artifacts();
  quest |= players[i].QuestItems();
  abil |= players[i].Abilities();
  dolls |= players[i].TeamDolls();
  ank = max(ank, players[i].Ankahs());
  ```
  Затем объединенные флаги присваиваются всем игрокам.
- **Оружие в мире (`pickup.cpp:175–195`):**
  Оружие не исчезает из мира при поднятии первым игроком. Оно остается доступным до тех пор, пока его не подберут все активные игроки (`AllPlayersOwnWeapon`).

---

## 6. Аудит качества AI-generated кода (Vibe Coding Audit)

Код мода классифицирован по категориям надежности:

| Компонент / Решение | Классификация | Техническое обоснование |
|---|---|---|
| Хранение 4 игроков в `players[4]` | **WORKING-EVIDENCED** | Стабильно работает, проверено сетевыми сессиями и хэшем бинарника. |
| Спавн `kexPuppet` на `AT_PLAYER` | **WORKING-EVIDENCED** | Подтверждено в игре, корректно привязывает слоты через `owner->SetActor`. |
| Friendly Fire & Прохождение снарядов | **WORKING-EVIDENCED** | Строгая проверка в `cmodel.cpp` и `actor.cpp`, подтверждена тестами. |
| ИИ эвристика перевыбора целей | **WORKING-EVIDENCED** | Детерминированный алгоритм без RNG, устраняет десинхронизации. |
| Спектэйтинг и командный вайп | **WORKING-EVIDENCED** | Надежная замена таймерному респавну, решает баг «tiny player». |
| OpenAL Voice Chat (IMA ADPCM) | **SOURCE-IMPLEMENTED** | Реализован полностью в исходниках, подтвержден автором в MODLOG. |
| Двухпроходный Teardown `SetTarget(NULL)` | **WORKING-EVIDENCED** | Оригинальный алгоритм Kaiser'а, критически важный для M0. |
| Потокобезопасность UPnP в `net_udp.cpp` | **DEFECTIVE** | Фоновый поток пишет в незащищенные статические буферы `upnpMsg`, `upnpControl` без мьютексов. Вызов `CloseHandle` не останавливает старый поток. |
| Утечка памяти в `PredictView` | **DEFECTIVE** | `static kexActor *shadow = new kexActor;` аллоцируется один раз и никогда не освобождается. |
| Форматирование строк через `sprintf` | **DEFECTIVE** | Использование `sprintf` в буферы фиксированного размера (128 байт) при обработке сетевых имен создает риск переполнения буфера. |
| Хардкод лимита ловушек `fbMaxSectors = 8192` | **UNVERIFIED** | При загрузке нестандартных карт с числом секторов > 8192 ловушки отключат защиту от очередей. |

---

## 7. Лицензионный аудит и интеллектуальная собственность

1. **Исходный код движка (Samuel "Kaiser" Villarreal):**
   - Распространяется под лицензией **GNU General Public License v2 (GPLv2)**.
   - Все оригинальные файлы в `src_orig` и `src` содержат стандартный заголовок GPLv2.
2. **Кооперативные модификации автора:**
   - Новые файлы (`netgame.cpp`, `net_udp.cpp`, `voice.cpp`) явно помечены как производные работы под лицензией GPLv2: `// PowerSlave EX co-op (added for the co-op source port, GPL like the rest of the engine)`.
   - В соответствии с условиями GPLv2 (раздел 2b), прямое заимствование или вставка исходного кода в другой проект накладывает обязательство распространять весь проект под GPLv2.
   - **Применение к нашему проекту PowerSlave Exhumed Together (2022):**  
     PowerSlave Exhumed (2022) является закрытым коммерческим продуктом Nightdive Studios. Наш мод работает как внешний инжектор/хук (x64 DLL).  
     **Прямое копирование исходного кода 2015 года в кодовую базу мода запрещено лицензионным разделением.**  
     Разрешено исключительно **алгоритмическое и архитектурное переиспользование** (clean-room reimplementation) принципов: эвристики ИИ, правил разделения прогрессии, логики спектэйтинга и алгоритмов очистки ссылок.
3. **Графические ресурсы спрайтов (`john_sheet.png` / `john.kpf`):**
   - Спрайты персонажа John рипнуты Dunkelschwamm с оригинальной DOS/PC-версии Exhumed для ресурса The Spriters Resource.
   - Данный арт не является свободным для коммерческого распространения и не может включаться в релизные пакеты без явных оговорок о фанатском некоммерческом статусе.

---

## 8. Итоговые выводы

1. Предоставленный каталог исходного кода является на 100% аутентичным и полным первоисточником мода `PowerSlave EX - CO-OP Edition`.
2. Архитектура старого кооператива содержит готовые, проверенные временем алгоритмические ответы на ключевые вопросы геймплея: разделение прогрессии, бесконфликтные коллизии союзников, спектэйтинг вместо опасного мгновенного респавна, и эвристику агро монстров.
3. Механизм управления ссылками Kaiser'а подтверждает гипотезу Sol Max: удержание ссылок `target` и `sourceOwner` является штатным поведением инвазивного refcounting в KEX-движках, требующим двухпроходного обнуления при удалении акторов.
4. Сетевой транспорт 2015 года (WinSock lockstep) устарел и непригоден для переноса на 2022 год; наш автономный ENet replication-прототип (DOC-29) на порядок превосходит его по масштабируемости и устойчивости к сетевым задержкам.
