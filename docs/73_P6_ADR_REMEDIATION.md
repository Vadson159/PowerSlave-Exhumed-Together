# DOC-73 — П6: доработка ADR-1/ADR-2 после проверки Sol

2026-11-04. Исполнитель доработки — Codex. Только пакет **П6 / S2.1, S3.1**, режим **В**. Статус: **REVIEW**, передан на повторную независимую проверку; PASS не выставлен. Рекомендации — A и W1; решения пользователя D2/D3 ещё не приняты. D3b требуется отдельно до Remove реплик объектов карты.

Основной результат — [DOC-62, редакция 2](62_P6_ADR_AUTHORITY_AND_CLIENT_WORLD.md). Предыдущая редакция сохранена байт-в-байт в [DOC62_BEFORE.md](evidence/m1/P6_ADR_REWORK/DOC62_BEFORE.md); прежний [независимый review](62_P6_REVIEW_SOL.md) и принятые evidence не изменены. Для повторной проверки замечания сопоставлены с конкретными правилами ниже.

## 1. Что изменено

| Замечание | Новый контракт | Проверка/предел |
|---|---|---|
| F1: seq не является временем состояния | DOC-62 §3.4 T1/T2: post-tick position/velocity/sector на Sim TID, stateTick и реальные монотонные stateTime; история по времени через ограниченную оценку offset/uncertainty. Held/new/neutral и coalesced input остаются latest-state, seq — диагностика | Численные примеры одного seq на нескольких тиках, neutral без seq, coalescing, интерполяция по времени. Реальная точка native hook, clock quality и единицы velocity — П7 |
| F1: повтор уже внесённой поправки | T3/T4: H_eff = H_position + C_now − C_at_record; C учитывает только фактический сдвиг. Новый snapshot заменяет остаток, не суммирует ошибки | Два старых samples после полной/половинной поправки; новая history после сдвига; частичный фактический сдвиг. Это компенсированное сравнение без replay, применимость к KEX — gate П7 |
| F1: потеря history/discontinuity | T5: WAIT_ANCHOR, надёжный resetToken/anchor/ack; host neutral, закрытие старого input stream, свежесть anchor. Движение клиента после времени anchor отбрасывается; не проигрывается и не переносится | Численные отрицательные случаи pause/uncertainty/stale history/clock jump, старых token/разрыва/anchor. Остаточная ошибка из-за движения мира при neutral измеряется в П7 |
| F2: scan не доказывает происхождение | R1/R2: захват конкретного фабричного вызова с валидированным record pointer текущего LoadMap, стек вложенных вызовов, фактический Actor/node, подтверждение после Spawn. Scan — только сверка | Неверные указатель/выравнивание, scan-only, ребёнок by-type, смена node/epoch/поколения, повтор index → отказ. Полнота observer/ABI — П9 |
| F2: поколение MAP-ID не защищает локальную память | Сетевое generation=1 означает одну активацию записи за эпоху. Локально проверяются heap alloc seq **и** activationGeneration при acquire из пула; повторное создание записи в той же эпохе не поддерживается | Сценарий reuse при прежнем alloc seq отклоняется по activationGeneration. Ручные refs/target/deletion changes запрещены |
| F3: равный manifest не означает равные данные | C1: закрытый фактически используемый профиль ресурсов, SHA распакованной PSM, whole-file hashes всех упорядоченных mounts/loose overrides, config/сложность/exact EXE. M1: отдельный канонический manifest по captured registry | Изменение координат/геометрии/defs при прежних manifest/counts меняет contentDigest; порядок mounts и loose override также проверяются. Fixtures не являются валидными PSM/KPF |
| F3: канонизация и точка фиксации | C1 — versioned canonical UTF-8 JSON; M1 — точный LE binary layout, сортировка recordIndex, type 0 исключён, totalRecordCount сохранён. Фиксация после LoadMap/вложенных Spawn до gameplay/startup/scripts/network mutations | В evidence сохранены descriptor bytes, manifest hex и оба SHA; равный content/разный manifest тоже закрывает gate. Suppression до LoadMap и side effects Spawn проверяет П9 |
| N1: redundant input не гарантирует совпадение | T1 сохраняет latest-state; избыточные samples не обязательны и не обещают восстановления пропущенного тика/edge | Не добавлена очередь replay и не изменён принятый транспорт |
| N2: D9 разрешил только identity-only v1.11 | §1/§3.6/§5 отделяют П6-предложение от D9. Time-sync/reset/content/manifest/discontinuity и прочие поля требуют отдельного решения о версии/формате | v1.10 и v1.11 не менялись; receipt v1.11 и SHA принятого v1.10 проверены |
| N3: мир может менять взгляд | T6: на новом разрыве применить авторитетный yaw/pitch один раз; старые дельты мыши отбросить, новые добавить к новой базе | Новый локальный ввод и duplicate anchor не возвращают старый угол |
| N4: прямой порядок фабрики не равен всем spawn | §4.3 ограничен VERIFIED static прямыми вызовами ReadActors, фильтрами и записью индекса. Вложенные class Spawn/startup/RNG остаются UNKNOWN | Переиспользованы прежние 25/25, без расширения утверждения до runtime или 25 карт |

