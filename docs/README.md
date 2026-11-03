# PowerSlave Exhumed Together — исследовательский пакет

> Обновлено 2026-11-03 (S0.1). Текущее состояние — [CURRENT_STATE](CURRENT_STATE.md), план — [ROADMAP](ROADMAP.md), журнал — [PROGRESS](PROGRESS.md), действующие правила — [AGENTS.md](../AGENTS.md). Документы 00–10 ниже — исходный исследовательский пакет (2026-10-31); их ограничения «только M0 / без сети» устарели.

Актуальная цель: 2–8 игроков включая хоста, listen server/host authority, LAN/Radmin VPN, ENet; исходная кампания и Shared Achievement Progress всех 20 Steam-достижений. M0 принят ([DOC-27](27_M0_COMPLETION_REPORT.md)), сеть реализуется (DOC-44…58B.2); Steam-достижения не трогать до решения пользователя D4.

Начать с **[CURRENT_STATE](CURRENT_STATE.md)** и **[ROADMAP](ROADMAP.md)**. [10_OPUS_HANDOFF.md](10_OPUS_HANDOFF.md) — исходный исследовательский handoff: технические находки, Full Player/Proxy, последствия singleton Spawn, Steam conditions/unlock.

| Документ | Назначение |
|---|---|
| [CURRENT_STATE](CURRENT_STATE.md) | Где проект сейчас: принятое, замороженные артефакты, открытое, следующие шаги |
| [ROADMAP](ROADMAP.md) | План S0–S9, роли моделей, решения пользователя, оценка готовности |
| [00_SCOPE](00_SCOPE.md) | Границы и VERIFIED/INFERRED/UNKNOWN |
| [01_BINARY_MANIFEST](01_BINARY_MANIFEST.md) | Бинарники, SHA, форматы, imports/exports, игровые данные |
| [02_ENGINE_ARCHITECTURE](02_ENGINE_ARCHITECTURE.md) | State/update, nodes, lifetime, RNG |
| [03_PLAYER_LIFECYCLE](03_PLAYER_LIFECYCLE.md) | Player/Puppet, поля, singleton и независимость |
| [04_ACTORS_AI](04_ACTORS_AI.md) | AI/цели, мир, host authority |
| [05_PHYSICS_WEAPONS_DAMAGE](05_PHYSICS_WEAPONS_DAMAGE.md) | Collision, owner, damage и anim actions |
| [06_LEVELS_CAMPAIGN_SAVE](06_LEVELS_CAMPAIGN_SAVE.md) | Map transitions, campaign, save/load |
| [07_HOOK_CANDIDATES](07_HOOK_CANDIDATES.md) | VA/RVA/file offset, аргументы-гипотезы, риск и runtime check каждого critical candidate |
| [08_NETWORK_ARCHITECTURE](08_NETWORK_ARCHITECTURE.md) | Только минимальные требования сети/восьми игроков |
| [09_POC_PLAN](09_POC_PLAN.md) | M0 эксперименты, gates M0–M5 |
| [EVIDENCE_INDEX](EVIDENCE_INDEX.md) | Источники и навигация по raw outputs |
| [OPEN_QUESTIONS](OPEN_QUESTIONS.md) | Незакрытые вопросы и минимальные проверки |
| [PROGRESS](PROGRESS.md) | Выполнено, блокеры, граница завершения и возобновление |

Прямой Ghidra fallback работал; REA Ghidra provider остаётся несовместимым с установленной12.2. Его исправление не требуется для передачи. Псевдокод не исходники. Крупные JSON читать выборочно через каталоги, не загружать весь strings/xrefs inventory в контекст.

В research сохранены проверенные source-only референсы и исторический сетевой эскиз. Они не заменяют актуальные требования 08–10 или Evidence2022. Ничего из них не является реализацией мода.
