"""Writes CODE_REVIEW.json for the Gemini DOC58D.1 candidate (static review against DOC58A/55/56 code and evidence)."""
from workflow import ROOT,OUT,GEMINI,LAB,sha,dump,stamp
H='tools/network-visual-proxy-readiness/doc58d1-contract-audit/doc58d1_hardened_visual_proxy_state_machine.hpp'
G='tools/network-visual-proxy-readiness/doc58d1-contract-audit/multi_slot_snapshot_guard.hpp'
T='tools/network-visual-proxy-readiness/doc58d1-contract-audit/test_doc58d1_reconciled.cpp'
P='experiments/m1/native_network_visual_proxy_58b/src/visual_proxy_58b.hpp'
A58='docs/58A_NATIVE_VISUAL_PROXY_COMPLETION.md'
rows=[
 # contract, source, evidence, status, defect, action
 ('Remove is synchronous (ConfirmedSuccess on return => ABSENT)',H+':517-522 executePendingRemove',A58+' D6/C02: Remove -> later Drain -> OnRemove -> PoolReturn in Sweep; G2 asks exactly this','BLOCKED',
  'D1: KEX unlinks in a later Drain; returning ABSENT on Remove return allows a re-Spawn/forgotten handle before PoolReturn','PATCHED: RemoveOutcome::Requested -> RETIRING; ABSENT only after native retirement witness (payload null/refs0/in_use0/occurrences0); test C06/C26/C29'),
 ('Spawn exception keeps SPAWN_PENDING',H+':447-450; Gemini test lines 637-638 assert it','Exception after a native factory call proves nothing about the Actor/ObjectNode created','BLOCKED',
  'D2: next tick re-Spawns with undetermined ownership (possible ghost Actor)','PATCHED: SpawnOutcome Unknown/exception -> QUARANTINED, spawn budget consumed; test C09'),
 ('Spawn nullptr => ABSENT and retry',H+':453-456','nullptr return does not prove rollback of a partially created Actor','BLOCKED',
  'D2b: blind repeated Spawn','PATCHED: Created without handle -> QUARANTINED; only adapter-proven no-call outcomes (Deferred/Refused) may retry; tests C10/C11'),
 ('onPreUnload early return while m_inSeamCall',H+':587-588 (same in requestShutdown/onDisconnect/onPostUnload/onMapReady)','Gemini handoff: Unload must set the barrier before sectors are freed','BLOCKED',
  'D3: unload/shutdown/disconnect intent silently dropped; original Unload continues with no barrier','PATCHED: all lifecycle intents latched unconditionally and applied after the seam returns; tests C14/C15'),
 ('Teardown => ABSENT (LevelInvalidSkipped / onPostUnload)',H+':478-485, 565-571, 603-614','G1 OPEN: no evidence KEX Level::Unload frees/sweeps a live proxy; DOC58A/57 never unloaded with a live Actor','BLOCKED',
  'D4: Actor declared gone without witness; Spawn re-enabled; m_lastUnloadedActorPtr kept as raw address','PATCHED: ORPHANED terminal (no native call ever, no re-Spawn), diagnostic opaque handle only; tests C13/C20/C28'),
 ('configureSession enables Spawn and lowers teardown barrier',H+':273-287','DOC56: Welcome != native MapReady (30 stable ticks, identity lock)','BLOCKED',
  'D5: new epoch/session token alone re-enables native Spawn and clears barrier; ACTIVE Actor of old session adopted','PATCHED: session / native map identity (ClientGate Ready token) / sector readiness (adapter worldReady) / spawn permission separated; old-session Actor -> REMOVE_PENDING; tests C01/C02/C19/C20/C25'),
 ('handleSnapshot performs native update synchronously',H+':372-383','DOC58A handoff: apply as new current once per simulation step in StateTick.onLeave','BLOCKED',
  'D6: snapshot intake calls the seam (ENet bursts => several native writes per tick); exception swallowed as None','PATCHED: intake stores latest target only; tick() applies once per safe step (coalescing); tests C04/C30'),
 ('Update result ignored',H+':373-381','DOC58A controller returns refusal/stopped statuses','BLOCKED',
  'D7: refused transforms recorded as current; adapter stop not propagated','PATCHED: Applied/Unchanged/Refused/Stopped/Unknown; Stopped/Unknown -> REMOVE_PENDING; tests C05/C06'),
 ('Raw void* actor passed to callers',H+':56-65, 690-691','Task: no raw-memory actor API for network code','BLOCKED',
  'D8: network-side code holds/forwards KEX addresses','PATCHED: opaque ProxyHandle issued by the Frida adapter; DLL never sees an address; test C31'),
 ('registerSimulationThread re-registrable',H+':265-267','Affinity must bind to the attested KEX simulation TID','INFERRED',
  'Any caller could re-register the TID','PATCHED: bind once (test C18)'),
 ('Reentrancy guard bool m_inSeamCall',H+':241-251','All APIs first check the bound TID, so the bool is only a same-thread nesting guard','VERIFIED',
  'none for cross-thread (TID check precedes); only an additional guard','KEPT as defence; correct dispatch provided by DLL (no seam call from intake); G7 measured natively'),
 ('MultiSlotGuard8 per-slot identity/sequence/tick',G,'Same logic as DOC55 Guard with 8 slots; noexcept','VERIFIED',
  'Local-slot exclusion is NOT in the guard; slot(index) silently maps out-of-range to slot 0; uint32 wrap unhandled','KEPT byte-identical; local exclusion/invalid slot/capacity done by SnapshotRouter (tests C23-C25); wrap INFERRED out of scope'),
 ('Session change on reused slot id',G+' reset(gen,epoch)','reset clears all slots; old gen/epoch rejected as Identity','VERIFIED','none in the guard','Router reconfigures every proxy; old Actor removed (test C25)'),
 ('Exception-to-status at C ABI boundary','Gemini try/catch(...) with /EHsc','catch(...) does not catch SEH faults from native code','INFERRED',
  'A native AV inside the adapter would bypass the state machine','PATCHED: SEH boundary in seam_58b.hpp; 0 == UNKNOWN for every callback (JS exception in NativeCallback returns 0); seam permanently disabled after a fault'),
 ('Gate registry G1-G9',  'tools/.../GATE_ID_CORRECTIONS.md','IDs restored; definitions used unchanged','VERIFIED','none','Reused verbatim in NATIVE_GATE_RESULTS.json'),
 ('Gemini 74-scenario suite','commands/GEMINI_RUN_*; gemini_repro/TEST_RESULTS.json','Reproduced 73 PASS / 0 FAIL / 1 NVR on the unchanged header','VERIFIED',
  'Several PASS assertions encode D2/D3/D4 (e.g. spawn exception -> SPAWN_PENDING; requestShutdown inside spawn ignored -> ACTIVE; LevelInvalidSkipped -> ABSENT)','Old tests not modified; patched contracts covered by separate proxy58b_contract (32 cases); Gemini assertions that encode defects are listed here, not claimed to pass against the patch'),
 ('Level::Unload 0x1401c90e0 covers all unload paths','GATE_ID_CORRECTIONS.md G1','Only a DOC56 vtable-slot witness; call path not traced','UNKNOWN','Hooking a single address is not proven sufficient','DOC58B never unloads with a live proxy; read-only unload witness; G1 stays OPEN/NATIVE_VERIFICATION_REQUIRED'),
]
cols=['contract','source','evidence','status','defect','required_action']
dump(OUT/'CODE_REVIEW.json',dict(candidate=dict(header=dict(path=H,sha256=sha(ROOT/H)),guard=dict(path=G,sha256=sha(ROOT/G)),tests=dict(path=T,sha256=sha(ROOT/T))),
  verdict='PATCHED',summary='REJECTED as-is for native integration (8 blocking ownership/unload/affinity-contract defects); accepted as basis and PATCHED in the isolated copy '+P,
  patched=dict(path=P,sha256=sha(ROOT/P)),guard_copy=dict(path='experiments/m1/native_network_visual_proxy_58b/src/multi_slot_snapshot_guard.hpp',sha256=sha(LAB/'src/multi_slot_snapshot_guard.hpp'),identical_to_gemini=sha(LAB/'src/multi_slot_snapshot_guard.hpp')==sha(ROOT/G)),
  api_adaptations=['void* actor -> opaque ProxyHandle (uint64) issued by adapter','spawnVisualActor/updateVisualTransform/removeVisualActorEx -> spawn/update/remove/retirement with 0=UNKNOWN codes','ISectorValidator moved into the native adapter (geometry needs live KEX reads)','registerSimulationThread -> bindSimulationThread(tid) once','configureSession(epoch,gen) -> configureSession(gen,epoch) + onNativeMapReady(epoch,token)','executePendingSpawn/Remove/ShutdownCleanup -> single tick(seam,TickContext)','added requestRemove (adapter fault -> one Remove)','SnapshotRouter<Capacity> for slot demux/local exclusion'],
  columns=cols,rows=[dict(zip(cols,r)) for r in rows],utc=stamp()))
print('CODE_REVIEW written',len(rows))
