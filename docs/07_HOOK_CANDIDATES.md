# Критические точки интеграции

Статический каталог ниже — **не production-ready hooks**. VERIFIED относится к статике; runtime дополнения M0 отдельно помечены RUNTIME OBSERVED. В test copy выполнены Frida/native CModule code hooks; оригинальный EXE на диске не менялся. Безопасность общего production patch/unload и пригодность для всей кампании UNKNOWN. Исторические риски/динамические планы ниже не отменяют частичные результаты следующего раздела.

## Runtime дополнение M0

Тот же SHA ниже; actual base0x140000000. Все вызовы gameplay выполнялись на observed simulation TID38512 (C01), а не worker thread. Factory ABI corroborated A03/B, graph/commands/health/actions — [C01](evidence/m0/C01/RESULT.md). Полный M0 не принят.

| Кандидат / preferred VA / RVA / file offset | ABI, Evidence и наблюдение | Риск / следующая динамическая проверка |
|---|---|---|
| Native global init `0x1401ae110` / `0x1ae110` / `0x1ad510` | [G_FUNC](evidence/m0_static/G_FUNC_1401ae110.json), [runtime witness](evidence/m0/C01/status_01_constructor.json). Hardcoded singleton stores;408-byte state совпал с независимой моделью | Не reusable Player constructor; повторный вызов повреждает singleton. Проверять owned adapter/ready bindings, не вызывать этот global init повторно |
| GI ready window `0x1401b01d0` / `0x1b01d0` / `0x1af5d0` | [window](evidence/m0_static/G_INSPECT_1401b0c20.json), [Reset call](evidence/m0_static/G_INSPECT_1401b0c7f.json), [runtime fields](evidence/m0/C01/ready_interpolants.json). GI this; binds two embedded interpolants before Reset | Не вызывать весь GI Init для P2. Presentation registry singleton; проверить отдельный camera/interpolant lifetime |
| Player Reset `0x1401dfa70` / `0x1dfa70` / `0x1dee70` | RCX=Player, void-like; [static](evidence/ghidra_2022/G_FUNC_1401dfa70.json), C01/C1. Native handle release, state/inventory reset | Нужны initialized interpolant refs, loaded definitions. Проверить death/respawn/map teardown, все retained references |
| Inventory reset `0x1401dfb40` / `0x1dfb40` / `0x1def40` | RCX=Player; [static](evidence/m0_static/G_FUNC_1401dfb40.json). Native Reset tailcall; global read-only weapon defs | Не является полным constructor. Подтвердить ammo/weapon inventory isolation при gun/projectile тесте |
| Interpolant reset `0x1400cafc0` / `0xcafc0` / `0xca3c0` | RCX=interpolant member; [static](evidence/m0_static/G_FUNC_1400cafc0.json), C01 failure/ready fix. Reads member+8 reference | Null ref→m_pRefValue assert; owned refs должны оставаться живыми. Не регистрировать/освобождать в global render list вслепую |
| GetPlayer H03 seam `0x1401b5520` / `0x1b5520` / `0x1b4920` | RCX=GI→RAX=GI+40; C01 scoped native resolver selected P2 while native Spawn wrote refs | Только5 bytes, failed JS/replaceFast variants; рабочий bounded redirect требует prefix/near allocation/safe point/restore. General concurrency/unload и другие singleton paths проверить отдельно |
| Factory/Spawn H02/H03 `0x1401d4780` / `0x1d4780` / `0x1d3b80`; `0x1401dbc60` / `0x1dbc60` / `0x1db060` | [A03/B](evidence/m0/A03/frida_status_04_B.json): RCXfactory, EDXtype, R8vec, XMM3yaw, stack5 sector→RAXactor; Spawn RCXPuppet. Native refcount/owner graph corroborated | Stock Spawn steals singleton; после scoped fix три graph/retire cycles. Проверить spawn failure/transition/repeated churn, не чинить pointer fields вручную |
| State/Player/Puppet H01/H05/H04 `0x1401b6ec0` / `0x1b6ec0` / `0x1b62c0`; `0x1401dfe30` / `0x1dfe30` / `0x1df230`; `0x1401db8a0` / `0x1db8a0` / `0x1daca0` | RCX=GI/Player/Puppet соответственно; [C3](evidence/m0/C01/status_11_weapon_result.json):420 controller calls каждому, native Puppet traversal; один observed thread | Gate state3 недостаточен: C4 tick stopped while state remained3. Проверить pause/focus/time scheduling, shared services и отсутствие двойного tick |
| Input H06/H07 `0x1401e34a0` / `0x1e34a0` / `0x1e28a0`; `0x1401e2ad0` / `0x1e2ad0` / `0x1e1ed0` | RCX=Player. Native adapters skip hardware builders for controlled contexts and write separate scalar commands |13-button loop ещё вызывается дважды; проверить настоящее local hardware, queue consumption, mouse/history и удалённые команды |
| Collision H08 `0x1401a4990` / `0x1a4990` / `0x1a3d90` | C3: RCXscratch/RDXPuppet/R8vec observed для обоих,420 calls each, floor/sector crossings | Call count не collision correctness; wall/portal/water/body contact и scratch reentrancy UNKNOWN. Только serial simulation thread |
| Damage H09 `0x1401a1370` / `0x1a1370` / `0x1a0770` | RCXvictim/RDXsource/R8Dtype/R9Damount; C2/C3 ADT_GENERIC0 amounts13/7 изменили только нужный health | Actual weapon damage/source attribution, death/respawn, projectile narrowing/flags проверить; direct helper не доказывает hit |
| Weapon ready `0x14020f9e0` / `0x20f9e0` / `0x20ede0` | RCX=P2+e0, EDX=mode0; [static](evidence/m0_static/G_FUNC_14020f9e0.json), C3 non-null ready animation | Native ChangeAnim executes actions; не presentation-only. Проверить ammo/projectile/weapon switch, ready при map reset |
| Weapon/melee H10/A04 `0x14020f760` / `0x20f760` / `0x20eb60`; `0x140199710` / `0x199710` / `0x198b10` | C3: RCXweapon context with owner at+0; melee RCXaction/RDXcorresponding Puppet; separate P1/P2 action execution | Shared action definitions не shared Player. Фактический target hit не проверен; исключить global view/target leakage, удвоение actions |
| Remove/retire H11/H12 `0x1401a02b0` / `0x1a02b0` / `0x19f6b0`; `0x1401b6360` / `0x1b6360` / `0x1b5760` | Remove RCXactor, Reset RCXPlayer, deferred native sweep; C1–C3 node payload=null/refs0, pool reuse | Buffers pinned до exit0. General deallocation/queued targets/map teardown не проверены; Remove не free |

