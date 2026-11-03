"""DOC-58B.2 static ownership graph + lifecycle contract results (from saved exact-build exports and command logs)."""
from workflow import *
G=ROOT/'docs/evidence/ghidra_2022';S=OUT/'static'
def ev(p,claim,va):return dict(path=Path(p).relative_to(ROOT).as_posix(),sha256=sha(p),va=va,claim=claim)
edges=[
 dict(id='E1',source='KEX Level (0x140a45940)',target='Sector array (level+0x28, count +0x30, stride 0x88)',kind='STRONG ownership (CRT malloc/free)',
   created_by='level load',freed_by='Level::Unload 1c90e0 via CALL [IAT 482ae8] at 1401c92dc',phase='StateExit case3 -> level vtable+0x10',
   status='VERIFIED static + native (DOC58B.1 and this run)',evidence=[ev(G/'G_FUNC_1401c90e0.json','sector loop then free(level+0x28); zero pointer/count','0x1401c90e0')]),
 dict(id='E2',source='Sector entry',target='Actor-list sentinel ObjectNode pair (*(sector+0x78))',kind='BORROWED pointer: sentinel nodes are allocated from node blocks (heap af4600), not inside the sector array',
   created_by='sector load 1ca930 (two 1d5820 nodes, circular)',freed_by='af4600 purge (bulk); Level::Unload never touches sector+0x78',phase='StateExit after Unload',
   status='VERIFIED static',evidence=[ev(S/'G_FUNC_1401ca930.json','sector+0x78 = new sentinel pair from 1d5820','0x1401ca930'),ev(G/'G_FUNC_1401d3d70.json','LinkSector inserts own node after *(sector+0x78)','0x1401d3d70')]),
 dict(id='E3',source='Sector actor list',target='ObjectNode (Actor own node)',kind='BORROWED intrusive link (prev+0/next+8); the list holds no reference count',
   created_by='LinkSector 1d3d70 (node from 1d5820)',unlinked_by='Drain 1b61d0 (after Remove) or LinkSector sector change; NOT by StateExit',freed_by='af4600 purge of the containing 0x1210 node block',phase='StateExit',
   status='VERIFIED static; native: node still in_use=1 and payload=Actor at node-block free',evidence=[ev(G/'G_FUNC_1401d5820.json','node alloc from DAT_140aec868 block list','0x1401d5820'),ev(S/'G_FUNC_1401d5360.json','node block = c5430(0x1210, heap af4600)','0x1401d5360')]),
 dict(id='E4',source='ObjectNode payload+0x28',target='Actor',kind='BORROWED (weak) back-reference; refs+0x40 counts external holders, not Actor ownership',
   created_by='LinkSector',cleared_by='Sweep 1b6360 after OnRemove/PoolReturn; NOT during StateExit',phase='-',status='VERIFIED static + native',evidence=[ev(S/'G_FUNC_1401b6360.json','Sweep clears payload after OnRemove+PoolReturn','0x1401b6360')]),
 dict(id='E5',source='Tagged heap af4490 (SlaveObjectFactory)',target='Actor storage (kexSlaveActor, 0x1b8)',kind='STRONG ownership (block header magic 0x3151983, heap ptr +0x18, dtor callback +0x40)',
   created_by='class create 19ee40 = c5430(0x1b8, heap aecca0) with dtor thunk d9980, then factory 1d4893 c59e0 retags into af4490',freed_by='StateExit c58b0(af4490): per block c5310 -> thunk d9980 -> vtable+8 19f1d0 -> 19f210 -> 1d32f0, then free(block)',
   phase='StateExit after Level::Unload',status='VERIFIED static + native (this run: heap 0x140af4490, dtor 0x1400d9980, dispatch caller c594d, dtor return 19f1e4, free return c5994)',
   evidence=[ev(S/'G_FUNC_1400c58b0.json','purge loop: c5310 then free','0x1400c58b0'),ev(S/'G_FUNC_1400c5310.json','dtor callback dispatch','0x1400c5310'),ev(S/'G_FUNC_1400c59e0.json','retag to af4490','0x1400c59e0'),ev(S/'G_FUNC_14019ee40.json','kexSlaveActor create','0x14019ee40'),ev(S/'G_INSPECT_1400d9980.json','MOV RAX,[RCX]; XOR EDX,EDX; JMP [RAX+8]','0x1400d9980'),ev(S/'G_FUNC_1400c5430.json','tagged alloc header','0x1400c5430')]),
 dict(id='E6',source='Class pool DAT_140af45f0[class] (malloc 0x10 cells)',target='Retired Actor storage (after PoolReturn)',kind='BORROWED reuse list; storage stays owned by af4490',
   created_by='PoolReturn 1d5000',cleared_by='factory vtable+8 = 1d4170 (pops/frees cells only, never dereferences payloads) after both purges',phase='StateExit tail',status='VERIFIED static',evidence=[ev(S/'G_FUNC_1401d4170.json','pool cell drain','0x1401d4170'),ev(G/'G_FUNC_1401d5000.json','PoolReturn pushes payload cell','0x1401d5000'),ev(S/'G_MEMORY_14052c078.json','factory vtable slot+8 = 1d4170','0x14052c078')]),
 dict(id='E7',source='Removal queue (globals af3380/af3388, malloc cells)',target='ObjectNode (+1 queue ref)',kind='BORROWED counted reference',
   created_by='Remove 1a02b0 -> 1d3460 -> GI vtable+0xb0 = 1b5530',cleared_by='Drain 1b61d0 only (StateTick live-world branch: pending==0, no menu); never by StateExit/StateEnter (refs: static init, Drain, atexit only)',
   phase='-',status='VERIFIED static; HAZARD: an undrained cell survives StateExit while its node block is freed by the af4600 purge',evidence=[ev(G/'G_FUNC_1401b5530.json','queue push with node ref','0x1401b5530'),ev(G/'G_FUNC_1401b61d0.json','Drain pops and unlinks','0x1401b61d0'),ev(S/'G_REFS_140af3388.json','no StateExit reference','0x140af3388'),ev(G/'G_FUNC_1401b6ec0.json','Sweep/Drain only inside live-world branch','0x1401b6ec0'),ev(S/'G_FUNC_1401d3460.json','Remove helper queues own node','0x1401d3460')]),
 dict(id='E8',source='Actor fields +0x28 target, +0x140, +0x148 source owner',target='other ObjectNodes',kind='COUNTED references released by the Actor destructor; nodes still alive because af4600 is purged after af4490',
   created_by='gameplay setters',freed_by='destructor during af4490 purge',phase='StateExit',status='VERIFIED static (+ M0 C19 runtime for projectiles/puppets)',evidence=[ev(G/'G_FUNC_14019f210.json','releases +0x148/+0x140, then base','0x14019f210'),ev(G/'G_FUNC_1401d32f0.json','base releases +0x28; does not touch own node +0x10 or any sector','0x1401d32f0')]),
 dict(id='E9',source='Remote Visual Proxy (DOC58B state machine)',target='Actor',kind='NONE: opaque handle only; the mod never owns KEX storage',created_by='factory via adapter (one call)',freed_by='never by the proxy at unload (barrier)',phase='-',status='VERIFIED source + native (remove_calls 0)',evidence=[])]
