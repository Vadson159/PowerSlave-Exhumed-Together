# Столкновения, оружие и урон

Основной EXE / SHA: [01](01_BINARY_MANIFEST.md). Статические сведения ниже дополнены [C01/C2–C3 runtime](evidence/m0/C01/RESULT.md):420 collision calls каждому, раздельные movement/yaw/health, native melee actions с собственным Puppet. Native weapon-ready RVA20f9e0(P2+e0,0) обязателен для начальной animation. Weapon hit, projectile/ammo и полный collision correctness UNKNOWN; одни call counts не доказывают весь gameplay. Ghidra-прототипы не являются готовым C++ ABI.

VERIFIED: Puppet virtual update `0x1401db8a0` обращается к owner Player+timers/flags, вызывает base actor update `0x14019f2d0`, затем ветвится к `0x1401dc6e0`, `0x1401dd670`, `0x1401ddf70`, `0x1401de8b0`. Их роль как ground/water/особых режимов — INFERRED; точное соответствие флагам не требуется предполагать для первого прототипа. Несколько веток вызывают общий collision candidate `0x1401a4990`, relink sector и sector-event обработчик. [Puppet update](evidence/ghidra_2022/G_FUNC_1401db8a0.json), [movement](evidence/ghidra_2022/G_FUNC_1401dc6e0.json), [collision](evidence/ghidra_2022/G_FUNC_1401a4990.json).

`0x1401a4990` принимает указатели на trace/scratch, actor и vector по декомпиляции, проходит geometry/actor lists, вызывает collision/touch callbacks и использует глобальные scratch arrays (`DAT_140b2f...`). Это аргумент в пользу последовательного исполнения движения нескольких игроков на simulation thread; реентерабельность и вызов из сетевого потока **UNKNOWN**. Возвращаемое значение/полный trace layout не восстановлены. Прямой вызов по угадываемой структуре недопустим. Результат этой функции сам может иметь игровые side effects.

VERIFIED поля: origin Actor+0x18, rotation+0x30, sector+0x3c, velocity+0xe0, health `int16` +0x17a. Getter asm и ссылки — в [03](03_PLAYER_LIFECYCLE.md). Простая запись позиции без LinkSector `0x1401d3d70` не обновляет spatial membership. Для remote correction нужны согласованные origin/sector/collision и исключение повторных triggers; M0 сначала проверяет native movement существующим путём.

Урон:

| Адрес | Наблюдение | Проверка прототипа / риск |
|---|---|---|
| `0x1401a1370` | Зарегистрирован как `InflictDamage(actor,damageType,int)`; проверяет условия, уменьшает health, вызывает victim virtual +0x48, death/animation paths | Decompiler показывает `short` для amount, script declaration — `int`; установить расширение/усечение по call sites и runtime. Предполагаемые slots: RCX victim, RDX instigator, R8D type, R9D amount |
| `0x1401dbd00` | Puppet damage handler: pain/death effects, owner timers/holster | Использует Puppet owner+0x1b8; proxy без корректного owner может повредить локальное состояние |
| `0x140195050` | AI damage/death handler, boss rewards и изменение target | Удержать instigator/sourceOwner, чтобы kill-method/achievement conditions были достоверны |

Evidence: [damage](evidence/ghidra_2022/G_FUNC_1401a1370.json), [puppet response](evidence/ghidra_2022/G_FUNC_1401dbd00.json), [AI response](evidence/ghidra_2022/G_FUNC_140195050.json). Host-authoritative означает, что клиент присылает input/намерение, а не окончательный health delta.

Оружие и actions: `0x14020f760` соответствует ChangeAnim по диагностической строке `kexSlaveWeapon::ChangeAnim`; меняет текущую анимацию и **в этом же вызове** проходит frame actions, вызывая их с actor из Player handle. Значит применение animation state к remote entity может выстрелить/нанести урон ещё раз. [ChangeAnim](evidence/ghidra_2022/G_FUNC_14020f760.json). Нужен отдельный presentation path или доказанное подавление gameplay actions; простое «проиграть ту же анимацию» небезопасно для согласованности симуляции.

`0x140199710` — melee/action path: trace `0x1401a78c0`, damage `0x1401a1370`, проверка actor-def type=5 и health<=0, затем `ACH_KILL_MANTIS_MACHETE`. Это конкретный источник события kill-method, полезнее общего перехвата Steam SetAchievement. Тип 5 как mantis corroborated ключом достижения, но полную weapon/actor enum карту не копировать из 2015. [Melee](evidence/ghidra_2022/G_FUNC_140199710.json).

Поставка содержит `defs/weaponInfo.txt` с 8 weapon definitions, ammo limits, анимациями; `defs/actors/projectiles.txt` и `scripts/p_pspr.txt` описывают projectile/beam chains, target/sourceOwner и RNG. [E010](evidence/E010_KPF_SELECTED_TEXT.json). Эти данные позволяют не изобретать weapons enum. Полный native weapon tick, ammo consumption, projectile allocator и hit-scan action handler не доказаны как готовые reusable API; для M0 достаточно одной штатной атаки с корректной атрибуцией, затем расширение по фактическим пробелам.

Критические испытания после перехода к прототипам: независимые input/velocity и ammo двух игроков; движение через дверь/лифт/воду; один выстрел → один projectile/damage event; projectile owner сохранён после удаления стрелка; death callback один раз; remote presentation не запускает actions/RNG; исправление позиции не подбирает предмет повторно. FF/предметы/Power-Ups/scaling — будущие policy-поля, сейчас без детального дизайна.
