# Сущности, AI и авторитет мира

Все наблюдения 2022 привязаны к [манифесту](01_BINARY_MANIFEST.md). Исторические исходники помогают выбирать проверки, но не заменяют Evidence нативной сборки.

VERIFIED: фабрика создаёт actor по map record (`0x1401d41f0`), integer type (`0x1401d4780`) и дополнительным путям, использует custom RTTI/class pools, LinkSector и virtual Spawn. Основные связи: Actor+0x10 → node; node+0x28 → payload; sector lists находятся по sector+0x78, stride сектора в исследованных проходах `0x88`. [Factory](evidence/ghidra_2022/G_FUNC_1401d4780.json), [LinkSector](evidence/ghidra_2022/G_FUNC_1401d3d70.json).

VERIFIED: update passes вызывают node callback `0x1401d3230`, который проверяет refcount/временную отметку и dispatch object virtual +0x20. Remove (`0x1401a02b0`), deferred sweep (`0x1401b6360`), factory pool return (`0x1401d5000`) и deleting destructor не эквивалентны. Нельзя использовать адрес C++ объекта как сетевой ID: память повторно используется. [Update](evidence/ghidra_2022/G_FUNC_1401d3230.json), [lifetime](evidence/ghidra_2022/G_FUNC_1401b6360.json).

AI:

| Кандидат | Подтверждённая статика | Ограничения / проверка |
|---|---|---|
| `0x140194ce0` | AI vtable+0x20; state по AI+0x1b8, animation/state transitions, dead-target clear, idle path 0x140195580, chase 0x1401971e0, затем base actor update | Имена state 0/1/2/3/5/6 — интерпретация; не переносить в другой класс: Puppet+0x1b8 имеет другой смысл |
| `0x1401971e0` | Строка `kexSlaveAI::ChaseTarget`; node target по +0x28, health/LOS/таймеры, fallback через global Player actor | Начальный поиск цели полностью не исследован. Для восьми игроков нужен выбор среди живых целей, а не только исправление fallback |
| `0x140195050` | AI damage callback; death state=5; boss achievement keys; instigator/source-owner branches меняют target node | Нужно проверить hit attribution и смерть один раз; friendly-fire/scaling — будущая policy |
| `0x1401953d0` | AI Spawn вызывает базовый Spawn, обнуляет state и использует difficulty service | Параметры масштабирования не проектируются сейчас |

Evidence: [AI tick](evidence/ghidra_2022/G_FUNC_140194ce0.json), [chase](evidence/ghidra_2022/G_FUNC_1401971e0.json), [damage](evidence/ghidra_2022/G_FUNC_140195050.json), [spawn](evidence/ghidra_2022/G_FUNC_1401953d0.json), [AI vtable](evidence/ghidra_2022/G_MEMORY_140527610.json).

VERIFIED: 2022 KPF `scripts/p_enemy.txt` содержит target-aware действия и отдельные singleton обращения. В частности, строки 824–827 получают `Player().GetActor()` для нанесения damage. `p_misc.txt:316` назначает projectile SourceOwner глобальному player. Поэтому замена только native target acquisition не охватит скриптовую логику. [E010](evidence/E010_KPF_SELECTED_TEXT.json).

События мира: `0x1401bea70(actor,sector)` проверяет sector events, вызывает скрипты/фабрику и interaction branches. `0x1401bccb0` обрабатывает face/use event по результатам trace и несколько раз запрашивает singleton Player для ключей/звука/активации. Это реальные кандидаты для host event authority, их C++ API пока не валидирован. [Sector event](evidence/ghidra_2022/G_FUNC_1401bea70.json), [face/use](evidence/ghidra_2022/G_FUNC_1401bccb0.json). Двери/лифты имеют типы в RTTI и defs; полный набор их состояний не восстановлен и для первого M0 не требуется.

INFERRED архитектурное требование: хост единолично выполняет AI, RNG, damage/death, pickups, world triggers, movers, boss/script progression и level completion. Клиенты получают результаты; повторное проигрывание anim actions не должно повторять damage/spawn. Сектора/акторы/узлы требуют отдельной связи с network ID `{session, mapEpoch, entityId, generation}`. Map actor index полезен как стабильный якорь исходной карты, но не заменяет ID динамического объекта.

UNKNOWN: возможность подавить client gameplay effects с сохранением штатной визуализации; корректный proxy actor для игроков; collision/AI recognition нескольких players; полное состояние movers/scripts для mid-level join. Эти вопросы закрываются экспериментами из [09](09_POC_PLAN.md), а не продолжением широкого реверса.
