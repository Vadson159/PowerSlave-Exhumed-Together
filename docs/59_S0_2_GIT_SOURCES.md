# DOC-59 / S0.2 — git для исходников

> 2026-11-03, Sonnet 5.5 (medium). Задача S0.2 из [ROADMAP](ROADMAP.md); решение D1 принято постановкой задачи пользователем. Runtime не выполнялся, DLL/evidence/игра не менялись. EXE SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d` не затрагивался.

## Сделано (VERIFIED, по `git` в этой папке)

- `git init -b main` в корне мода (`PowerSlave_Exhumed_Together_mod`). Репозиторий — только внутри этой папки; референсы выше (`S:\_Vibe_Coding\PowerSlave_Exhumed_Together`) и установка игры не тронуты. `core.autocrlf=false` (локально).
- Добавлен [`.gitignore`](../.gitignore) (SHA-256 `c5f2995d…`): исключает evidence, копии игры, профили, сборки, логи и сгенерированные данные.
- Первый коммит: 692 файла, ≈3,9 МиБ в объектах (`git count-objects`). Состав: `docs/` 115 (отчёты, ROADMAP, PROGRESS, `docs/research/`, `docs/presentation/`), `experiments/` 364, `tools/` 210, `AGENTS.md`, `.gitignore`.

## Что вне git (правила `.gitignore`)

| Категория | Правила |
|---|---|
| Evidence и рабочие копии | `docs/evidence/`, `docs/doc57_work/`, `experiments/kpf_extracted/` |
| Игра и профили | `**/game/`, `**/profile/`, `**/profiles/` (≈1 ГБ копий EXE) |
| Сборки | `**/build/`, `**/build_*/`, `**/Release/`, `**/Debug/`, `**/x64/`, `**/CMakeFiles/`, `**/_deps/`, `/b[0-9]*/` (корневые `b58`, `b581`, `b582`) |
| Прогоны и вывод | `**/logs/`, `**/runs/`, `**/run_*/`, `**/scratch/`, `**/ghidra_runtime/`, `tools/m12b-input-cadence-qa/data/`, `tools/multiclient-test-runner/output/`, `docs/research/runtime/`, `docs/research/ghidra_projects/` |
| Типы файлов | `*.exe *.dll *.pdb *.obj *.lib *.pyc *.class *.pack *.kpf *.zip *.pcap *.dmp *.log *.jsonl *.csv` |
| Вендор | `tools/enet-prototype/third_party/enet/` — у него собственный `.git` |

Проверка: в индексе 0 файлов `.dll/.exe/.pdb/.obj`, 0 файлов из `docs/evidence`, `b5*`, `kpf_extracted`.

## Ограничения и открытое

- **ENet вендорный.** `third_party/enet` — отдельный git-репозиторий (HEAD `2662c0de09e36f2a2030ccc2c528a3e4c9e8138a`, 2024-04-14), в мой репозиторий не вложен, чтобы не создавать gitlink без URL. Если нужно версионировать — завести submodule (нужен URL от пользователя) или копировать исходники ENet.
- **Evidence и DLL не версионируются:** их хеши фиксируются в [CURRENT_STATE](CURRENT_STATE.md) и [EVIDENCE_INDEX](EVIDENCE_INDEX.md); потеря каталога evidence в git не восстанавливается — нужен отдельный бэкап (решение пользователя).
- Из `*.csv`, `*.jsonl`, `*.log` в git не попали и мелкие исходные данные тестов; если какой-то такой файл окажется входом сборки/теста (не обнаружено при выборке), добавить исключение `!path` в `.gitignore`.
- Коммит не проверял сборку из чистого клона (S0.3 — единый код и одна команда для offline-сьютов).
- Удалённого репозитория нет; push не выполнялся.
- Проверяющий для S0.2 в ROADMAP не назначен.

## Воспроизведение

```bash
cd PowerSlave_Exhumed_Together_mod
git log --stat -1 | tail -3
git ls-files | wc -l          # 692
git status --short --ignored  # ignored = evidence/game/profile/build
```
