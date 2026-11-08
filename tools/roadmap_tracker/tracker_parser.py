"""
Parser for PowerSlave Exhumed Together project status.
Reads STRICTLY from the 4 designated markdown files:
- CURRENT_STATE.md
- ROADMAP.md
- PROGRESS.md
- EVIDENCE_INDEX.md

Produces structured data for the Roadmap Tracker UI.
"""

import os
import re
import json
from datetime import datetime

# Canonical Package metadata (names, segments, tracks from ROADMAP §4.1, §4.2, §5)
PACKAGE_CATALOG = {
    'П1': {
        'name': 'Адаптер прокси в DLL',
        'seg': 'S0',
        'track': 'Основной путь',
        'crit': True,
        'details': 'S0.4: перенос управления прокси из JS в C++ DLL; single-proxy bounded lifecycle, retirement',
        'notes': 'PASS (bounded). Неблокирующие замечания N1–N4 переданы в П3/П4.'
    },
    'П2': {
        'name': 'Двухмашинный стенд',
        'seg': 'S0',
        'track': 'Независимый',
        'crit': False,
        'details': 'S0.5: переиспользуемый двухпроцессный стенд (tools/two-process-harness)',
        'notes': 'PASS. Воспроизведение DOC-58B run 02 без расхождений, 12/12 unit tests PASS.'
    },
    'П3': {
        'name': 'Реконнект и смена карт',
        'seg': 'S1',
        'track': 'Основной путь',
        'crit': True,
        'details': 'S1.1, S1.2: реконнект и повторный Spawn поверх ENGINE_RETIRED, teardown-witness, смена карты',
        'notes': 'PASS (bounded, режим В; DOC-78 §7). Реконнект, повторный Spawn и 10 переходов карты на клиенте; run 05 после исправлений. Границы: один прокси, TOMB. S1.2 PARTIAL: смерть → рестарт и следующая карта не сделаны (П4/П7/П17).'
    },
    'П4': {
        'name': 'Спавн, порталы, сглаживание',
        'seg': 'S1',
        'track': 'Основной путь',
        'crit': True,
        'details': 'S1.3, S1.4, S1.5: anchor spawn, corridor portal, jitter-буфер, фоновый тик клиента',
        'notes': 'Возвращён исполнителю после проверки DOC-67 (Opus extra): дефектов памяти нет, run01 подтверждён. Нужны F1 (код цели П1 изменён вне флага), F2 (буфер без дрейфа и переякорения), native-прогон текущих кандидатов и решение пользователя по v1.11.'
    },
    'П5': {
        'name': 'Полный тест на двух ПК',
        'seg': 'S1',
        'track': 'Основной путь',
        'crit': True,
        'details': 'S1.6: первый тест на двух ПК по LAN/Radmin с DLL',
        'notes': 'Ждёт: П3, D6 (партнёр и время), D5 (загрузчик) или стенд REA на втором ПК.'
    },
    'П6': {
        'name': 'ADR: авторитет и мир клиента',
        'seg': 'S2 / S3',
        'track': 'Основной путь',
        'crit': True,
        'details': 'S2.1, S3.1: ADR-1 (авторитет игрока) и ADR-2 (модель мира клиента W1, порядок спавна 25/25)',
        'notes': 'REVIEW (режим В). Ждёт оппонентов Sol extra и Gemini max, затем решений пользователя D2 и D3.'
    },
    'П7': {
        'name': 'Свой игрок/пешка от хоста',
        'seg': 'S2',
        'track': 'Основной путь',
        'crit': True,
        'details': 'S2.2, S2.3: здоровье, патроны, оружие, ключи с хоста; локальный урон подавлен',
        'notes': 'Ждёт: П3 и решений по П6 (D2, D3).'
    },
    'П8': {
        'name': 'Спрайты игроков и переходы',
        'seg': 'S2',
        'track': 'Параллельная дорожка',
        'crit': False,
        'details': 'S2.4: спрайты игроков от 3-го лица с перекраской по слотам вместо мумии, анимации',
        'notes': 'Параллельная дорожка. Зависимость (П1) выполнена! Готов к старту в режиме Б.'
    },
    'П9': {
        'name': 'Проба общего мира (1 тип врага)',
        'seg': 'S3',
        'track': 'Основной путь',
        'crit': True,
        'details': 'S3.2: проба общего мира: 1 тип врага на одной карте; на клиенте AI выключен, данные с хоста',
        'notes': 'Главный архитектурный риск! Ждёт: завершения П6 и решений пользователя (D3).'
    },
    'П10': {
        'name': 'Все типы врагов и боссы',
        'seg': 'S3',
        'track': 'Основной путь',
        'crit': True,
        'details': 'S3.3: поведение, особые атаки, арены, все типы врагов и боссы',
        'notes': 'Ждёт: пробы П9.'
    },
    'П11': {
        'name': 'Снаряды, эффекты и звук',
        'seg': 'S3',
        'track': 'Основной путь',
        'crit': True,
        'details': 'S3.4: визуал на клиенте, авторитет на хосте; подавление gameplay-действий анимаций на клиенте',
        'notes': 'Ждёт: пробы П9.'
    },
    'П12': {
        'name': 'Двери, лифты, выключатели',
        'seg': 'S3',
        'track': 'Основной путь',
        'crit': True,
        'details': 'S3.5: двери, лифты, движущиеся полы и потолки, переключатели',
        'notes': 'Ждёт: пробы П9.'
    },
    'П13': {
        'name': 'Подключение протокола мира',
        'seg': 'S3',
        'track': 'Основной путь',
        'crit': True,
        'details': 'S3.6: подключение wire-протокола репликации мира из прототипа (DOC-29) к нативному адаптеру',
        'notes': 'Ждёт: пробы П9.'
    },
    'П14': {
        'name': 'Host AI и подбор на хосте',
        'seg': 'S4',
        'track': 'Параллельная дорожка',
        'crit': False,
        'details': 'S4.1, S4.2: AI селектор ближайшего игрока (BLK-01/02), раздельный подбор (BLK-03/04), Ankh dedup (BLK-05)',
        'notes': 'Возвращён проверкой DOC-70 (Opus high): статика, smoke и пересборка 7/7 подтверждены; F1 — после потери права AI и подбор хоста замирают; нужны F1–F3, F7 и игровые gates BLK-01…05.'
    },
    'П15': {
        'name': 'Ключи, скрипты, выход, кат-сцены',
        'seg': 'S4',
        'track': 'Параллельная дорожка',
        'crit': False,
        'details': 'S4.3, S4.4, S4.6: реестр ключей (BLK-06), скрипты (BLK-07/08), синхронный выход (BLK-09), кат-сцены (BLK-10)',
        'notes': 'Ждёт: закрытия и приёмки П14.'
    },
    'П16': {
        'name': 'Возрождение на E и счёт команды',
        'seg': 'S4',
        'track': 'Параллельная дорожка',
        'crit': False,
        'details': 'S4.5: возрождение на E, гибель всей команды, оружие после возрождения (BLK-11)',
        'notes': 'PASS (bounded) режима Б, DOC-88: E-респаун, TPK без reload, повторный подбор weapon-only пикапа в игре (run08); смерть и пикап — фикстуры, независимый мир клиента.'
    },
    'П17': {
        'name': 'Барьер переходов карт и инвентарь',
        'seg': 'S5',
        'track': 'Основной путь',
        'crit': True,
        'details': 'S5.1, S5.3: общий барьер смены карты, экран путешествия, перенос инвентарей, запрет cheat map',
        'notes': 'Ждёт: П3, П10, П12.'
    },
    'П18': {
        'name': 'Кооп-сохранение на хосте',
        'seg': 'S5',
        'track': 'Основной путь',
        'crit': True,
        'details': 'S5.2: кооп-сохранение на хосте (sidecar), продолжение кампании, вход на смене карты',
        'notes': 'Ждёт: П17.'
    },
    'П19': {
        'name': 'Сквозной тест всей кампании в EX!',
        'seg': 'S4 / S5',
        'track': 'Основной путь',
        'crit': True,
        'details': 'S4.7, S5.4: пройти вдвоём всю кампанию (≈25 карт в DOC-19); регрессионная матрица карт',
        'notes': 'Ждёт: П15, П16, П18.'
    },
    'П20': {
        'name': 'Спецификация UI/достижений',
        'seg': 'S6',
        'track': 'Параллельная дорожка',
        'crit': False,
        'details': 'S6.1: спецификация 20 Steam-достижений, static 50/50, правила PR-1..PR-3 (D8)',
        'notes': 'PASS (режим А). Правила участия PR-1…PR-3 утверждены пользователем (D8).'
    },
    'П21': {
        'name': 'Лог событий и выдачи в Steam',
        'seg': 'S6',
        'track': 'Основной путь',
        'crit': True,
        'details': 'S6.2, S6.3: лог событий (A0) на хосте, связь слот↔Steam, выдача без заднего числа',
        'notes': 'Ждёт: П17, П20 и решения пользователя D4 (Steam API).'
    },
    'П22': {
        'name': 'Масштабирование до 8 игроков',
        'seg': 'S7',
        'track': 'Основной путь',
        'crit': True,
        'details': 'S7.1, S7.2: P3..P8 на хосте, 7 прокси на клиенте, слоты 2-7, нагрузочный стенд',
        'notes': 'Ждёт: П17.'
    },
    'П23': {
        'name': 'Меню, лобби-поддержка, чат, карта',
        'seg': 'S8',
        'track': 'Параллельная дорожка',
        'crit': False,
        'details': 'S8.1, S8.2, S8.3: создание/подключение в меню, имена, чат, союзники на автокарте',
        'notes': 'Ждёт: П7.'
    },
    'П24': {
        'name': 'Запуск без Frida и продакшн EXE',
        'seg': 'S9',
        'track': 'Параллельная дорожка',
        'crit': False,
        'details': 'S9.1, S9.2: загрузчик без Frida (proxy DLL или launcher), проверка SHA EXE',
        'notes': 'Ждёт: П1 и решения пользователя D5 по типу загрузчика.'
    },
    'П25': {
        'name': 'Упаковка, логи, конфиг, документация',
        'seg': 'S9',
        'track': 'Параллельная дорожка',
        'crit': False,
        'details': 'S9.3: логи, краш-дампы, конфиг, инструкция на RU/EN, набор для друзей',
        'notes': 'Ждёт: П24.'
    },
    'П27': {
        'name': 'Переносимая установка мода в игру',
        'seg': 'S9',
        'track': 'Параллельная дорожка',
        'crit': False,
        'details': 'S9.5–S9.11: копия игры друга, broker без жёстких путей, native изоляция вместо Frida, запуск и permit без REA, KEX логи/дампы, приёмка на чистой машине',
        'notes': 'Срез 1 (S9.6): native write-guard, shim v2, pe-play, срез 2 (S9.7): broker v2 сам выдаёт permit, pe-play --role, offline 85/85, RUN02 17/17 (DOC-89); лобби не проходилось. Ждёт: S9.8–S9.11; независимая приёмка В не проводилась.'
    },
    'П26': {
        'name': 'Полевые тесты с друзьями и аудит',
        'seg': 'S9',
        'track': 'Основной путь',
        'crit': True,
        'details': 'S9.4: полевые тесты с реальными игроками и финальный аудит всей системы',
        'notes': 'Финальный пакет: ждёт завершения всех остальных пакетов.'
    }
}


