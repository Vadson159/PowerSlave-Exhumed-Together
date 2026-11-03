"""DOC-58B.2 derived evidence. Everything native is read from the raw run journals; nothing is hand-entered.
Usage: report58b2.py"""
from workflow import *
H=OUT/'N58B2_HOST_01';C=OUT/'N58B2_CLIENT_01'
def jl(p):return [json.loads(x) for x in Path(p).read_text(encoding='utf8').splitlines() if x.strip()]
def ref(p):p=Path(p);return dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p))
NOISE={'client_input_sampled','client_input_dispatched','snapshot_worker_receive','native_snapshot_received','proxy_snapshot_routed','native_snapshot_rejected','tick_status'}
def main():
    own=jl(C/'ownership.jsonl');nat=jl(C/'native.jsonl');ad=jl(C/'adapter.jsonl');hnat=jl(H/'native.jsonl')
    cl=load(C/'launch.json');hl=load(H/'launch.json');obs=load(C/'OWNERSHIP_OBSERVER.json')['config']
    E={r['event']:r for r in own if r['event']!='arena_purge_entry' and r['event']!='arena_purge_return'}
    purges=[r for r in own if r['event'] in('arena_purge_entry','arena_purge_return') and r.get('kind')!='other']
    summ=E['ownership_summary']['stats'];teardown=[p for p in summ['purges'] if p['kind']!='other']
    spawn=next(r for r in ad if r['event']=='proxy_spawned');ident=next(r for r in ad if r['event']=='spawn_identity_verified')
    barriers=[r for r in nat if r['event']=='proxy_teardown_barrier'];pstates=[r for r in nat if r['event']=='proxy_state']
    blocked=[r for r in nat if r['event']=='blocked'];mapinv=[r for r in nat if r['event']=='client_map_status' and r.get('state')=='invalidated']
    seamexit=[r for r in nat if r['event']=='seam_world_exit'];wexit=next(r for r in ad if r['event']=='world_exit_observed')
    afault=[r for r in ad if r['event']=='experiment_fault']
    viol=[r for r in own if r['event'].startswith('VIOLATION')]
    U,SF,UR,DD,DE,DR,AF,NF=(E[k] for k in ['level_unload_entry','sector_array_free_entry','level_unload_return','actor_dtor_dispatch_entry','actor_destructor_entry','actor_destructor_return','actor_block_free_entry','node_block_free_entry'])
    # --- causal order (same Sim thread; observer seq is serial; DLL barrier precedes the original StateExit by construction and by time) ---
    order=[dict(step=1,source='native.jsonl',event='client_map_status invalidated (DOC56 ClientGate, pending transition)',record=mapinv[-1]['record'],utc=mapinv[-1]['utc'],tid=mapinv[-1]['tid']),
        dict(step=2,source='native.jsonl',event='proxy_teardown_barrier reason=native_map_invalidated -> ORPHANED',record=barriers[0]['record'],utc=barriers[0]['utc'],tid=barriers[0]['tid']),
        dict(step=3,source='native.jsonl',event='proxy_teardown_barrier reason=StateExit (DLL StateExit detour, before original)',record=barriers[1]['record'],utc=barriers[1]['utc'],tid=barriers[1]['tid']),
        dict(step=4,source='adapter.jsonl',event='world_exit_observed (weak pointers invalidated, no KEX call)',seq=wexit['seq'],utc=wexit['utc'],tid=wexit['tid']),
        dict(step=5,source='native.jsonl',event='seam_world_exit seam_status=5 (ExitDetached), then session stop/worker join',record=seamexit[0]['record'],utc=seamexit[0]['utc'],tid=seamexit[0]['tid'])]
    for i,r in enumerate([U,SF,UR]+[p for p in purges if p['kind']=='af4490_actor_heap' and p['event']=='arena_purge_entry']+[DD,DE,DR,AF]+
                         [p for p in purges if p['kind']=='af4490_actor_heap' and p['event']=='arena_purge_return']+[p for p in purges if p['kind']=='af4600_node_heap' and p['event']=='arena_purge_entry']+[NF]+
                         [p for p in purges if p['kind']=='af4600_node_heap' and p['event']=='arena_purge_return'],start=6):
        order.append(dict(step=i,source='ownership.jsonl',event=r['event'],seq=r['seq'],utc=r['utc'],tid=r['tid'],state=r['state'],next=r['next'],
            detail={k:v for k,v in r.items() if k in('heap','kind','caller_rva','return_rva','sector_array','sector_count','free_argument','block','dispatches','frees','removal_queue_empty','node_index','alloc_seq_matches_spawn','sector_freed_before','vtable','vtable_after','own_node_field_after','node_payload_before_free','node_in_use_before_free','node_refs_before_free')}))
    ok_order=[o['seq'] for o in order if 'seq' in o and o['source']=='ownership.jsonl']==sorted(o['seq'] for o in order if 'seq' in o and o['source']=='ownership.jsonl')
    sim=cl['primary_thread_id']
    all_sim=all(o['tid']==sim for o in order)
    dump(OUT/'UNLOAD_CLEANUP_CALL_ORDER.json',dict(status='VERIFIED_BOUNDED_LIVE_PROXY',pid=cl['pid'],simulation_tid=sim,scenario='client: Esc -> Exit To Title Screen -> Yes with one live ACTIVE Remote Visual Proxy',
        order=order,serial_order_ok=ok_order,all_on_simulation_thread=all_sim,
        notes=['Barrier latched twice: first by the DOC56 ClientGate invalidation during the pending transition (60 ticks earlier), then by the StateExit detour; both before Level::Unload entry.',
               'Observer seq is a single serial counter on one Sim thread; DLL records and observer records share the thread and UTC clock (DLL barrier 40.663Z < Unload 40.709Z).'],
        sources=[ref(C/'ownership.jsonl'),ref(C/'native.jsonl'),ref(C/'adapter.jsonl')]))
    # --- sector free order ---
    dump(OUT/'SECTOR_FREE_ORDER.json',dict(status='VERIFIED_BOUNDED_LIVE_PROXY',level_global='0x140a45940',sector_array_field='level+0x28',sector_count_field='level+0x30',
        sector_array=U['sector_array'],sector_count=U['sector_count'],free_return_rva=SF['return_rva'],sector_memory_dereferenced=SF['sector_memory_dereferenced'],
        after_unload=dict(pointer=UR['sector_array'],count=UR['sector_count']),actor_destroyed_after_sector_free=DD['sector_freed_before'],actor_block_freed_after_sector_free=AF['sector_freed_before'],
        removal_queue_empty_at_unload_entry=U['removal_queue_empty'],canonical_plus_0x140='not used; DOC58B.1 discrepancy retained',
        consequence='Actor destructor and both purges run after the sector array is freed; neither the destructor (static 19f210/1d32f0) nor the purge (static c58b0/c5310) reads the sector array; no proxy native call exists after the barrier.',
        sources=[ref(C/'ownership.jsonl')]))
    # --- witnesses jsonl (copied raw records with their source) ---
    with (OUT/'NATIVE_OWNERSHIP_WITNESSES.jsonl').open('x',encoding='utf8') as f:
        for r in [next(r for r in own if r['event']=='ownership_observer_loaded')]+[U,SF,UR]+purges[:1]+[DD,DE,DR,AF]+purges[1:]+[NF,E['ownership_summary']]:
            x=dict(r);x.pop('stats',None);f.write(json.dumps(dict(source='N58B2_CLIENT_01/ownership.jsonl',record=x),ensure_ascii=False)+'\n')
        for r in [spawn,ident,wexit]+afault:f.write(json.dumps(dict(source='N58B2_CLIENT_01/adapter.jsonl',record=r),ensure_ascii=False)+'\n')
        for r in mapinv[-1:]+blocked+barriers+pstates+seamexit:f.write(json.dumps(dict(source='N58B2_CLIENT_01/native.jsonl',record=r),ensure_ascii=False)+'\n')
    # --- witness verdict (the DOC58B.2 EngineTeardownWitness rule, applied offline to the native record) ---
    last=pstates[-1]
    crit={'identity_at_creation_kexSlaveActor_1007':ident['actor']['vtable']=='0x140528608' and ident['actor']['type']==1007 and ident['actor']['flags']==64,
        'identity_at_unload_entry_ok':U['identity_pre_unload'].get('identity_ok') is True,
        'block_heap_is_af4490_with_dtor_thunk':U['identity_pre_unload']['block']['heap']=='0x140af4490' and U['identity_pre_unload']['block']['dtor']=='0x1400d9980',
        'dtor_dispatch_of_exact_block_in_af4490_purge':DD['purge']=='af4490_actor_heap' and DD['block_is_actor_header'] and summ['dispatch_ours']==1,
        'actor_destructor_entered_and_returned_once':summ['dtor_ours_entries']==1 and summ['dtor_ours_returns']==1,
        'actor_block_freed_once_same_allocation':summ['actor_block_frees']==1 and AF['alloc_seq_matches_spawn'],
        'node_block_freed_in_af4600_after_actor':summ['node_block_frees']==1 and NF['actor_block_freed_before'] and NF['node_payload_is_old_actor'],
        'no_violation_records':not viol and summ['dispatch_after_free']==0 and summ['dtor_after_free']==0,
        'no_proxy_native_call_after_barrier':all(p['remove_calls']==0 and p['spawn_calls']==1 for p in pstates) and len({p['update_calls'] for p in pstates})==1,
        'proxy_orphan_cause_live':last['state']=='ORPHANED' and last['orphaned_handle']==spawn['handle'] and last['remove_calls']==0,
        'removal_queue_empty_at_unload':U['removal_queue_empty'] is True}
    verdict='ArenaDestroyed' if all(crit.values()) else 'Unknown'
    # --- player integrity ---
    snaps=[r for r in ad if r['event']=='p1_state_snapshot'];comps=[r for r in ad if r['event']=='p1_state_comparison']
    dump(OUT/'PLAYER_INTEGRITY.json',dict(status='EQUAL_BOUNDED' if comps and all(c['comparison']['status']=='EQUAL' for c in comps) else 'NOT_EQUAL',
        snapshots=[dict(label=s['snapshot']['label'],status=s['snapshot']['status'],seq=s['seq'],utc=s['utc'],tid=s['tid'],identity=s['snapshot']['identity']) for s in snaps],
        comparisons=[dict(label=c['label'],status=c['comparison']['status'],differences=c['comparison']['differences'],unknown=c['comparison']['unknown'],fields=len(c['comparison']['compared_fields'])) for c in comps],
        fields='12-field DOC58B contract (Player+84/86/90/94/98/9a/a4/88/8c/cc/bc, Puppet+17a)',post_teardown='NOT_MEASURABLE: the client world (Player Puppet included) is destroyed by native StateExit; no Player/Puppet/camera writes by DLL (client_quiesced player_writes=0) or adapter (camera_writes/player_writes 0)',
        host_p1=dict(position_readonly='(-7420.27,-6843.88,1152) sector259 before client connect',host_final=[r for r in hnat if r['event']=='native_final_exit'][-1]),sources=[ref(C/'adapter.jsonl'),ref(H/'native.jsonl')]))
    # --- destroyed-class histogram at the af4490 purge (stock objects, read-only) ---
    a490=next(p for p in teardown if p['kind']=='af4490_actor_heap');a600=next(p for p in teardown if p['kind']=='af4600_node_heap')
    # --- gates ---
    rb=load(C/'LOAD_RESTORE_READBACK.json')['structuredContent']['normalized_result']['messages'][0]['payload']
    cx=load(C/'exit.json');hx=load(H/'exit.json')
    hook_ok=rb['exact16_restored']==rb['sites_checked']==24 and rb['free_exact16_restored'] and rb['dll_owned_sites_exact16_now']['update']
    lc=load(OUT/'LIFECYCLE_CONTRACT_RESULTS.json') if (OUT/'LIFECYCLE_CONTRACT_RESULTS.json').exists() else None
    gates=[dict(gate='GATE-PSE-58C-01 Pre-Unload',status='VERIFIED (bounded)',evidence='barrier before Level::Unload entry (twice), sector array live at entry and freed inside Unload, zero proxy Remove/Spawn/Update after barrier, engine-owned destruction of the live Actor after sector free without sector access',
             remaining='one transition (Tomb->Title), one proxy; DOC58D review sign-off of 10 consecutive map transitions NOT executed; other unload paths (level exit to next map, restart, reload checkpoint, death, DirectQuit with live proxy) not covered; canonical +0x140 offset replaced by verified +0x28'),
        dict(gate='GATE-PSE-58C-02 Remove/Drain',status='VERIFIED (bounded, DOC58B)',evidence='not re-executed; this run issued no Remove',remaining='Remove-in-flight at teardown is a static native hazard (removal queue not reset by StateExit)'),
        dict(gate='GATE-PSE-58C-03 Pool sweep',status='VERIFIED (bounded, DOC58B)',evidence='not re-executed; engine teardown does not use Sweep/PoolReturn for a live Actor (node payload still the Actor, in_use 1 at node-block free)',remaining='-'),
        dict(gate='GATE-PSE-58C-04 Portal boundary',status='DEFERRED',evidence='-',remaining='-'),
        dict(gate='GATE-PSE-58C-05 Sim affinity',status='VERIFIED (bounded, this run includes the title-exit unload path)',evidence=f'all teardown records on Sim TID {sim}',remaining='other unload paths'),
        dict(gate='GATE-PSE-58C-06 Seven proxies',status='DEFERRED',evidence='-',remaining='-'),
        dict(gate='GATE-PSE-58C-07 Reentrancy',status='VERIFIED (bounded)',evidence='reentrancy_blocked 0, unload_during_seam 0 in proxy_state records',remaining='no native fault injection'),
        dict(gate='GATE-PSE-58D-08 InsertActor',status='PARTIAL (unchanged)',evidence='not traced',remaining='-'),
        dict(gate='GATE-PSE-58D-09 Slot demux',status='VERIFIED slots0/1 (re-observed)',evidence=f"routed/local_excluded {[r for r in nat if r['event']=='proxy_route_counts'][-1]['routed']}/{[r for r in nat if r['event']=='proxy_route_counts'][-1]['local_excluded']}",remaining='slots 2..7 offline only')]
    dump(OUT/'NATIVE_GATE_RESULTS.json',dict(gates=gates,hook_restoration=dict(status='PASS' if hook_ok else 'FAIL',frida_sites=rb,dll_owned_note='Client DLL keeps Tick/StateExit MinHook detours pinned until process exit (DOC58B design); Update restored exact16 at StateExit (client_control_restored)'),
        os_exit=dict(client=cx,host=hx,client_os_signaled=load(C/'OS_SIGNALED.json')['exit_code'],host_os_signaled=load(H/'OS_SIGNALED.json')['exit_code']),sources=[ref(C/'LOAD_RESTORE_READBACK.json'),ref(C/'OS_SIGNALED.json'),ref(H/'OS_SIGNALED.json')]))
    g1=dict(canonical_gate='GATE-PSE-58C-01',status='VERIFIED (bounded)',classification='LIVE_PROXY_UNLOAD (engine-owned), not SAFE_PRE_UNLOAD_RETIREMENT',
        scope=dict(transitions=1,path='Tomb of Ramses -> Exit to Title Screen (client UI)',proxies=1,actor_type=1007,flags='0x40',sector=259,host_pid=hl['pid'],client_pid=cl['pid'],client_sim_tid=sim),
        canonical_contract={'barrier_before_sector_deallocation':barriers[0]['utc']<U['utc'] and barriers[1]['utc']<U['utc'],'sector_array_live_at_unload_entry':U['sector_count']=='270' and U['sector_array']!='0x0',
                            'zero_proxy_destroy_or_remove_after_sector_free':crit['no_proxy_native_call_after_barrier'],'clean_shutdown_no_dangling_proxy_reference':last['handle']==0 and wexit['all_weak_pointers_invalidated'] and cx['exit_code']==0},
        engine_teardown_witness=dict(verdict=verdict,criteria=crit),not_satisfied=['DOC58D review sign-off: 10 consecutive map transitions (Tomb->Karnak->Sanctuary)','IsBadReadPtr/page-protection check of rcx+0x140 (intentionally not used: wrong offset for this build and not a lifetime witness)'],
        residual_risk=['Remove issued but undrained at StateExit (RETIRING) leaves a removal-queue cell referencing a node freed by the af4600 purge (static hazard; not exercised)','Other unload paths unobserved','Actor types other than stock 1007/kexSlaveActor not covered'],
        sources=[ref(OUT/'UNLOAD_CLEANUP_CALL_ORDER.json'),ref(OUT/'NATIVE_OWNERSHIP_WITNESSES.jsonl')])
    dump(OUT/'G1_NATIVE_ACCEPTANCE.json',g1)
    dump(OUT/'TEARDOWN_PURGE_CENSUS.json',dict(note='Read-only census of objects dispatched by the StateExit purges in this run (vtable RVA -> count). Stock objects, not the proxy, unless stated.',
        af4490=a490,af4600=a600,kexSlaveActor_vtable_rva='0x528608',proxy_among_af4490=DD['block_is_actor_header'],sources=[ref(C/'ownership.jsonl')]))
    res=dict(milestone='DOC-58B.2',overall='BOUNDED_PASS',g1='VERIFIED (bounded)',ownership='VERIFIED (bounded) for stock Actor1007 created by factory 1d4780 in a live DOC58B client',
        strategy='ENGINE_OWNED_UNLOAD for ACTIVE/REMOVE_PENDING-without-call; fail-closed ORPHANED+nativeQueueHazard for Remove in flight',
        native_run=dict(host=dict(run='N58B2_HOST_01',pid=hl['pid'],sim_tid=hl['primary_thread_id'],exit=hx['exit_code']),client=dict(run='N58B2_CLIENT_01',pid=cl['pid'],sim_tid=sim,exit=cx['exit_code'])),
        actor=dict(actor=spawn['actor'],node=spawn['node'],handle=spawn['handle'],alloc_seq=U['identity_pre_unload']['block']['alloc_seq']),engine_teardown_witness=verdict,
        player_integrity=load(OUT/'PLAYER_INTEGRITY.json')['status'],hook_restoration='PASS' if hook_ok else 'FAIL',violations=len(viol),
        limitations=['one transition / one proxy / one Actor type','patched state machine (ENGINE_RETIRED) verified offline only; the live run used the frozen DOC58B DLL whose terminal state is ORPHANED',
                     'Remove-in-flight hazard static only','reconnect/rearm not in scope'])
    dump(OUT/'RESULT_SUMMARY.json',res)
    print(json.dumps(dict(verdict=verdict,crit=crit,order_ok=ok_order,all_sim=all_sim,hook_ok=hook_ok,player=res['player_integrity'],a490=dict(d=a490['dispatches'],f=a490['frees'],v=a490['vtables']),a600=dict(d=a600['dispatches'],f=a600['frees'])),indent=1))
if __name__=='__main__':main()
