# Минимальные требования к сети

Актуальное задание: **2–8 игроков включая хоста**, host-authoritative listen server, LAN или Radmin VPN; **ENet предпочтителен**. Не нужны STUN/TURN, NAT traversal, публичные серверы, matchmaking, host migration. Сейчас сеть не реализуется; полный wire protocol не проектируется. Все предложения ниже — INFERRED, работоспособность native integration UNKNOWN.

Официальный [ENet repository](https://github.com/lsalzman/enet) — источник для последующей интеграции, версия/сборка библиотеки сейчас не выбраны и не скачивались. Наличие WS2_32/Steam API в игре не означает готового coop transport или world replication.

| Область | Хост | Клиент |
|---|---|---|
| Player | Отдельные команды каждого участника, movement/damage/inventory | Локальный ввод; authoritative state; prediction после изоляции movement |
| Мир | Единственная симуляция AI, projectiles, pickups, damage/death, movers, triggers/scripts/RNG | Реплики/интерполяция; gameplay actions от анимаций исключены доказанным способом |
| Кампания | Общий сюжетный progress, карта, сложность, save generation | Level barrier; индивидуальный player state отдельно |
| Достижения | Условия, participant set, dedup/eligibility | Только собственные earned awards через штатный локальный Steam service |

Network identity не содержит C++/node pointers. Нужны session/run ID, mapEpoch, entity ID с generation, participant ID и подтверждённая связь с локальным Steam user. Восемь player slots (7 удалённых peers у хоста), отдельный owner у actor/projectile, индивидуальные command/health/ammo/weapon/death states. Общие campaign/achievement state не прятать в неявный «player 0».

Первому M2 достаточно input, player snapshots и надёжных lifecycle/level events. Sequence/tick и baseline revision исключают старые события для нового объекта/карты. Потерянный snapshot заменяется новым; spawn/destroy/transition/earned-achievement требуют bounded reliable delivery и dedup. Каналы ENet, частоты/MTU/quantization проверяются при реализации, сейчас произвольные значения не назначаются.

Simulation thread последовательно применяет команды. Сетевой callback не вызывает native collision/gameplay: global scratch и singleton lookups не доказаны реентерабельными. Клиентская презентация не создаёт второй authoritative мир. [Риски collision/actions](05_PHYSICS_WEAPONS_DAMAGE.md).

Первое подключение — admission на level barrier. Mid-level join позже, после доказательства полноты world baseline. Переподключение узнаёт прежнего participant, создаёт новые connection/entity generations и восстанавливает допустимое индивидуальное состояние; disconnect не переносит remote ownership на local Player.

Shared Achievement Progress: хост хранит event ID, event participant set и отрицательные/накопительные условия. Клиент хранит processed award IDs, очередь до Steam readiness и подтверждение результата. Транспортный ACK не означает Steam commit. Переподключение может завершить ранее заработанную награду; новый участник не получает историю наград. [Политика и Steam RVA](10_OPUS_HANDOFF.md).

Расширяемость: versioned Ruleset с будущими policy keys для shared/personal items и Power-Ups, FF, respawn, scaling. Подробные механики сейчас не проектируются. Team achievement conditions независимы от выбора gameplay inventory: любой team ankh нарушает team no-ankhs.

Сравнение Full Player/Proxy и DLL/deep changes — в [handoff](10_OPUS_HANDOFF.md). Ранний [сетевой эскиз](research/NETWORK_AND_MILESTONES_DESIGN.md) сохраняется как история; его 2–4-player/transport/протокольные предложения **заменены текущим заданием и этим документом**, не являются требованиями реализации.
