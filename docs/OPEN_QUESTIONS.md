# Незакрытые вопросы

Порог текущего этапа достигнут: для перечисленного нужны узкие прототипы/наблюдения, а не массовая декомпиляция. Никакая строка UNKNOWN не означает доказанную невозможность.

**M0 runtime update:** [C01](evidence/m0/C01/RESULT.md) частично закрыл Q01/Q03/Q04/Q05: separate context/native controller/synthetic commands/serial collisions и bounded Puppet retirement наблюдались. Full Player стал подтверждённым экспериментальным кандидатом; общий lifecycle и production input остаются UNKNOWN. Новые priority questions: actual weapon hit/instigator, ammo/projectiles, fresh-process repeat, death/respawn/map teardown, complete slot lifetime; C4 tick scheduling/desktop cause. Не повторять B или static ctor discovery без изменения SHA. Полный M0 не принят, M0-D не запускался.

| ID | Вопрос / статус | Имеющееся Evidence | Минимальная следующая проверка |
|---|---|---|---|
| Q01 | UNKNOWN: независимый Player lifecycle | Spawn singleton, embedded Player, owner-dependent paths | M0-C: корректный init/weapon/handle/teardown второго context; не memcpy |
| Q02 | UNKNOWN: Full Player vs Proxy | Native Puppet/pickups требуют owner/type; общая actor physics существует | Сравнить M0-C/D по одинаковым gameplay критериям; Full Player первый кандидат |
| Q03 | UNKNOWN: remote input injection | Input candidates используют platform globals; Player buttons+0 | Два synthetic streams, opposite movement/turn, отсутствие shared history/read consumption |
| Q04 | UNKNOWN: threading/tick frequency | Guarded state tick, callback stamp | Логировать реальный thread/tick/frame/pause; не предполагать 30/60Hz |
| Q05 | UNKNOWN: collision reentrancy/trace layout | Global scratch, callbacks, actor/sector side effects | Последовательные native movement двух players; no network-thread calls |
| Q06 | UNKNOWN: полноценный proxy | Plain Actor не обязательно проходит Puppet checks; owner в Tick/Damage | Collision, pickups, AI targeting, оружие/damage/attribution без local singleton bleed |
| Q07 | UNKNOWN: client presentation без gameplay | ChangeAnim немедленно выполняет actions | Presentation replay не меняет health/ammo/spawn/RNG; иначе отдельная визуальная ветка |
| Q08 | UNKNOWN: AI acquisition и все scripted singleton calls | AI chase fallback; p_enemy/p_misc/TOMB examples | Узкая карта зависимостей по конкретным failures M1/M3; не менять все scripts заранее |
| Q09 | UNKNOWN: safe lifetime/ID reuse | Nodes/refcounts/pool return | Repeated spawn/destroy, pending targets/events, generation+mapEpoch rejection |
| Q10 | UNKNOWN: complete world baseline | Native save subset/async load; map/script/mover state | Join на barrier до проверки mid-level snapshot; не приравнивать save к baseline |
| Q11 | UNKNOWN: hook ABI/unwind/patch safety | Complete asm/body ranges и PE pdata сохранены | Реальные call-site/register checks + disposable detour validation; сейчас no patch |
| Q12 | UNKNOWN: local Steam service address/type/thread | vtable+10→SetAchievement, readiness callback, pump | Наблюдать DAT_140aeaf80, this adjustment, current-account context, success/storage callbacks |
| Q13 | UNKNOWN: все условия20 achievements | Embedded definitions20 + характерные native branches | Implementation coverage matrix для всех keys; текущие типовые tests, не20 отдельных RE-проектов |
| Q14 | UNKNOWN: участник события/кампании | Требования пользователя, native только local conditions | Stable participant↔Steam identity; pickup/battle/run intervals, reconnect/late join tests |
| Q15 | UNKNOWN: shared negative/aggregate eligibility | Ankh helper count+84 before bit check; finish count==0 | Host dedup до native helper, monotonic team violation, five unique sources, persisted run history |
| Q16 | UNKNOWN: масштабирование8 | Нет runtime multi-player evidence | 8 independent contexts/sidecars; 1+7 clients; IDs/masks/queues/load tests после M4 |

Известные tool blockers: REA6.7.0/Ghidra12.2 несовместимы; прямой headless12.2 доступен. WMI/volume CIM Access denied обойдены read-only Registry/.NET. Они не блокируют M0 handoff и сейчас не требуют настройки/установок. Все future options предметов/Power-Ups/FF/respawn/scaling остаются за текущим исследовательским scope.