def find_docs_dir(base_path=None):
    """Finds the directory containing the 4 required markdown files."""
    candidates = [
        base_path,
        'docs',
        'PowerSlave_Exhumed_Together_mod/docs',
        '../docs',
        '../../docs',
        os.path.join(os.path.dirname(__file__), '../../docs'),
        os.path.join(os.path.dirname(__file__), '../../PowerSlave_Exhumed_Together_mod/docs')
    ]
    for c in candidates:
        if c and os.path.exists(c) and os.path.exists(os.path.join(c, 'ROADMAP.md')):
            return os.path.abspath(c)
    raise FileNotFoundError("Could not locate docs directory with ROADMAP.md")


def parse_clean_status(raw_status):
    """Extracts high-level status key from raw text."""
    raw_upper = raw_status.upper()
    if 'PASS' in raw_upper:
        return 'PASS'
    if 'REVIEW / PARTIAL' in raw_upper or 'PARTIAL' in raw_upper:
        return 'REVIEW / PARTIAL'
    if 'REVIEW' in raw_upper:
        return 'REVIEW'
    if 'IN PROGRESS' in raw_upper:
        return 'IN PROGRESS'
    if 'DONE' in raw_upper:
        return 'DONE'
    if 'BLOCKED' in raw_upper:
        return 'BLOCKED'
    return 'TODO'


