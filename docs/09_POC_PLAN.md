# План экспериментов и milestones

Пользователь разрешил runtime M0; результаты — [M0_EXPERIMENTS](M0_EXPERIMENTS.md). A03 выполнил M0-A baseline/M0-B singleton diagnostic; C01 частично подтвердил Full Player: construction, independent updates/movement/weapon actions/health и bounded Puppet retirement. Полный M0 НЕ принят: actual weapon hit и прочие collision/lifetime/death/map checks UNKNOWN. C4 не начал Spawn при переставшем наблюдаться tick. Сеть/ENet/достижения не начинать. Цель дальнейших этапов: 2–8 игроков, LAN/Radmin + ENet, исходная кампания.

## Первый пакет M0

1. **M0-A: runtime baseline.** В disposable копии/профиле записать module base, SHA, thread/timing state tick, GameInstance+0x40, Player node+0xd8, node payload+0x28, Puppet owner+0x1b8 и lifetime. Без achievements/save. Приёмка: соответствие статике, исходная установка не затронута.
2. **M0-B: singleton Spawn.** Наблюдать RVA `0x1dbc60`; дополнительный type0 проходит штатную фабрику. Зафиксировать original Player handle/refcounts. Второй видимый actor сам по себе M0 не проходит. Не исправлять это memcpy/неучтёнными pointer writes.
3. **M0-C: Full Player кандидат.** Минимальный корректный lifecycle независимого Player context, включая оружие/handle/команды. Второй Puppet получает отдельного owner, original context сохраняется. Противоположные input streams, отдельные health/ammo/weapon, один update каждого за tick, корректные destroy/recreate/level teardown. Переиспользование штатного Player init/update остаётся UNKNOWN.
4. **M0-D: ограниченный Proxy эксперимент, если C не проходит.** Remote actor/sidecar с коллизиями, damage и attribution без привязки локального Player. Проверить обход owner-dependent Puppet update с сохранением штатной физики. Визуальная копия без gameplay не проходит. Если нужен полноценный Player owner/controller, Proxy не считать упрощением.

Выбор C/D по объёму вмешательства, сохранению gameplay, независимости контекстов и масштабированию до восьми. При неудаче обоих фиксировать конкретный недостающий contract, не строить сеть вокруг фиктивного remote player.

## M0–M5

| Этап | Зависимости / результат | Приёмка |
|---|---|---|
| M0 | Независимый второй player | Movement/коллизии, одно оружие, damage/death, reciprocal refs, destroy/recreate; без сети и corruption local controller |
| M1 | M0; два локальных simulated participants | Раздельные input/health/ammo/weapon; doors/pickups/AI attribution; presentation не запускает actions/RNG; level teardown |
| M2 | M1; ENet host+client LAN/Radmin | Connect/disconnect/reconnect, IDs/generation/mapEpoch, bounded input/snapshots, loss/jitter; host authority; join на barrier |
| M3 | M2; world replication | AI/projectiles/damage/pickups/doors/scripts/movers/bosses согласованы; клиент не дублирует действия; однократные events |
| M4 | M3; совместная кампания / достижения | Переходы/save/resume, individual/global state; shared rewards на разных Steam accounts; negative/aggregate/method conditions и eligibility без retroactive awards |
| M5 | M4; до восьми включая хоста | 1+7 clients, нет hardcoded 2/4 masks, ограничены CPU/bandwidth/очереди; join churn, barriers, длительная кампания |

Steam A0 перед M4: сначала только логировать artifact/ankh/melee/boss/finish events и participant selection; затем проверить локальную Steam unlock цепочку, readiness/error/offline retry/dedup на отдельных тестовых аккаунтах. Не очищать достижения для автоматического теста. Уже полученная награда годится для idempotency, но не доказывает новый unlock.

Fail-criteria: второй Spawn заменяет local controller без корректного lifecycle; второй input зависит от первого; proxy разыменовывает null owner; animation replay повторяет gameplay; duplicate ankh повторно вызывает native count update; save или late join выдаёт прошлые достижения; старый пакет адресует recycled actor. При таком результате milestone не пройден.

Адреса/риски/динамические проверки — [07](07_HOOK_CANDIDATES.md); конкретная первая задача Opus — [10](10_OPUS_HANDOFF.md). Настройки items/Power-Ups/FF/respawn/scaling сейчас только будущие policy boundaries.
