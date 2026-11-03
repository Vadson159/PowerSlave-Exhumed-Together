# M0 → M1: native handoff

**M0 PASS, exact-build native foundation for2 independent players.** [Решение/критерии](27_M0_COMPLETION_REPORT.md), [acceptance manifest](evidence/m0/M0_ACCEPTANCE.json). Это документационный handoff; M1/M2/ENet/Thread Bridge не реализованы и не изменены.

EXE SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`. Перед новыми runs — новый профиль/launch, актуальный hash, current isolation PASS и реальный native gameplay tick. Не загружать старые bound sources с прежними PID; C01 constructor witness переиспользовать только для совпадающей сборки. Код в `experiments/m0/` — version-locked diagnostic adapter, не KEX source/готовая DLL.

## Native seams и lifetime

- Player allocation408 bytes/alignment16/guards; constructor defaults и ready interpolant bindings доказаны C01. Native Reset RVA1dfa70, factory Spawn1d4780 (Win64 RCX factory,EDX type,R8 position,XMM3 yaw,fifth sector on stack), weapon-ready20f9e0. Не вызывать hardcoded singleton initializer повторно и не memcpy Player/Puppet.
- Scoped GetPlayer1b5520 выбирает P2 только на observed simulation TID во время explicit Spawn; exact5-byte leaf возвращается после scope. Controller1dfe30/commands1e34a0/turning1e2ad0 остаются на simulation thread, последовательно P1/P2. Inputs/held counters независимы; production adapter должен сохранять local input и не повторять fixture scripts.
- Native Remove только для собственного подтверждённого Actor с проверенным virtual target. Remove не означает немедленное free. Пока native holders живы, old generation pending допустимо. Не писать refs/target/SourceOwner/deletion flags и не запускать global target-clear passes.
- Respawn: остановить старое scheduling, native Remove old/Reset Player/acquire pistol/Spawn native level start/weapon-ready, новый generation ID. Old/new Actor+node различны, общий Player сохраняется; **старые снаряды держат старое поколение**, включая attribution. [C19](evidence/m0/C19_MANUAL_RESPAWN/RESULT.md), [C23](evidence/m0/C23_PROJECTILE_THROUGH_RESPAWN/RESULT.md). Не переименовывать старый shooter новым Puppet.
- P1 death: минимальный native restart gate1dffd6 сохраняет camera/update continuation; whole-P1 update skip и spectator не нужны. [C17](evidence/m0/C17_HOST_DEATH/RESULT.md). Long-duration combat/host respawn ещё не приняты.
- При StateExit1b6670 **до** native actor arenas: stop scheduling, native Reset внешнего Player один раз. Дождаться owned native destructor returns и original StateExit return, handle0/guards/exact restored hooks; только затем допустимо free/unload context/code. [C20](evidence/m0/C20_CONTEXT_WORLD_EXIT/RESULT.md), [C22](evidence/m0/C22_DIRECTED_PHYSICS_FINITE/RESULT.md), [C23](evidence/m0/C23_PROJECTILE_THROUGH_RESPAWN/RESULT.md).
- **Direct Quit:** live-P2 unbind на fcc20 до engine service cleanup, exact control hooks restore, все callback code/storage/return trampolines pinned до process exit. Этот native путь идётfc580→_exit→ExitProcess без StateExit/actor arenas/owned Puppet destructors в принятом run. Не вызывать их насильно и не выгружать script на entry shutdown. [C25](evidence/m0/C25_NATIVE_DIRECT_QUIT_BOUNDED/RESULT.md), [static](evidence/m0_static_final_acceptance/RESULT.md).

## Следующие gates M1, не выполненные M0

1. Новый production input/identity adapter с теми же native lifetime правилами; сохранение local input и typed generation attribution. Не копировать C23 queued-E fixture в игровой автоматический respawn.
2. Capacity3–8, реальные LAN/VPN/network sessions и их failures/joins/leaves. Native M0 подтверждает2 players, не8 и не репликацию. ENet/Thread Bridge не подключены этой работой.
3. Native gameplay/presentation integration: physical E, P1 respawn, HUD/camera UX, spawn placement, фактическое AI combat targeting после смерти хоста, более длинные death/respawn loops и множество projectile holders.
4. Кампания/общий сюжетный progress, все transitions/save-load/arsenal. C20 initial test-save load UNKNOWN; C23 новый Puppet штатно получил damage139 и затем source-null79, поэтому120-alive-tick auxiliary marker incomplete сохранён. Это не проверка всей игровой семантики.

Старые C24 overflow/C21 NaN и предыдущие ошибки сохранены; не использовать их как PASS. Для accepted gates читать exact RESULT/SUMMARY/raw, а не только код/exit0. Новая версия бинарника, native ABI или другая схема lifetime требует собственного gate; произвольный mid-level hot unload не поддерживается.