def parse_clean_mode(raw_mode):
    """Extracts mode letter А, Б, В."""
    # The cell starts with the current mode; later letters are history ("В (поднят с Б: …)" is В).
    lead = re.match(r'\s*\**\s*([АБВABV])', raw_mode)
    if lead:
        return {'A': 'А', 'B': 'Б', 'V': 'В'}.get(lead.group(1), lead.group(1))
    if 'А' in raw_mode or 'A' in raw_mode:
        return 'А'
    if 'Б' in raw_mode or 'B' in raw_mode:
        return 'Б'
    if 'В' in raw_mode or 'V' in raw_mode:
        return 'В'
    return 'Б'


def parse_progress_percent(clean_status):
    """Computes completion percentage."""
    if clean_status == 'PASS':
        return 100
    if clean_status == 'DONE':
        return 80
    if clean_status in ['REVIEW', 'REVIEW / PARTIAL']:
        return 70
    if clean_status == 'IN PROGRESS':
        return 20
    return 0


def parse_dependencies(deps_str):
    """Extracts package IDs (Пn) and decision IDs (Dn) from dependencies string."""
    pkg_deps = re.findall(r'П\d+', deps_str)
    decision_deps = re.findall(r'D\d+', deps_str)
    if 'остальные' in deps_str.lower():
        # П26 depends on all other packages (П1..П25)
        pkg_deps = [f'П{i}' for i in range(1, 26)]
    return pkg_deps, decision_deps


