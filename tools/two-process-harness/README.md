# Two-Process Test Harness (`tools/two-process-harness`)

> **Пакет П2 / Задача S0.5 из [ROADMAP.md](../../docs/ROADMAP.md) (режим А — самоприёмка)**  
> Превращение двухпроцессного стенда (`runner58b.py`, `input58b.py`, `rea_extract58b.py`, `review58b.py`) в универсальный переиспользуемый инструмент: запуск, управление фокусом, скриптовые сценарии ввода, экспорт evidence и приёмка.

---

## Возможности

1. **Конфигурируемый запуск и изоляция (`session.py`, `launcher.py`)**:
   - Запуск хоста и клиента в режиме `CREATE_SUSPENDED` с независимыми профилями и изоляцией окружения (M0).
   - Поддержка произвольных имён запусков (`host_run`, `client_run`) и путей вывода (`out_dir`), без зашитых строковых констант `N58B_*`.
   - Автоматическая генерация раннего `kexengine.cfg` (`cl_engineNoSleep 1`, `cl_engineFPS 180`).
   - Динамическая сборка скриптов внедрения Frida (`load_native.js`, `permit_native.js`, `status.js`, `request_shutdown.js`, `request_detach.js`, `mailbox_<cmd>.js`).

2. **Надёжное управление фокусом и окнами (`window_manager.py`)**:
   - Поиск окон по PID процесса из `launch.json` (выбор окна с наибольшей клиентской областью).
   - Активация реального переднего плана Win32 (`ShowWindow(SW_RESTORE)` + `SetForegroundWindow`).
   - Резервный механизм через временное объединение очередей ввода (`AttachThreadInput`), если Windows отклоняет фокус.
   - Автоматический цикл повторных попыток с контролем по времени для парирования перехвата фокуса рабочим столом (ловушка ~1,4 с из приложения A).

3. **Сценарии и контроллер ввода (`input_controller.py`, `scenario_runner.py`)**:
   - Точечные действия через CLI или программный API: `focus`, `key`, `mouse`, `hold`.
   - Режим **`handoff`**: отпускание клавиши движения на хосте только после фактической передачи фокуса клиенту (замедление хоста в фоне).
   - **Декларативные сценарии ввода** в формате JSON:
     - Валидация типов действий, диапазонов длительности и ролей перед запуском.
     - Прогон сценария с логированием каждого шага (`SCENARIO_<name>_<utc>.json`).
     - Поддержка dry-run проверки без влияния на запущенные процессы.
     - Готовый канонический сценарий движения: [`scenarios/doc58b_run02.json`](scenarios/doc58b_run02.json).

4. **Универсальный экспорт Evidence из REA MCP (`rea_extractor.py`)**:
   - Извлечение записей из дампов REA Evidence Bundle (`bundle_*.json`) для произвольных сессий и запусков.
   - Извлечение скриптов (`LOAD_<STEM>.json` и инлайн-скриптов `LOAD_INLINE_<ID>.json`).
   - Сохранение статуса завершения (`STATUS_EXIT.json`) с метаданными provenance.

5. **Универсальный ревьюер и приёмочный аудит (`evidence_reviewer.py`, `doc58b_reproduction.py`)**:
   - Полная корреляция сетевой цепочки (`NETWORK_CORRELATION.jsonl`): захват на хосте -> отправка Worker -> OS UDP -> прием OS UDP -> клиентский Worker -> клиентский Sim -> действие прокси -> адаптер -> рендер.
   - Бит-точная сверка координат, кватернионов/yaw и монотонности последовательностей.
   - Аудит времени жизни Actor (`Spawn` -> `Update` -> `Remove` -> `Drain` -> `PoolReturn` -> `Retirement`).
   - Контроль неизменности Local Player клиента (12 полей снимка, запрет локальных записей).
   - Оценка native gates (GATE-PSE-58C-01…07, 58D-08…09).
   - **Приёмка S0.5:** точное воспроизведение результатов DOC-58B run 02 без расхождений (`doc58b_reproduction.py`).

---

## Использование CLI (`harness.py`)

### 1. Воспроизведение и приёмка DOC-58B Run 02
```bash
python -X utf8 -B tools/two-process-harness/harness.py reproduce-58b
```
Загружает журналы `N58B_HOST_02` и `N58B_CLIENT_02`, проводит полный аудит и сверяет все метрики и вердикты с эталонным `RESULT_SUMMARY.json`.

### 2. Запуск декларативного сценария ввода
```bash
# Проверка валидности шагов (dry-run)
python -X utf8 -B tools/two-process-harness/harness.py scenario tools/two-process-harness/scenarios/doc58b_run02.json --dry-run

# Выполнение сценария
python -X utf8 -B tools/two-process-harness/harness.py scenario tools/two-process-harness/scenarios/doc58b_run02.json
```

### 3. Точечное управление окнами и вводом
```bash
# Активация окна хоста или клиента
python -X utf8 -B tools/two-process-harness/harness.py focus host
python -X utf8 -B tools/two-process-harness/harness.py focus client

# Нажатие клавиши с удержанием (в секундах)
python -X utf8 -B tools/two-process-harness/harness.py key host W 0.03
python -X utf8 -B tools/two-process-harness/harness.py key host A 0.08 --handoff

# Относительное движение мыши
python -X utf8 -B tools/two-process-harness/harness.py mouse host 120

# Пауза
python -X utf8 -B tools/two-process-harness/harness.py hold 1.5
```

### 4. Экспорт evidence из бандла REA
```bash
python -X utf8 -B tools/two-process-harness/harness.py extract <path/to/bundle.json> <run_name> <session_id> STATUS_EXIT
```

### 5. Анализ и формирование отчёта по сырым журналам
```bash
python -X utf8 -B tools/two-process-harness/harness.py review --host-dir <host_folder> --client-dir <client_folder> --write
```

---

## Запуск модульных тестов

```bash
python -X utf8 -B -m unittest discover -s tools/two-process-harness/tests
```
Тестовый набор покрывает:
- Инициализацию сессии, разрешение путей, хеширование EXE и DLL (`test_session.py`).
- Валидацию шагов и декларативных сценариев (`test_scenario_runner.py`).
- Извлечение данных из реальных и синтетических REA evidence bundle (`test_rea_extractor.py`).
- Бит-точное воспроизведение приёмочного среза DOC-58B Run 02 (`test_doc58b_reproduction.py`).
