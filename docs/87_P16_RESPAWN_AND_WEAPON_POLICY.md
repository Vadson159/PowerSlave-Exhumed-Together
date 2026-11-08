# DOC-87 — П16: автомат респауна и постоянный оружейный пикап

2026-11-06. **PARTIAL, режим Б. П16 / S4.5 не приняты, PASS не достигнут.**
Исполнитель и проверяющий — текущий Codex по прямому указанию пользователя.
Отдельных агентов не было. EXE SHA-256:
`b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`.

## Требование и результат

Опора — [DOC-16](16_RESPAWN_DESIGN.md), [M0 handoff](M0_TO_M1_HANDOFF.md),
принятые C19/C23 и [П7](80_P7_SELF_STATE_NATIVE_GATES.md).
[Решение пользователя](evidence/m1/P16_RESPAWN/DECISIONS.md): сброс оружия до
стартового набора; оружейный пикап **постоянно остаётся на карте** и выдаёт
**только оружие, без патронов**. После смерти его можно подобрать снова.

| Критерий S4.5 | Что имеется | Вердикт |
|---|---|---|
| E → native возрождение | Value-only DeadInput/Team, debounce, host validation, idempotent request/confirm через настоящий ENet; native completion в тесте — fixture | Не выполнен в игре |
| Гибель всей команды без reload | Counts/TPK и независимое восстановление 2–8 слотов в policy; нет native P1 restart gate и общего игрового опыта | Не выполнен в игре |
| Оружие после респауна / BLK-11 | Starter policy, per-life entitlement, отдельный native weapon seam и host DLL-кандидат; нет native rearm/повторного подбора в KEX | Не выполнен в игре |

Это незавершённая реализация пакета, а не отказ REA, нехватка разрешения или
завершённый кандидат, которому достаточно формальной смены статуса.

## Реализация

[`respawn_p16.hpp`](../experiments/pse_together/src/session/respawn_p16.hpp)
не содержит игровых указателей. Team валидирует connection/epoch/death,
резервирует новый generation и ticket, требует native receipt для commit,
повторяет сохранённый confirm без второго native вызова. Частичная ошибка
останавливает слот в Faulted без автоматического retry. Disconnect резервирует
также прерванное поколение, чтобы поздний v1.12 broadcast confirm не совпал с
новым occupant. DeadInput требует отпускания E перед новой смертью/повтором;
timeout сам не порождает запрос, старый DEAD после confirm игнорируется.
Counts различает Alive/Dead/Respawning/Faulted. Учёт оружия ограничен256
logical pickups, без eviction и без ручного восстановления MAP Actor.

[`weapon_pickup_p16.cpp`](../experiments/pse_together/src/native/weapon_pickup_p16.cpp)
— отдельный native seam поверх принятых вызовов. Он проверяет текущую roster
Player/Puppet/node, native definition weaponSlotToGive, здоровье и owned[];
затем вызывает GiveWeapon конкретного Player. Все восемь ammo pools должны
остаться неизменными. Original OnTouch/consume не вызываются для принятого
кооп-касания, поэтому пикап не удаляется. Уже имеющееся оружие не выдаётся
повторно. Inactive/unowned/stop возвращаются к original; refs/target/flags и
указатели не переписываются. Новый Player не создаётся этим seam.

**VERIFIED static, не RUNTIME OBSERVED:** сохранённые
[OnTouch](evidence/ghidra_2022/G_FUNC_1401d8470.json) и
[GiveWeapon](evidence/m1/PSE_TOGETHER_P7/static_native_selected/G_FUNC_1401e06d0.json).
Preferred VA/RVA/file offset: OnTouch `0x1401d8470 / 0x1d8470 / 0x1d7870`,
GiveWeapon `0x1401e06d0 / 0x1e06d0 / 0x1dfad0`. Win64-контракт:
`OnTouch(pickup RCX, Puppet RDX)`,
`GiveWeapon(Player RCX, slot EDX, select R8B) → AL`. Native GiveWeapon меняет
owned/pending, не ammo; original OnTouch затем вызывает consume1d8360.
Псевдокод не является исходным кодом; данные динамического receiver и
сохранность пикапа новым обработчиком пока не подтверждены в игре.

`pse_dll_host_p16` использует отдельный `host_p16.cpp`, полученный из П7 с
подключением weapon seam после construct и exact16 stop перед unbind.
Принятые host_p7/client_p7 и старые targets не изменены. Респаун по-прежнему
отсутствует в этой DLL: session adapter отвергает RespawnRequest, П7-клиент
закрывает lifetime при изменении native incarnation. Новая DLL не заменяет
принятые игровые артефакты.

## Проверка

[OFFLINE04](evidence/m1/P16_RESPAWN/OFFLINE04/RESULT.json): **38/38 CTest**, новая
policy474/0, два реальных ENet workers68/0, weapon engine doubles16/0.
Покрыты replay/spam/timeout, stale epoch/connection/death, disconnect во время
plan, поколения при slot reuse, Faulted/capacity, TPK2–8 и повторное получение
оружия в synthetic storage. Real-loopback передаёт v1.12 request/confirm,
дубликаты20 раз и вторую жизнь; **нативное завершение подменено fixture**.

v1.12 RespawnConfirm не сериализует health; его DTO возвращается с transport
default. Из этого нельзя получать native HP. Наблюдавшиеся200/60 после native
Reset/Acquire взяты из C19/C23, а не из исторического эскиза100/50 в DOC-16.
Difficulty/другие карты новым кодом не проверялись.

[AUDIT01](evidence/m1/P16_RESPAWN/AUDIT01/RESULT.json): **18/18 file/evidence checks**.
Original77 и обе независимые копии побайтово равны исходной базе; реальные
5protected файлов неизменны. Обе принятые П7 DLL после сборки в b87 совпали с
frozen RUN03 по7/7 masked PE sections/exports. v1.10 header и retained
v1.11–v1.13 sources не изменены. Повторно прочитаны П7 RUN03 control exact16,
14native client restores и exit0. **Это retained restores П7**, не restores
П16. Новых игровых процессов, Frida-сессий и hook installations0; остаточных
игровых/P16 test процессов нет.

[Воспроизведение и сохранённые ошибки](evidence/m1/P16_RESPAWN/REPRODUCTION.md).
Сборка и машинный PASS этого аудита не означают PASS пакета.

## Что осталось выполнить до PASS

1. Соединить E/Team с Session и реализовать native Remove/Reset/Acquire/Spawn/
   weapon-ready для новой собственной инкарнации, сохранив старую атрибуцию
   снарядов и общий сюжетный прогресс. Запрещённую очистку refs из старого
   DOC-16 не использовать.
2. Реализовать P1 restart gate и native TPK без загрузки уровня; клиентскую
   новую жизнь/admission/snapshot generation с новыми read-only key gates.
3. Проверить в KEX постоянный weapon Actor, отсутствие ammo grant, повторные
   касания обоих игроков и тот же пикап после настоящего респауна.
4. Выполнить новые isolation/native runs, полный raw audit, exact restores
   нового weapon hook и control/restart gates, штатное завершение и integrity.

Эти пункты **не реализованы и не приняты** текущими offline-проверками.