def compute_auto_readiness(pkg_id, clean_status, clean_mode, pkg_deps, decision_deps, completed_pkgs, accepted_decisions):
    """
    Computes Column J (Готовность к старту (Авто)):
    - PASS -> Завершён (PASS)
    - DONE -> Завершён (DONE)
    - REVIEW / PARTIAL -> В ревью
    - REVIEW -> В ревью
    - IN PROGRESS -> В работе
    - TODO -> Check dependencies!
      If all pkg_deps in completed_pkgs and all decision_deps in accepted_decisions:
        -> ГОТОВ К СТАРТУ
      Else:
        -> Ждёт: <missing>
    """
    if clean_status == 'PASS':
        return 'Завершён (PASS)', 'pass'
    if clean_status == 'DONE':
        return 'Завершён (DONE)', 'done'
    if clean_status in ['REVIEW', 'REVIEW / PARTIAL']:
        return 'В ревью', 'review'
    if clean_status == 'IN PROGRESS':
        return 'В работе', 'in_progress'

    # clean_status == 'TODO'
    missing = []
    for p in pkg_deps:
        if p not in completed_pkgs:
            missing.append(p)

    for d in decision_deps:
        if d not in accepted_decisions:
            missing.append(d)

    if not missing:
        return 'ГОТОВ К СТАРТУ', 'ready'
    else:
        return f"Ждёт: {', '.join(missing)}", 'waiting'