## 2. Доказательства

Целевой EXE SHA-256: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`. Три файла EXE (две тестовые копии и оригинал только для чтения) заново **прохешированы**, игру не запускали.

Принятая статика S3.1 [25/25](evidence/m1/P6_ADR_AUTHORITY_WORLD/MAP_SPAWN_IDENTITY_STATIC.json) и [подтверждение Sol](evidence/m1/P6_ACCEPTANCE_REVIEW_SOL/MAP_SPAWN_IDENTITY_STATIC.json) переиспользованы по совпавшим SHA входов/скрипта и профилю Ghidra 12.2 / `x86:LE:64:default` / `windows` / base `0x140000000`. Проверка порядка фабрики заново не выполнялась. 25 строк включают INFO; это не 25 карт и не наблюдение двух одинаковых миров. Мутационный результат первого автора 3/3 не воспроизводился; его ограничение сохранено.

- [REUSE_CHECKS.json](evidence/m1/P6_ADR_REWORK/REUSE_CHECKS.json): **141/141** сверок SHA, exact EXE, сохранённых summaries и профилей; wire v1.11 совпадает с [первым receipt доработки](evidence/m1/P6_ADR_REWORK/WIRE111_BASELINE.json).
- [CONTRACT_EXAMPLES.json](evidence/m1/P6_ADR_REWORK/CONTRACT_EXAMPLES.json): **53/53** численных/логических примеров, включая отрицательные; canonical manifest/content vectors. Метка **INFERRED**, никакой native/runtime-приёмки.
- [Скрипт](research/p6_adr_rework_checks.py), [raw output](evidence/m1/P6_ADR_REWORK/CHECKS.log), [ссылки](evidence/m1/P6_ADR_REWORK/LINK_CHECK.json).
- [Координаты функций](evidence/m1/P6_ACCEPTANCE_REVIEW_SOL/FUNCTION_COORDINATES.json) (VA/RVA/file offset), [принятые статические выборки](evidence/m1/P6_ACCEPTANCE_REVIEW_SOL/STATIC_EXCERPTS.json), [исходные контрпримеры review](evidence/m1/P6_ACCEPTANCE_REVIEW_SOL/CONTRACT_COUNTEREXAMPLES.json). Сигнатуры Ghidra не считаются восстановленным native API; новых адресов и вызовов нет.

Первый проход ссылок нашёл три ссылки на ещё не созданный собственный output LINK_CHECK.json; результат сохранён в [LINK_CHECK_FIRST_PASS](evidence/m1/P6_ADR_REWORK/LINK_CHECK_FIRST_PASS.json). После появления файла повторная проверка всех шести документов дала 0 broken; проверки контрактов/переиспользования не падали.

Игра, Ghidra, REA/Frida, сеть и Steam не запускались. Изменены только документы П6/сводки и его offline-checker/evidence. Код/DLL/принятые raw logs, wire v1.10/v1.11, AGENTS, оригинал и защищённые профили не менялись. Собственных игровых процессов/REA-сессий не создавалось; чужие процессы не закрывались. Проверка оригинальных 77 файлов после runtime не нужна: runtime в этом пакете отсутствует.

## 3. Что требуется от приёмки

Независимая модель проверяет исправления F1–F3/N1–N4 и выдаёт вердикт по каждому ADR; прежний review не перезаписывает. Нужны также мнение Gemini и затем D2/D3. Утверждения о достижимой плавности/частоте snap, полноте подавления и retirement выбранных классов остаются UNKNOWN/gates П7/П9; их не требуется выдавать за закрытые в ADR.

До PASS П6 зависимые П7/П9 не разблокированы. Даже после выбора A/W1 реализация полей П6 требует отдельного разрешения на расширение протокола за пределами D9; нативный Remove MAP-реплик требует D3b и ограниченного class gate П9. Это границы уже принятого процесса/правил, не запрос разрешения на текущую документационную доработку.

П6, S2.1 и S3.1 переведены из **IN PROGRESS** в **REVIEW**; обновлены ROADMAP, PROGRESS, EVIDENCE_INDEX и CURRENT_STATE. Режим В сохранён, самоприёмки нет.

## 4. Воспроизведение новых проверок

Из корня мода, без запуска игры:

```text
python -X utf8 -I docs/research/p6_adr_rework_checks.py
python -X utf8 -I docs/research/check_md_links.py docs/62_P6_ADR_AUTHORITY_AND_CLIENT_WORLD.md docs/73_P6_ADR_REMEDIATION.md docs/ROADMAP.md docs/CURRENT_STATE.md docs/PROGRESS.md docs/EVIDENCE_INDEX.md --out docs/evidence/m1/P6_ADR_REWORK/LINK_CHECK.json
```

Первый скрипт читает принятые dossiers, receipts и EXE; пишет только новые результаты в P6_ADR_REWORK. Нативные offline-наборы/сборки и старые Ghidra/static checker/runs повторять для этой доработки не нужно.