order=[('StateExit 1b6670 entry (DLL detour: barrier, seam detach, session stop first)','native'),('GI+0x118 local Puppet handle release, services','static'),
 ('Level::Unload 1c90e0: sector+0x60 edge refs--, free(sector array), zero fields','native'),
 ('c58b0(af4490): every factory object destroyed by dtor callback then free (no unlink, no Sweep/OnRemove/PoolReturn)','native'),
 ('c58b0(af4600): node blocks freed wholesale (sentinels and own nodes, no unlink, no destructor)','native'),
 ('factory vtable+8 1d4170: pool cells dropped without payload reads','static'),('tail jump: next state enter','static')]
dump(OUT/'STATIC_OWNERSHIP_GRAPH.json',dict(binary_sha256=EXPECTED,image_base='0x140000000',runtime_base_observed='0x140000000',edges=edges,stateexit_case3_order=order,
  distinct_operations={'Actor destructor':'19f210 (via d9980 -> vtable+8 19f1d0) at tagged-heap purge; not OnRemove','ObjectNode unlink':'Drain 1b61d0 / LinkSector only; never at StateExit',
   'PoolReturn':'1d5000 from Sweep only','ObjectNode retirement':'Sweep 1b6360 (payload null, in_use 0)','Bulk arena deallocation':'c58b0 per heap: af4490 objects (with destructors), af4600 node blocks (no destructors)'},
  variant='D (engine-owned tagged-heap teardown): closest to C, with per-object destructor callbacks for factory objects and bulk free for node blocks; no individual Remove/Drain/retirement',
  labels='VERIFIED = exact-build instruction/decompiler export with evidence; native = this run or DOC58B.1; decompiler text is not source'),overwrite=True)
res={}
for t in ['BUILD_02','TEST_DOC58B_02','TEST_DOC58B1_02','TEST_DOC58B2_02','TEST_ADAPTER58B_02','TEST_DOC58A_02','TEST_DOC58B2_01']:
    r=load(OUT/'commands'/t/'RESULT.json');log=(ROOT/r['log']).read_text(encoding='utf8',errors='replace').strip().splitlines()
    res[t]=dict(exit=r['exit_code'],summary=[l.strip() for l in log if 'TOTAL' in l or '"passed"' in l][-1:] or log[-1:],log=r['log'])
dump(OUT/'LIFECYCLE_CONTRACT_RESULTS.json',dict(results=res,superseded=dict(TEST_DOC58B2_01='T08 assertion arithmetic in the test (expected 3 refusals, actual 4 including T06); code unchanged, assertion corrected, attempt 02 PASS'),
  compiler='MSVC C++20 /W4 /WX /permissive- /EHsc (build tree b582)',native_replay='report58b2.py applies the EngineTeardownWitness rule to the N58B2_CLIENT_01 journal -> ArenaDestroyed (G1_NATIVE_ACCEPTANCE.json); T03/T13 cover the resulting ORPHANED -> ENGINE_RETIRED transition'),overwrite=True)
print('ok')