def parse_all_4_files(docs_dir=None):
    """
    Reads strictly the 4 files:
    1. CURRENT_STATE.md
    2. ROADMAP.md
    3. PROGRESS.md
    4. EVIDENCE_INDEX.md
    """
    docs_path = find_docs_dir(docs_dir)

    roadmap_file = os.path.join(docs_path, 'ROADMAP.md')
    current_state_file = os.path.join(docs_path, 'CURRENT_STATE.md')
    progress_file = os.path.join(docs_path, 'PROGRESS.md')
    evidence_file = os.path.join(docs_path, 'EVIDENCE_INDEX.md')

    with open(roadmap_file, 'r', encoding='utf-8') as f:
        roadmap_content = f.read()

    with open(current_state_file, 'r', encoding='utf-8') as f:
        current_state_content = f.read()

    with open(progress_file, 'r', encoding='utf-8') as f:
        progress_content = f.read()

    with open(evidence_file, 'r', encoding='utf-8') as f:
        evidence_content = f.read()

    mtimes = {
        'ROADMAP.md': datetime.fromtimestamp(os.path.getmtime(roadmap_file)).strftime('%d.%m.%Y %H:%M:%S'),
        'CURRENT_STATE.md': datetime.fromtimestamp(os.path.getmtime(current_state_file)).strftime('%d.%m.%Y %H:%M:%S'),
        'PROGRESS.md': datetime.fromtimestamp(os.path.getmtime(progress_file)).strftime('%d.%m.%Y %H:%M:%S'),
        'EVIDENCE_INDEX.md': datetime.fromtimestamp(os.path.getmtime(evidence_file)).strftime('%d.%m.%Y %H:%M:%S'),
    }

    # 1. Parse User Decisions from ROADMAP.md §6
    decisions = []
    accepted_decisions = set()
    dec_match = re.search(r'## 6\. Решения пользователя\s*\n\s*.*?\n(\|.*?\n\|[-| :]+\n(?:\|.*?\n)+)', roadmap_content)
    if dec_match:
        dec_lines = dec_match.group(1).strip().split('\n')[2:]
        for line in dec_lines:
            cols = [c.strip() for c in line.split('|')[1:-1]]
            if len(cols) >= 3:
                did = cols[0]
                desc = cols[1]
                when = cols[2]
                is_accepted = 'принято' in when.lower() or 'принят' in when.lower()
                if is_accepted:
                    accepted_decisions.add(did)
                decisions.append({
                    'id': did,
                    'desc': desc,
                    'when': when,
                    'accepted': is_accepted
                })

    # 2. Parse Raw Packages Table from ROADMAP.md §4.1
    pkg_pattern = r'\| (П\d+) \| (.*?) \| (.*?) \| (.*?) \| (.*?) \| (.*?) \| (.*?) \|'
    raw_packages = []
    completed_pkgs = set(['S0.3'])  # S0.3 is marked PASS in roadmap

    for match in re.finditer(pkg_pattern, roadmap_content):
        pid, tasks, assignee, mode, reviewer, deps, status = [x.strip() for x in match.groups()]
        clean_status = parse_clean_status(status)
        clean_mode = parse_clean_mode(mode)

        # In Mode Б, DONE counts as completed for unlocking dependents
        if clean_status == 'PASS' or (clean_mode == 'Б' and clean_status == 'DONE'):
            completed_pkgs.add(pid)

        raw_packages.append({
            'id': pid,
            'tasks': tasks,
            'assignee': assignee,
            'mode_raw': mode,
            'mode': clean_mode,
            'reviewer': reviewer,
            'deps_raw': deps,
            'status_raw': status,
            'status': clean_status,
        })

    # 3. Parse Recent Package Progress logs from PROGRESS.md
    pkg_progress = {}
    progress_sections = re.split(r'\n(?=## )', progress_content)
    for sec in progress_sections[1:]:
        lines = sec.strip().split('\n')
        if not lines:
            continue
        hdr = lines[0].replace('## ', '').strip()
        m = re.search(r'П(\d+)', hdr)
        if m:
            pid = f"П{m.group(1)}"
            if pid not in pkg_progress:
                body = '\n'.join(lines[1:]).strip()
                pkg_progress[pid] = {
                    'header': hdr,
                    'body': body[:800] + ('...' if len(body) > 800 else '')
                }

    # 4. Parse Evidence Links from EVIDENCE_INDEX.md
    pkg_evidence = {}
    evidence_sections = re.split(r'\n(?=## )', evidence_content)
    for sec in evidence_sections[1:]:
        lines = sec.strip().split('\n')
        if not lines:
            continue
        hdr = lines[0].replace('## ', '').strip()
        m = re.search(r'П(\d+)', hdr)
        if m:
            pid = f"П{m.group(1)}"
            if pid not in pkg_evidence:
                links = re.findall(r'\[(.*?)\]\((.*?)\)', sec)
                pkg_evidence[pid] = {
                    'header': hdr,
                    'links': [{'title': l[0], 'url': l[1]} for l in links[:12]]
                }

    # 5. Build Complete Packages List (Matching Screenshot 2 Columns A-O)
    packages = []
    for raw in raw_packages:
        pid = raw['id']
        cat = PACKAGE_CATALOG.get(pid, {})
        pkg_deps, dec_deps = parse_dependencies(raw['deps_raw'])

        readiness_text, readiness_state = compute_auto_readiness(
            pid, raw['status'], raw['mode'], pkg_deps, dec_deps, completed_pkgs, accepted_decisions
        )

        progress_pct = parse_progress_percent(raw['status'])

        # Build prompt template for this package (from ROADMAP §1)
        prompt_text = (
            f"Роль: исполнитель и проверяющий пакета {pid} ({cat.get('name', '')}) из docs/ROADMAP.md, режим {raw['mode']}.\n"
            f"Главная задача: достичь PASS.\n\n"
            f"Прочитай перед работой:\n"
            f"1. AGENTS.md\n"
            f"2. docs/CURRENT_STATE.md\n"
            f"3. Строку пакета {pid} в ROADMAP §4.1, его задачи ({raw['tasks']}) в §4.2 и документы из колонки «Опора».\n\n"
            f"Объём: только задачи пакета {pid}. EXE SHA-256 b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d. Принятые исследования и runs не повторять.\n"
            f"Критерии готовности («Готово, когда»): {cat.get('details', '')}.\n\n"
            f"Порядок сдачи и приёмки:\n"
            f"Выполнить задачи пакета и провести полную валидацию/приёмку по режиму {raw['mode']} (ROADMAP §1, п. 4). "
            f"Проверить evidence, CTest/offline проверки, неизменность 77 оригинальных файлов игры и exact restores хуков.\n"
            f"Обновить статусы пакета и задач до PASS в docs/ROADMAP.md, docs/PROGRESS.md и docs/CURRENT_STATE.md."
        )

        review_prompt_text = (
            f"Роль: исполнитель и проверяющий пакета {pid} ({cat.get('name', '')}) из docs/ROADMAP.md, режим {raw['mode']}.\n"
            f"Главная задача: достичь PASS.\n\n"
            f"Прочитай перед работой:\n"
            f"1. AGENTS.md\n"
            f"2. docs/CURRENT_STATE.md\n"
            f"3. Строку пакета {pid} в ROADMAP §4.1, отчёт исполнителя и evidence в docs/evidence/.\n\n"
            f"Предмет проверки и доработки:\n"
            f"Провести независимую верификацию результатов {pid}: валидация evidence, CTest/offline проверок, логов прогонов, неизменности 77 оригинальных файлов игры и exact restores хуков. При необходимости внести исправления.\n\n"
            f"Порядок сдачи:\n"
            f"Оформить результат приёмки (при режиме В — отчёт DOC в docs/), обновить статусы в ROADMAP, PROGRESS и EVIDENCE_INDEX. "
            f"Перевести пакет в итоговый статус PASS."
        )

        packages.append({
            'col_a_id': pid,
            'col_b_name': cat.get('name', f"Пакет {pid}"),
            'col_c_seg': cat.get('seg', 'S1'),
            'col_d_tasks': raw['tasks'],
            'col_e_track': cat.get('track', 'Основной путь'),
            'col_f_status': raw['status'],
            'col_f_status_raw': raw['status_raw'],
            'col_g_mode': raw['mode'],
            'col_g_mode_raw': raw['mode_raw'],
            'col_h_progress': progress_pct,
            'col_i_scale': progress_pct,
            'col_j_readiness': readiness_text,
            'col_j_readiness_state': readiness_state,
            'col_k_assignee': raw['assignee'],
            'col_l_reviewer': raw['reviewer'],
            'col_m_deps': raw['deps_raw'],
            'col_m_pkg_deps': pkg_deps,
            'col_m_dec_deps': dec_deps,
            'col_n_details': cat.get('details', ''),
            'col_o_notes': cat.get('notes', ''),
            'is_critical': cat.get('crit', False),
            'prompt_template': prompt_text,
            'review_prompt_template': review_prompt_text,
            'latest_progress': pkg_progress.get(pid, None),
            'evidence': pkg_evidence.get(pid, None)
        })

    # 6. Parse Tasks by Segment from ROADMAP.md §4.2
    tasks = []
    tasks_sec = roadmap_content.split('### 4.2 Задачи по сегментам')[1].split('## 5. Критический путь')[0]
    task_pattern = r'^\s*\|\s*(S\d+\.\d+)\s*\|\s*(.*?)\s*\|\s*(.*?)\s*\|\s*(.*?)\s*\|\s*(.*?)\s*\|'
    for line in tasks_sec.split('\n'):
        m = re.match(task_pattern, line)
        if m:
            tid, desc, pkg, ref, status = [x.strip() for x in m.groups()]
            clean_tstatus = parse_clean_status(status)
            seg = tid.split('.')[0]
            tasks.append({
                'id': tid,
                'seg': seg,
                'desc': desc,
                'package': pkg,
                'ref': ref,
                'status_raw': status,
                'status': clean_tstatus
            })

    # 7. Parse Milestones from ROADMAP.md §7
    milestones = []
    readiness_sec = roadmap_content.split('## 7. Оценка готовности (INFERRED)')[1].split('## Приложение A')[0]
    for line in readiness_sec.split('\n'):
        m = re.match(r'^\s*\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|', line)
        if m:
            name, val = [x.strip() for x in m.groups()]
            if 'Веха' not in name and '---' not in name:
                milestones.append({'name': name, 'ready': val})

    # 8. Parse Frozen Artifacts from CURRENT_STATE.md
    artifacts = []
    art_sec = current_state_content.split('## Замороженные артефакты')[1].split('## Открыто')[0]
    for line in art_sec.split('\n'):
        m = re.match(r'^\s*\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|', line)
        if m:
            what, path, sha = [x.strip() for x in m.groups()]
            if 'Что' not in what and '---' not in what:
                artifacts.append({'what': what, 'path': path, 'sha': sha})

    # 9. Parse Alerts, What Works, Open Items, Next Steps from CURRENT_STATE.md
    top_alerts = [l[1:].strip() for l in current_state_content.split('\n') if l.startswith('> ')]

    def extract_bullets(sec_name, next_sec):
        try:
            part = current_state_content.split(f'## {sec_name}')[1].split(f'## {next_sec}')[0]
            return [l.strip()[2:] for l in part.split('\n') if l.strip().startswith('- ')]
        except Exception:
            return []

    what_works = extract_bullets('Что работает (в узких границах)', 'Замороженные артефакты')
    open_hazards = extract_bullets('Открыто', 'Следующие шаги')
    next_steps = extract_bullets('Следующие шаги', 'Как запускать')

    # 10. Compute Summary Statistics for Dashboard & Table Footer (Row 31: ИТОГО)
    total_packages = len(packages)
    completed_count = sum(1 for p in packages if p['col_f_status'] == 'PASS')
    review_count = sum(1 for p in packages if 'REVIEW' in p['col_f_status'])
    in_progress_count = sum(1 for p in packages if p['col_f_status'] == 'IN PROGRESS')
    ready_count = sum(1 for p in packages if p['col_j_readiness_state'] == 'ready')
    critical_path_count = sum(1 for p in packages if p['is_critical'])

    mode_a_count = sum(1 for p in packages if p['col_g_mode'] == 'А')
    mode_b_count = sum(1 for p in packages if p['col_g_mode'] == 'Б')
    mode_v_count = sum(1 for p in packages if p['col_g_mode'] == 'В')

    # Recommended next actions (Whom to give what task right now!)
    recommended_actions = []

    # A) Ready to start tasks
    for p in packages:
        if p['col_j_readiness_state'] == 'ready':
            recommended_actions.append({
                'priority': 'HIGH',
                'type': 'START_PACKAGE',
                'badge': 'ГОТОВ К СТАРТУ',
                'badge_color': 'green',
                'package_id': p['col_a_id'],
                'package_name': p['col_b_name'],
                'assignee': p['col_k_assignee'],
                'reviewer': p['col_l_reviewer'],
                'mode': p['col_g_mode'],
                'track': p['col_e_track'],
                'prompt': p['prompt_template'],
                'reason': f"Все зависимости ({p['col_m_deps']}) выполнены! Можно запускать прямо сейчас."
            })

    # B) Pending reviews in mode В (Blocking!)
    for p in packages:
        if 'REVIEW' in p['col_f_status']:
            recommended_actions.append({
                'priority': 'BLOCKING',
                'type': 'REVIEW_PACKAGE',
                'badge': 'ТРЕБУЕТСЯ РЕВЬЮ',
                'badge_color': 'amber',
                'package_id': p['col_a_id'],
                'package_name': p['col_b_name'],
                'assignee': p['col_k_assignee'],
                'reviewer': p['col_l_reviewer'],
                'mode': p['col_g_mode'],
                'track': p['col_e_track'],
                'prompt': p['review_prompt_template'],
                'reason': f"Пакет {p['col_a_id']} ({p['col_f_status']}) требует проверки: {p['col_l_reviewer']}."
            })

    # C) Pending user decisions
    for d in decisions:
        if not d['accepted'] and d['id'] in ['D2', 'D3', 'D4', 'D5', 'D6']:
            recommended_actions.append({
                'priority': 'DECISION',
                'type': 'USER_DECISION',
                'badge': f"РЕШЕНИЕ {d['id']}",
                'badge_color': 'blue',
                'package_id': d['id'],
                'package_name': d['desc'],
                'assignee': 'Пользователь',
                'reviewer': '—',
                'mode': '—',
                'track': 'Управление',
                'prompt': f"Принять решение по {d['id']}: {d['desc']}. Условие: {d['when']}.",
                'reason': f"Требуется решение пользователя: {d['when']}."
            })

    return {
        'parsed_at': datetime.now().strftime('%d.%m.%Y %H:%M:%S'),
        'mtimes': mtimes,
        'docs_dir': docs_path,
        'summary': {
            'total_packages': total_packages,
            'completed_count': completed_count,
            'review_count': review_count,
            'in_progress_count': in_progress_count,
            'ready_count': ready_count,
            'critical_path_count': critical_path_count,
            'total_tasks': len(tasks),
            'progress_percent_overall': 20.0,  # From CURRENT_STATE & ROADMAP §7 (~20%)
            'mode_counts': {
                'А': mode_a_count,
                'Б': mode_b_count,
                'В': mode_v_count
            }
        },
        'packages': packages,
        'tasks': tasks,
        'decisions': decisions,
        'milestones': milestones,
        'artifacts': artifacts,
        'top_alerts': top_alerts,
        'what_works': what_works,
        'open_hazards': open_hazards,
        'next_steps': next_steps,
        'recommended_actions': recommended_actions,
        'roles_guide': [
            {
                'model': 'Opus',
                'role': 'ADR, прогоны в игре через REA MCP (Frida), главный рецензент и блокирующая приёмка',
                'levels': 'max — ADR и gates с риском lifetime/UAF; extra — обычные нативные gates; high — ревью'
            },
            {
                'model': 'GPT 6.1 Sol',
                'role': 'Нативный реверс, детуры, хуки и C++ в DLL (вёл M0 и нативную часть M1)',
                'levels': 'max — новая подсистема с неизвестным ABI/lifetime; extra — расширение отработанных шаблонов; high — точечные правки'
            },
            {
                'model': 'Sonnet',
                'role': 'Объёмная работа по готовым шаблонам: консолидация, offline-тесты, AngelScript, UI, упаковка, документация',
                'levels': 'high — код; medium — документы и индексы'
            },
            {
                'model': 'Gemini',
                'role': 'Прототипы и тестовые стенды (ENet-прототип, QA-клиенты), чтение больших объёмов, ассеты, второе мнение',
                'levels': 'high — стенды, тесты, ассеты; max — второе мнение по протоколу и архитектуре'
            }
        ],
        'modes_guide': [
            {
                'mode': 'А — самоприёмка',
                'desc': 'Исполнитель сам прогоняет машинные проверки (сборка, тесты, хеши, воспроизведение) и ставит PASS',
                'condition': 'Результат проверяется машинно, изменения обратимы (git), память и lifetime не затронуты'
            },
            {
                'mode': 'Б — самоприёмка с неблокирующей проверкой',
                'desc': 'Исполнитель ставит DONE, следующие пакеты стартуют сразу; другая модель проверяет пачкой',
                'condition': 'Нативный код Opus или Sol по принятым шаблонам; ошибка тихая, но обратима'
            },
            {
                'mode': 'В — блокирующая проверка',
                'desc': 'Исполнитель ставит REVIEW; зависимые пакеты ждут, пока другая модель не поставит PASS',
                'condition': 'Архитектурные решения (ADR); проба мира; внешние эффекты (Steam, оригинал); новая нативная логика от Sonnet/Gemini'
            }
        ]
    }


if __name__ == '__main__':
    data = parse_all_4_files()
    print(f"Successfully parsed data from 4 files at: {data['parsed_at']}")
    print(f"Total packages: {data['summary']['total_packages']}")
    print(f"Completed: {data['summary']['completed_count']}")
    print(f"Ready to start: {data['summary']['ready_count']}")
    print(f"In review: {data['summary']['review_count']}")
    print(f"Recommended actions: {len(data['recommended_actions'])}")
    for a in data['recommended_actions']:
        print(f" - [{a['priority']}] {a['package_id']} ({a['assignee']}): {a['reason']}")