Exact bound sources/raw errors находятся в C01; [M0_EXPERIMENTS](M0_EXPERIMENTS.md) задаёт правильный load/unload порядок. Production ABI/thread/lifetime для save/load/world events остаются исходными UNKNOWN; эти пути M0 не затрагивал.

SHA-256 EXE: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`; preferred image base `0x140000000`. В таблице VA/RVA/file offset — разные величины. Runtime VA вычислять от фактической module base; DYNAMIC_BASE=false не разрешает опираться на абсолютную загрузку. Полные body ranges/assembly/псевдокод/связи — в dossiers; [машинный индекс](evidence/HOOK_CANDIDATES.json), [все сохранённые функции](research/FUNCTION_CATALOG.md).

| ID / назначение | Preferred VA | RVA | File offset | Evidence |
|---|---|---|---|---|
| H01 Simulation/state tick | `0x1401b6ec0` | `0x1b6ec0` | `0x1b62c0` | [dossier](evidence/ghidra_2022/G_FUNC_1401b6ec0.json) |
| H02 Actor spawn by type | `0x1401d4780` | `0x1d4780` | `0x1d3b80` | [dossier](evidence/ghidra_2022/G_FUNC_1401d4780.json) |
| H03 Puppet Spawn singleton binding | `0x1401dbc60` | `0x1dbc60` | `0x1db060` | [dossier](evidence/ghidra_2022/G_FUNC_1401dbc60.json) |
| H04 Puppet update | `0x1401db8a0` | `0x1db8a0` | `0x1daca0` | [dossier](evidence/ghidra_2022/G_FUNC_1401db8a0.json) |
| H05 Player/controller update | `0x1401dfe30` | `0x1dfe30` | `0x1df230` | [dossier](evidence/ghidra_2022/G_FUNC_1401dfe30.json) |
| H06 Input command candidate | `0x1401e34a0` | `0x1e34a0` | `0x1e28a0` | [dossier](evidence/ghidra_2022/G_FUNC_1401e34a0.json) |
| H07 Turning/input candidate | `0x1401e2ad0` | `0x1e2ad0` | `0x1e1ed0` | [dossier](evidence/ghidra_2022/G_FUNC_1401e2ad0.json) |
| H08 Actor collision/move trace | `0x1401a4990` | `0x1a4990` | `0x1a3d90` | [dossier](evidence/ghidra_2022/G_FUNC_1401a4990.json) |
| H09 InflictDamage | `0x1401a1370` | `0x1a1370` | `0x1a0770` | [dossier](evidence/ghidra_2022/G_FUNC_1401a1370.json) |
| H10 Weapon ChangeAnim | `0x14020f760` | `0x20f760` | `0x20eb60` | [dossier](evidence/ghidra_2022/G_FUNC_14020f760.json) |
| H11 Actor Remove | `0x1401a02b0` | `0x1a02b0` | `0x19f6b0` | [dossier](evidence/ghidra_2022/G_FUNC_1401a02b0.json) |
| H12 Deferred node retirement | `0x1401b6360` | `0x1b6360` | `0x1b5760` | [dossier](evidence/ghidra_2022/G_FUNC_1401b6360.json) |
| H13 Actor/sector events | `0x1401bea70` | `0x1bea70` | `0x1bde70` | [dossier](evidence/ghidra_2022/G_FUNC_1401bea70.json) |
| H14 Use/face events | `0x1401bccb0` | `0x1bccb0` | `0x1bc0b0` | [dossier](evidence/ghidra_2022/G_FUNC_1401bccb0.json) |
| H15 LoadMap | `0x1401c7b70` | `0x1c7b70` | `0x1c6f70` | [dossier](evidence/ghidra_2022/G_FUNC_1401c7b70.json) |
| H16 Save slot | `0x1401b3260` | `0x1b3260` | `0x1b2660` | [dossier](evidence/ghidra_2022/G_FUNC_1401b3260.json) |
| H17 Parse/apply loaded slot | `0x1401b3f20` | `0x1b3f20` | `0x1b3320` | [dossier](evidence/ghidra_2022/G_FUNC_1401b3f20.json) |
| A01 Artifact pickup condition | `0x1401d8ac0` | `0x1d8ac0` | `0x1d7ec0` | [dossier](evidence/ghidra_2022/G_FUNC_1401d8ac0.json) |
| A02 Ankh pickup condition | `0x1401d8870` | `0x1d8870` | `0x1d7c70` | [dossier](evidence/ghidra_2022/G_FUNC_1401d8870.json) |
| A03 AI death / boss reward | `0x140195050` | `0x195050` | `0x194450` | [dossier](evidence/ghidra_2022/G_FUNC_140195050.json) |
| A04 Melee / constrained kill reward | `0x140199710` | `0x199710` | `0x198b10` | [dossier](evidence/ghidra_2022/G_FUNC_140199710.json) |
| A05 Campaign finish conditions | `0x1401b5850` | `0x1b5850` | `0x1b4c50` | [dossier](evidence/ghidra_2022/G_FUNC_1401b5850.json) |
| A06 Native local Steam unlock | `0x140184110` | `0x184110` | `0x183510` | [dossier](evidence/ghidra_2022/G_FUNC_140184110.json) |
| A07 Steam stats readiness callback | `0x140184a90` | `0x184a90` | `0x183e90` | [dossier](evidence/ghidra_2022/G_FUNC_140184a90.json) |
| A08 Steam callback pump | `0x14018fbf0` | `0x18fbf0` | `0x18eff0` | [dossier](evidence/ghidra_2022/G_FUNC_14018fbf0.json) |

## Аргументы, связи, риски и необходимые проверки

**H01 — Simulation/state tick.**
Аргументы/ABI: RCX=this GameInstance; void-like recovered return.
Связи: State dispatcher 1b6480 -> guarded state case3 -> player/world/node passes.
Риск: Menus, transitions and conditional world path; tick rate/thread not measured.
Динамическая проверка: Log state, frame/tick stamp and thread through menu/pause/map change; verify exactly one authoritative simulation step.


**H02 — Actor spawn by type.**
Аргументы/ABI: RCX=factory context (unused in body), EDX=type, R8=vector pointer, XMM3=yaw, fifth stack argument=sector; RAX=actor candidate.
Связи: Creates/reuses class -> LinkSector -> virtual +38 Spawn.
Риск: Type0 hard-checks Puppet; allocation not independent Player.
Динамическая проверка: Spawn one additional type0 only in controlled future experiment; watch original controller handle, node refs and teardown.


**H03 — Puppet Spawn singleton binding.**
Аргументы/ABI: RCX=Puppet; return semantics UNKNOWN (decompiler ulonglong not contract).
Связи: Base Spawn -> global Player vcall+a8 -> Player+d8 handle replacement -> Puppet+1b8 owner.
Риск: Unconditional singleton reassignment; raw handle writes would violate refcounts.
Динамическая проверка: Capture before/after owner and both handles; prove separate context binding without altering local player state.


**H04 — Puppet update.**
Аргументы/ABI: RCX=Puppet; inferred void-like.
Связи: Base actor update -> owner fields -> movement/collision/sector events.
Риск: Owner required; mode flags and shared scratch; possible presentation side effects.
Динамическая проверка: Two command streams, positions/velocity, floor/door/water; count updates and verify no cross-player state changes.


**H05 — Player/controller update.**
Аргументы/ABI: RCX=Player context; inferred void-like.
Связи: State tick calls GameInstance+40; input/timers/weapon/use/death.
Риск: Uses local/global devices and presentation; not proven reusable for remote contexts.
Динамическая проверка: Update two contexts with synthetic separate commands; establish which local presentation/input work must run once.


**H06 — Input command candidate.**
Аргументы/ABI: RCX=Player candidate; exact command layout UNKNOWN.
Связи: Called from Player update; platform input service calls.
Риск: Hardware input may be read/consumed globally.
Динамическая проверка: Record command fields before/after; prove one local sample and independent supplied remote commands.


**H07 — Turning/input candidate.**
Аргументы/ABI: RCX=Player candidate; prototype UNKNOWN beyond recovered dossier.
Связи: Called by controller update/input path.
Риск: Smoothing/history/global console values may cross contexts.
Динамическая проверка: Opposite turn commands on two contexts; verify no shared history or input bleed.


**H08 — Actor collision/move trace.**
Аргументы/ABI: RCX=trace/scratch*, RDX=actor*, R8=vector* inferred; return/status UNKNOWN.
Связи: Called by player movement and base actor update, can call touch/events.
Риск: Global scratch arrays; impure, reentrancy UNKNOWN.
Динамическая проверка: Sequential per-player sweeps, two-body contact, sloped sector/portal; no network-thread calls.


**H09 — InflictDamage.**
Аргументы/ABI: RCX=victim, RDX=instigator, R8D=damage type, R9D=amount observed moves; native health int16.
Связи: Registered script callback -> health write -> virtual +48 response.
Риск: Int-to-health narrowing, conditions/death/actions and kill attribution.
Динамическая проверка: One attack gives one health change/death; projectile owner, environmental kill and remote victim all traced.


**H10 — Weapon ChangeAnim.**
Аргументы/ABI: RCX=weapon context*, RDX=animation selector inferred; return UNKNOWN contract.
Связи: Animation lookup/reset -> immediate frame0 action dispatch with Player actor.
Риск: Visual replay may shoot/spawn/damage/consume RNG.
Динамическая проверка: Replay animation on presentation-only replica; prove no gameplay actions; otherwise separate path required.


**H11 — Actor Remove.**
Аргументы/ABI: RCX=actor; recovered void-like.
Связи: Flags/links -> later node retirement/pool.
Риск: Remove is not immediate destructor/free.
Динамическая проверка: Remove while network event queued; no stale pointer or ID reuse; generation rejects late events.


**H12 — Deferred node retirement.**
Аргументы/ABI: No explicit args recovered; globals; return UNKNOWN contract.
Связи: Tick -> refcount check -> virtual +28 -> factory pool return.
Риск: Object lifetime depends on references retained by targets/players/scripts.
Динамическая проверка: Repeated spawn/remove, held target/projectile references and map teardown; no double free or stale refs.


**H13 — Actor/sector events.**
Аргументы/ABI: RCX=actor, RDX=sector inferred.
Связи: Movement -> floor/sector trigger/script/factory branches.
Риск: Must attribute actor; one-shot actions and map progress.
Динамическая проверка: Two players enter same sector in one tick; single authoritative event and correct instigator.


**H14 — Use/face events.**
Аргументы/ABI: RCX=face/event, RDX=trace/context inferred; exact structs UNKNOWN.
Связи: Player use trace -> keys/doors/scripts, repeated global Player lookup.
Риск: Additional players may use local keys/state.
Динамическая проверка: Remote use key/door/switch after owner context isolation; no local-player leakage.


**H15 — LoadMap.**
Аргументы/ABI: RCX=level, RDX=map selector inferred; return type UNKNOWN.
Связи: State enter -> level parse -> actors -> singleton puppet Spawn.
Риск: Old handles/scripts/world IDs invalidated; load not network-safe by itself.
Динамическая проверка: Transition barrier with two players then eight; old mapEpoch packets rejected; one valid controller per participant.


**H16 — Save slot.**
Аргументы/ABI: RCX=GameInstance, EDX=slot; bool-like caller result, not validated full signature.
Связи: Console save wrapper -> byte serialization -> platform user data.
Риск: Single-player subset and async persistence; not complete world baseline.
Динамическая проверка: Disposable future save copy roundtrip; original save untouched; coop generation consistency and no remote state overwrite.


**H17 — Parse/apply loaded slot.**
Аргументы/ABI: RCX=GameInstance, EDX=slot; bool-like recovered.
Связи: Readiness check -> parse/restore fields -> player init/state.
Риск: Slot request completion and native load side effects.
Динамическая проверка: Wait for completion; verify independent players recreated, campaign state correct, achievement eligibility not retroactive.


**A01 — Artifact pickup condition.**
Аргументы/ABI: RCX=pickup, RDX=touching Puppet inferred; void-like.
Связи: Uses Puppet+1b8 owner artifacts+90, then achievement service+10.
Риск: Native grant only local Steam user; collector progress differs from team event.
Динамическая проверка: Two accounts: remote collector, both eligible participants granted once; late join receives no past reward.


**A02 — Ankh pickup condition.**
Аргументы/ABI: RCX=pickup, RDX=Puppet inferred.
Связи: Global Player -> helper1e0910 -> count Player+84 >4 -> ACH_ALL_ANKAHS.
Риск: Singleton count; joint collection and no-ankhs condition need host ledger.
Динамическая проверка: Distribute five pickups among players; duplicate/reconnect does not increment twice; any team pickup invalidates team no-ankhs.


**A03 — AI death / boss reward.**
Аргументы/ABI: RCX=victim AI, RDX=instigator inferred; void-like.
Связи: Dead health/type -> boss achievement keys; living target/sourceOwner logic.
Риск: Last-hit source is not the full event participant set.
Динамическая проверка: Host sees one boss death, all event participants eligible independent of last hit; late join excluded.


**A04 — Melee / constrained kill reward.**
Аргументы/ABI: RCX=action, RDX=Puppet inferred.
Связи: Trace -> damage -> actor def type5 + health<=0 -> mantis/machete key.
Риск: General kill event alone would lose weapon restriction.
Динамическая проверка: Machete kill grants team; other weapon does not; simultaneous attacks preserve actual lethal cause.


**A05 — Campaign finish conditions.**
Аргументы/ABI: RCX=GameInstance; void-like.
Связи: Difficulty at+1d8, ankh count at+c4, emits hard/pharaoh/no-ankhs keys.
Риск: Current field alone cannot prove per-user campaign participation or historical team compliance.
Динамическая проверка: Campaign resume/rejoin and mid-campaign join; only qualified participants; any historic team ankh disqualifies no-ankhs.


**A06 — Native local Steam unlock.**
Аргументы/ABI: RCX=achievement service, RDX=key; third flag0 at callers, semantics UNKNOWN.
Связи: Service virtual+10 -> readiness/flags -> Steam UserStats virtual+38 then+50.
Риск: Returns may precede durable Steam confirmation; cheat/readiness gates must remain.
Динамическая проверка: Separate Steam accounts; initialized stats, success/failure/callback handling, offline retry and duplicate host award.


**A07 — Steam stats readiness callback.**
Аргументы/ABI: RCX=callback/service candidate, RDX=callback data; exact this adjustment UNKNOWN.
Связи: App/result gate -> cache/stat initialization.
Риск: No assumed direct callable SDK callback layout.
Динамическая проверка: Observe real callbacks and object address; delay awards until ready; do not fake callback memory.


**A08 — Steam callback pump.**
Аргументы/ABI: Recovered signature only; observe actual game thread.
Связи: IAT SteamAPI_RunCallbacks xref.
Риск: Native callback threading/lifetime must be preserved.
Динамическая проверка: Verify existing game pump processes each client local achievement confirmation without an extra competing pump.


## Ограничения перехвата и приоритет

Windows x64 default argument slots и shadow store описаны в [Microsoft x64 calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention). Это справка для интерпретации asm, не доказательство конкретного C++ прототипа. Сохранение nonvolatile registers, stack alignment, RIP-relative relocation, полных инструкций и [unwind/prolog](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64) остаётся работой будущего прототипа. Существующие `.pdata` записи сохранены в PDATA_main2022.json; наличие записи не доказывает безопасную длину detour.

Не созданы byte-pattern signatures, trampoline layout, stolen-byte length или patch sites. Не выбирать shared leaf getter, noop/guard thunk либо generic callback без проверки всех aliases. Короткий getter GetPlayer сам по себе не решает singleton Spawn и может быть непригоден как detour entry.

Первый эксперимент: H03/H02 + H04/H05, наблюдение reciprocal owner/node refs и независимых команд, без сети и без достижения/сохранения. H08/H09/H10 следуют после независимого жизненного цикла. Steam A01–A08 исследованы только на нескольких характерных путях; список всех 20 ключей извлечён из встроенного JSON, остальные условия не декомпилировались по отдельности. Для получения награды использовать штатный локальный service, сохраняя его gates; глобальное подавление checks или прямая выдача всех наград не соответствует Shared Achievement Progress.
