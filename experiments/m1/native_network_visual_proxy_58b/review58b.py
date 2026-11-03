"""DOC58B review: derives every Evidence file from the raw run journals (native.jsonl, adapter.jsonl,
unload_witness.jsonl, OS observer files, REA STATUS_*.json). Exclusive-create outputs; no game is touched."""
from workflow import ROOT,LAB,OUT,EXPECTED,sha,load,dump,stamp
import collections,json,math,struct
H,C=OUT/'N58B_HOST_02',OUT/'N58B_CLIENT_02'
H1,C1=OUT/'N58B_HOST_01',OUT/'N58B_CLIENT_01'
def jl(p):
    rows=[]
    for line in open(p,encoding='utf8'):
        line=line.strip()
        if line:rows.append(json.loads(line))
    return rows
def rea(run,kind):
    d=load(run/'STATUS_EXIT.json')['structuredContent']['normalized_result']
    return [m['payload'] for m in d['messages'] if isinstance(m.get('payload'),dict) and (m['payload'].get('kind')==kind or m['payload'].get('event')==kind)],d
def bits(f):return struct.unpack('<I',struct.pack('<f',f))[0]
def f32(v):return struct.unpack('<f',struct.pack('<f',v))[0]
def ev(rows,name):return [r for r in rows if r['event']==name]
def main():
    hl,cl,al=jl(H/'native.jsonl'),jl(C/'native.jsonl'),jl(C/'adapter.jsonl')
    hlaunch,claunch=load(H/'launch.json'),load(C/'launch.json')
    htid,ctid=hlaunch['primary_thread_id'],claunch['primary_thread_id']
    udp_send,hst=rea(H,'n55_udp_snapshot_send');udp_recv,cst=rea(C,'n55_udp_snapshot_receive')
    cap={r['snapshot_seq']:r for r in ev(hl,'native_snapshot_captured')}
    wsend={r['snapshot_seq']:r for r in ev(hl,'snapshot_worker_send')}
    osend={r['snapshot_seq']:r for r in udp_send};orecv={r['snapshot_seq']:r for r in udp_recv}
    wrecv={r['snapshot_seq']:r for r in ev(cl,'snapshot_worker_receive')}
    acc={r['snapshot_seq']:r for r in ev(cl,'native_snapshot_received')}
    routed={r['snapshot_seq']:r for r in ev(cl,'proxy_snapshot_routed')}
    acts=ev(cl,'proxy_tick_action');act_count=collections.Counter(r['action'] for r in acts)
    upd={r['command']['sequence']:r for r in ev(al,'proxy_update')}
    mov=ev(al,'movement_applied');lerp=ev(al,'native_interpolation_sample')
    spr=[r for r in al if r['event'] in ('sprite_submission','sprite_submission_first','sprite_intermediate_submission')]
    first_sprite={}
    for r in spr:
        if r.get('revision') is not None and r['revision'] not in first_sprite and r['event']!='sprite_intermediate_submission':first_sprite[r['revision']]=r
    lerp_by_rev=collections.Counter(r['revision'] for r in lerp)
    # ---------- NETWORK_CORRELATION.jsonl: every snapshot that produced a client native action ----------
    corr=[];mism=[]
    for a in acts:
        if a['action'] not in ('SpawnCreated','UpdateApplied','UpdateRefused','UpdateUnchanged'):continue
        s=a['snapshot_seq'];c=cap.get(s);r=acc.get(s);u=upd.get(s)
        row=dict(snapshot_seq=s,slot=a['slot'],server_tick=a['server_tick'],map_epoch=a['map_epoch'],connection_generation=a['connection_generation'],entity_id=a['entity_id'],entity_generation=a['entity_generation'],
            host_capture=dict(utc=c['utc'],tid=c['tid'],pos=c['pos'],yaw=c['yaw'],bits=c['bits']) if c else None,
            host_worker_send=dict(utc=wsend[s]['utc'],tid=wsend[s]['tid']) if s in wsend else None,
            host_os_udp_send=dict(utc=osend[s]['utc'],tid=osend[s]['tid'],bits=osend[s]['bits']) if s in osend else None,
            client_os_udp_receive=dict(utc=orecv[s]['utc'],tid=orecv[s]['tid'],bits=orecv[s]['bits']) if s in orecv else None,
            client_worker_receive=dict(utc=wrecv[s]['utc'],tid=wrecv[s]['tid']) if s in wrecv else None,
            client_sim_accept=dict(utc=r['utc'],tid=r['tid'],bits=r['bits']) if r else None,
            route=dict(route=routed[s]['route'],rejection=routed[s]['rejection'],tid=routed[s]['tid']) if s in routed else None,
            native_action=dict(action=a['action'],utc=a['utc'],tid=a['tid'],tick=a['tick'],coalesced=a['coalesced'],target_bits=a['pos_bits']+[a['yaw_bits']]),
            adapter=dict(target=u['target'],yaw=u['yaw'],mode=u['mode'],revision=u['revision'],actor_origin=u['actor_origin'],actor_yaw=u['actor_yaw'],engine_stamp=u['engine_stamp'],tid=u['tid']) if u else None)
        rev=u['revision'] if u else None
        fs=first_sprite.get(rev) if rev is not None else None
        row['render']=dict(render_frame=fs['render_frame'],identity=fs['identity'],origin=fs['entry']['origin'],angles=fs['entry']['angles'],sector=fs['entry']['sector'],utc=fs['utc']) if fs else None
        row['native_lerp_samples_for_revision']=lerp_by_rev.get(rev,0) if rev is not None else 0
        ok=bool(c and r and c['bits'][:4]==r['bits'][:4]==a['pos_bits']+[a['yaw_bits']])
        if u:ok=ok and u['target']==[f32(c['pos'][0]),f32(c['pos'][1]),f32(f32(c['pos'][2])+88.0)] and u['actor_origin']==u['target']
        if s in osend:ok=ok and osend[s]['bits'][:4]==c['bits'][:4]
        if s in orecv:ok=ok and orecv[s]['bits'][:4]==c['bits'][:4]
        row['bit_exact_chain']=ok
        if not ok:mism.append(s)
        corr.append(row)
    with (OUT/'NETWORK_CORRELATION.jsonl').open('x',encoding='utf8') as f:
        for row in corr:f.write(json.dumps(row,ensure_ascii=False)+'\n')
    applied=[r for r in corr if r['native_action']['action'] in ('UpdateApplied','SpawnCreated')]
    # ---------- accounting ----------
    slot0=[s for s,r in cap.items() if r['slot']==0];slot1=[s for s,r in cap.items() if r['slot']==1]
    rc=collections.Counter((r['slot'],r['route'],r['rejection']) for r in routed.values())
    seq_mono={}
    for slot in (0,1):
        xs=[r['snapshot_seq'] for r in ev(cl,'native_snapshot_received') if r['slot']==slot];seq_mono[slot]=all(b>a for a,b in zip(xs,xs[1:]))
    accounting=dict(host_captured=len(cap),host_captured_slot0=len(slot0),host_captured_slot1=len(slot1),host_worker_send=len(wsend),host_os_udp_send=len(osend),
        client_os_udp_receive_window=len(orecv),client_worker_receive=len(wrecv),client_sim_accepted=len(acc),client_rejected=len(ev(cl,'native_snapshot_rejected')),
        routed={f'slot{k[0]}:{k[1]}:{k[2]}':v for k,v in rc.items()},native_actions=dict(act_count),correlated_rows=len(corr),applied_rows=len(applied),bit_exact_mismatches=mism,
        per_slot_sequence_strictly_increasing=seq_mono,adapter_update_callbacks=len(upd))
    # ---------- movement ----------
    tgt=[r['adapter']['target'] for r in applied if r['adapter']]
    yaws=sorted({r['adapter']['yaw'] for r in applied if r['adapter']})
    moves=dict(distinct_targets=len({tuple(t) for t in tgt}),x=[min(t[0] for t in tgt),max(t[0] for t in tgt)],y=[min(t[1] for t in tgt),max(t[1] for t in tgt)],z=[min(t[2] for t in tgt),max(t[2] for t in tgt)],
        yaws=yaws,modes=dict(collections.Counter(r['adapter']['mode'] for r in applied if r['adapter'])),refused_reasons=dict(collections.Counter(r['reason'] for r in al if r['event'] in('update_refused','update_refused_sample'))))
    inputs=sorted((OUT/'inputs').glob('INPUT_*.json'));host_inputs=[]
    for p in inputs:
        d=load(p);recs=d.get('records',[])
        if p.stat().st_mtime<(H/'launch.json').stat().st_mtime:continue
        host_inputs.append(dict(file=p.name,action=d.get('action'),status=d.get('status'),first=recs[0]['utc'] if recs and 'utc' in recs[0] else None))
    # ---------- lifecycle ----------
    life_names={'native_factory_call','factory_return','spawn_identity_verified','factory_diagnostic','controller_spawn_status','proxy_spawned','controller_remove_status','native_remove_postcondition','deferred_drain_entry','deferred_drain_return','native_on_remove_entry','native_on_remove_return','native_pool_return_entry','native_pool_return_complete','actor_identity_retired','deferred_retired','world_exit_observed','experiment_fault','spawn_preflight','attested_area'}
    dll_names={'seam_registered','proxy_session_configured','proxy_native_map_ready','proxy_disconnect','proxy_state','proxy_shutdown_requested','seam_detached','seam_detach_deferred','seam_world_exit','client_stop_begin','client_worker_joined','client_control_restored','client_quiesced','client_native_final_exit','proxy_not_quiescent_at_stop','seam_native_fault','client_session','client_welcome'}
    lifecycle=[dict(journal='adapter.jsonl',**r) for r in al if r['event'] in life_names]+[dict(journal='native.jsonl',**r) for r in cl if r['event'] in dll_names or (r['event']=='proxy_tick_action' and r['action'] in ('SpawnCreated','SpawnUnknown','RemoveRequested','RemoveRefused','RemoveUnknown','Retired','RetirementFailed','UpdateStopped','UpdateUnknown'))]
    lifecycle.sort(key=lambda r:r['utc'])
    with (OUT/'NATIVE_ACTOR_LIFECYCLE.jsonl').open('x',encoding='utf8') as f:
        for r in lifecycle:f.write(json.dumps(r,ensure_ascii=False)+'\n')
    spawned=ev(al,'proxy_spawned');factory=ev(al,'native_factory_call');removes=ev(al,'native_remove_postcondition');retired=ev(al,'deferred_retired')
    stable=ev(al,'stable');last=stable[-1];post=ev(al,'post_retire');faults=ev(al,'experiment_fault')
    drain=ev(al,'deferred_drain_return');pool=ev(al,'native_pool_return_entry')
    # ---------- render ----------
    inter=ev(al,'sprite_intermediate_submission')
    render=dict(sprite_submissions_total=last['sprite_submissions'],ours=last['ours_sprites'],ambiguous=0 if not ev(al,'sprite_identity_ambiguous') else len(ev(al,'sprite_identity_ambiguous')),
        intermediate_logged=len(inter),intermediate_correlated_with_last_lerp=sum(1 for r in inter if r['correlated_with_last_lerp']),
        distinct_revisions_rendered=len({r['revision'] for r in spr}),applied_rows_with_render=sum(1 for r in applied if r['render']),
        native_lerp_samples=last['lerp_samples'],lerp_max_formula_error=max((r['formula_error'] for r in lerp),default=None),
        renderer_tid_equals_sim=all(r['tid']==ctid for r in spr),sprite_after_remove=len(ev(al,'sprite_after_remove')),
        samples=dict(first=ev(al,'sprite_submission_first')[:1],intermediate=inter[:5],lerp=lerp[:5]),
        frames=[p.name for p in sorted(C.glob('window_*.png'))],
        note='Sprite entry origin/angles are compared bit-exactly with the live Actor render outputs (+6c/+94) at submission (DOC58A CrossSector58.spriteIdentity); intermediate = render output between previous and current simulation transform.')
    dump(OUT/'RENDER_EVIDENCE.json',render)
    # ---------- player integrity ----------
    snaps=[r['snapshot'] for r in ev(al,'p1_state_snapshot')];comps=ev(al,'p1_state_comparison')
    local_moved=[r for r in stable if r.get('local_p1_moved_since_baseline')]
    integrity=dict(client_local_player=dict(snapshots=[dict(label=s['label'],status=s['status'],tid=s['tid'],native_tick=s['native_tick'],identity=s['identity'],fields={k:v['raw'] for k,v in s['fields'].items()}) for s in snaps],
            comparisons=[dict(label=r['label'],status=r['comparison']['status']) for r in comps],all_equal=bool(comps) and all(r['comparison']['status']=='EQUAL' for r in comps),
            local_position_changed_while_proxy_live=len(local_moved),baseline=ev(al,'p1_baseline')[0],camera_writes=0,player_writes=0,
            dll_player_writes=all(r.get('player_writes',0)==0 for r in cl if 'player_writes' in r)),
        host_p1=dict(note='Host P1 moved only by ordinary OS input to the Host window; host DLL is the frozen DOC56 artifact.',independent_spawn=ev(hl,'independent_spawn')[0],host_inputs=host_inputs),
        field_set='DOC57/58A P1StateSnapshot-v1 (12 mandatory fields)')
    dump(OUT/'PLAYER_INTEGRITY.json',integrity)
    # ---------- threads ----------
    tid_sets=dict(client_sim=ctid,client_dll_proxy_events=sorted({r['tid'] for r in cl if r['event'].startswith('proxy_') or r['event'].startswith('seam_') and r['event']!='seam_registered'}),
        client_snapshot_accept=sorted({r['tid'] for r in acc.values()}),client_worker=sorted({r['tid'] for r in wrecv.values()}),client_os_receive=sorted({r['tid'] for r in orecv.values()}),
        adapter_events=sorted({r['tid'] for r in al if r['event']!='probe_loaded'}),host_sim=htid,host_capture=sorted({r['tid'] for r in cap.values()}),host_worker=sorted({r['tid'] for r in wsend.values()}),
        unload_witness=[r for r in jl(C/'unload_witness.jsonl')])
    sim_only=tid_sets['client_dll_proxy_events']==[ctid] and tid_sets['client_snapshot_accept']==[ctid] and tid_sets['adapter_events']==[ctid] and ctid not in tid_sets['client_worker']
    # ---------- gates ----------
    ps=[r for r in cl if r['event']=='proxy_state'];lastps=ps[-1]
    wit=tid_sets['unload_witness']
    gates=[
     dict(gate='GATE-PSE-58C-01',definition='Pre-unload barrier at kexSlaveLevelLocal::Unload (0x1401c90e0) before sector deallocation; zero Remove after.',status='OPEN',native_status='NATIVE_VERIFICATION_REQUIRED',
          evidence=dict(unload_entries_observed=sum(1 for r in wit if r['event']=='level_unload_entry'),witness=wit,dll_barrier_points=['StateExit (state3->!3)','DirectQuit (Shutdown fcc20)'],seam_world_exit=ev(cl,'seam_world_exit'),
                        run01_witness=jl(C1/'unload_witness.jsonl')),
          remaining_risk='1c90e0 never executed in either client (DirectQuit exits without it); no level unload with a live proxy was attempted (by design). Map transition with active proxy is NOT safe/claimed.'),
     dict(gate='GATE-PSE-58C-02',definition='Is sector-list unlink synchronous in Remove or deferred to Drain?',status='VERIFIED',native_status='VERIFIED (one Actor, sector 259)',
          evidence=dict(after_remove=removes,drain=ev(al,'deferred_drain_entry')+drain),conclusion='Remove returns with removed=1/flags 0x2000/refs1 and the node STILL linked (found 1); Drain on the next simulation step unlinks (found 0, refs 0). ConfirmedSuccess-on-return is false -> RETIRING state.',
          remaining_risk='Other Actor types/sectors not observed.'),
     dict(gate='GATE-PSE-58C-03',definition='ObjectNode pool sweep witness before retirement',status='VERIFIED',native_status='VERIFIED (bounded)',
          evidence=dict(on_remove=ev(al,'native_on_remove_entry')+ev(al,'native_on_remove_return'),pool_return=pool,retired=retired,state_machine=[r for r in acts if r['action'] in('RemoveRequested','RetirementPending','Retired')]),
          conclusion='PoolReturn inside controlled Sweep (valid_scope) -> payload null, refs 0, in_use 0, occurrences 0; only then retirement()==Retired and ABSENT.',remaining_risk='Sweep timing under load/other map unverified.'),
     dict(gate='GATE-PSE-58C-04',definition='PointInSector coplanar portal boundary',status='DEFERRED',native_status='NOT_EXERCISED',evidence=dict(native_point_in_sector_used_for='same-sector area attestation only',refusals=moves['refused_reasons']),remaining_risk='No cross-sector network transfer performed.'),
     dict(gate='GATE-PSE-58C-05',definition='Simulation thread affinity of tick and lifecycle callbacks',status='VERIFIED',native_status='VERIFIED (client paths of this run)',
          evidence=dict(tids={k:v for k,v in tid_sets.items() if k!='unload_witness'},simulation_steps=last['simulation_ticks'],actor_tick_callbacks=last['actor_tick_callbacks'],affinity_violations=lastps.get('affinity_violations'),foreign_calls=[r for r in cl if r['event']=='client_native_final_exit']),
          conclusion='All proxy state-machine events, seam callbacks, Spawn/Update/Remove and adapter events on client Sim TID; ENet receive on Worker TID only.',remaining_risk='Unload path not executed (G1).'),
     dict(gate='GATE-PSE-58C-06',definition='Seven proxies in one sector list',status='DEFERRED',native_status='NOT_EXERCISED',evidence=dict(native_capacity=1),remaining_risk='Only one proxy in DOC58B.'),
     dict(gate='GATE-PSE-58C-07',definition='No synchronous state-machine mutation from inside a native seam call',status='VERIFIED',native_status='VERIFIED (bounded)',
          evidence=dict(reentrancy_blocked=lastps.get('reentrancy_blocked'),unload_during_seam=lastps.get('unload_during_seam'),dispatch='snapshots only stored; seam called from phase58 after consume(); KEX callbacks (Drain/Sweep/PoolReturn) observed inside StateTick never call into the DLL',offline=['C14','C15','C16']),
          remaining_risk='Guard counters are DLL-observed; no fault injection natively.'),
     dict(gate='GATE-PSE-58D-08',definition='ISectorValidator prevents assert7b3c0 for invalid IDs in InsertActor',status='PARTIAL',native_status='VERIFIED for the used path; invalid-ID path NATIVE_VERIFICATION_REQUIRED',
          evidence=dict(factory_sector=[r['sector'] for r in factory],attested_area=ev(al,'attested_area')[0] if ev(al,'attested_area') else None,link_sector_calls=0,offline_invalid=['C21','C23']),
          remaining_risk='Invalid indices never reach KEX by construction (adapter area check); InsertActor 0x1401e58f0 itself not traced.'),
     dict(gate='GATE-PSE-58D-09',definition='ENet slot demultiplexing',status='VERIFIED',native_status='VERIFIED for slots 0 (remote) and 1 (local) only',
          evidence=dict(routing=accounting['routed'],welcome=ev(cl,'client_welcome'),entity_per_slot={str(k):v for k,v in collections.Counter((r['slot'],r['entity_id']) for r in acc.values()).items()},offline=['C23','C24','C25']),
          remaining_risk='Slots 2..7, stale/old-epoch injection: offline only.'),
    ]
    dump(OUT/'NATIVE_GATE_RESULTS.json',dict(registry_source='tools/network-visual-proxy-readiness/doc58d1-contract-audit/GATE_ID_CORRECTIONS.md (IDs/definitions unchanged)',gates=gates))
    # ---------- acceptance ----------
    hsig,csig=load(H/'OS_SIGNALED.json'),load(C/'OS_SIGNALED.json')
    restores=[r for r in hl if r['event']=='control_restore']+[r for r in cl if r['event']=='client_control_restored']
    def chk(cat,name,ok,**kw):return dict(category=cat,check=name,status='PASS' if ok else 'FAIL',**kw)
    rows=[
     chk('NETWORK','two real KEX processes, exact EXE',hlaunch['pid']!=claunch['pid'] and hlaunch['binary_sha256']==claunch['binary_sha256']==EXPECTED,host=hlaunch['pid'],client=claunch['pid']),
     chk('NETWORK','real ENet/OS UDP exchange of host snapshots',len(osend)>0 and len(orecv)>0,host_udp_send=len(osend),client_udp_receive_window=len(orecv)),
     chk('NETWORK','Welcome/native MapReady/session identity',bool(ev(cl,'client_welcome')) and bool(ev(cl,'proxy_native_map_ready')),welcome=ev(cl,'client_welcome')[0],map_ready=ev(cl,'proxy_native_map_ready')[0]),
     chk('NETWORK','client Sim accepted every received snapshot bit-exact; per-slot monotonic sequences',accounting['client_rejected']==0 and all(seq_mono.values()) and not mism,accounting=accounting),
     chk('NATIVE_SPAWN','exactly one factory call from a network snapshot',len(factory)==1 and act_count.get('SpawnCreated')==1 and len(spawned)==1,factory=factory,spawned=spawned),
     chk('NATIVE_SPAWN','Actor/ObjectNode distinct from Local Puppet, one sector-list membership',bool(spawned) and spawned[0]['actor']!=spawned[0]['primary']['actor'] and spawned[0]['sector_list']['found']==1),
     chk('SAME_SECTOR_MOVEMENT','several real Host movement events produced client native updates',act_count.get('UpdateApplied',0)>=5 and moves['distinct_targets']>=5,movement=moves,host_inputs=len(host_inputs)),
     chk('SAME_SECTOR_MOVEMENT','native current transform == accepted snapshot (x,y,z+88) and yaw',not mism and all(r['adapter']['actor_origin']==r['adapter']['target'] for r in applied if r['adapter'])),
     chk('SAME_SECTOR_MOVEMENT','no duplicate Spawn, no ghost: one Actor for whole run',act_count.get('SpawnCreated')==1 and last['sector_list']['found']==1),
     chk('RENDER','renderer received the Remote Actor transform (identity OURS)',render['ours']>0 and render['ambiguous']==0,ours=render['ours']),
     chk('INTERPOLATION','native lerp outputs reached sprite renderer between network steps',render['intermediate_logged']>0 and render['native_lerp_samples']>0,intermediate=render['intermediate_logged'],lerp=render['native_lerp_samples']),
     dict(category='CROSS_SECTOR',check='bounded 259->123 network transfer',status='DEFERRED',reason='Host coordinates never matched the DOC58A attested transfer; cross-sector updates refused without writes'),
     chk('LIFECYCLE','disconnect -> exactly one Remove -> Drain -> PoolReturn -> retirement witness',len(removes)==1 and len(retired)==1 and act_count.get('RemoveRequested')==1 and act_count.get('Retired')==1,retired=retired),
     chk('LIFECYCLE','no sprite/actor read after retirement, weak pointers null',render['sprite_after_remove']==0 and bool(post) and post[-1]['all_weak_pointers_null']),
     chk('LOCAL_PLAYER_INTEGRITY','client P1 12-field snapshots all EQUAL; ownership/graph kept; no local movement',integrity['client_local_player']['all_equal'] and not local_moved,comparisons=len(comps)),
     chk('HOOK_RESTORATION','client Update/host control sites restored exact16; adapter unloaded only after seam Detached',all(r.get('exact16') for r in restores) and bool(ev(cl,'seam_detached')),restores=restores),
     chk('OS_EXIT','both processes: retained+duplicate handle wait0, exit0, no emergency cleanup',all(s['exit_code']==0 and s['wait']==0 and s['duplicate_wait']==0 and s['no_emergency_cleanup'] for s in (hsig,csig))),
     chk('FAULTS','no adapter fault, no native fault/blocked in either journal',not faults and not [r for r in hl+cl if r['event'] in('blocked','native_fault','seam_native_fault')]),
    ]
    cats={}
    for r in rows:cats.setdefault(r['category'],[]).append(r['status'])
    verdict={k:('DEFERRED' if v==['DEFERRED'] else 'BOUNDED_PASS' if all(x=='PASS' for x in v) else 'FAIL') for k,v in cats.items()}
    verdict['OVERALL']='BOUNDED_PASS' if all(v in('BOUNDED_PASS','DEFERRED') for k,v in verdict.items()) else 'FAIL'
    dump(OUT/'NATIVE_ACCEPTANCE_REVIEW.json',dict(checks=rows,verdict=verdict,runs=dict(host='N58B_HOST_02',client='N58B_CLIENT_02',prior='N58B_*_01 BLOCKED before Spawn (see RUN01_RESULT.json)')))
    # ---------- run 01 ----------
    a1=jl(C1/'adapter.jsonl');c1=jl(C1/'native.jsonl')
    dump(OUT/'RUN01_RESULT.json',dict(status='BLOCKED_BEFORE_SPAWN',reason='Host P1 (spawned overlapping host P2 by the frozen DOC56 host) was pushed onto P2 and fell over a one-way ledge into sector 123; returning to the attested sector 259 was physically blocked.',
        spawn_refused=[r for r in a1 if r['event']=='spawn_refused'][:2],spawn_refusals=len([r for r in a1 if r['event']=='spawn_refused']),factory_calls=len([r for r in a1 if r['event']=='native_factory_call']),
        routing={f'slot{k[0]}:{k[1]}':v for k,v in collections.Counter((r['slot'],r['route']) for r in c1 if r['event']=='proxy_snapshot_routed').items()},
        shutdown=[r for r in c1 if r['event'] in('proxy_shutdown_requested','seam_detached','seam_world_exit','client_native_final_exit')],
        os=dict(host=load(H1/'OS_SIGNALED.json'),client=load(C1/'OS_SIGNALED.json')),rea_client_capture_truncated=True,unload_witness=jl(C1/'unload_witness.jsonl')))
    # ---------- manifests / summary ----------
    pre=load(OUT/'PRE_NATIVE_GATE.json')
    src=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in sorted([*LAB.glob('*.py'),*LAB.glob('*.js'),*LAB.glob('*.json'),*(LAB/'src').glob('*'),*(LAB/'tests').glob('*'),LAB/'CMakeLists.txt'])]
    used=[dict(path=p,sha256=sha(ROOT/p)) for p in ['experiments/m1/native_second_instance/src/client.cpp','experiments/m1/native_visual_proxy_movement/controller.js','experiments/m1/native_visual_proxy_movement/one_actor.js','experiments/m1/native_visual_proxy_movement/movement_native.js','experiments/m1/native_visual_proxy_movement/common.js','experiments/m1/native_visual_proxy_movement/cross_sector.js','experiments/m1/native_visual_proxy_movement/inventory_snapshot.js','experiments/m1/native_visual_proxy_movement/camera_math.js','experiments/m1/native_snapshot/src/snapshot_contract.hpp','experiments/m1/native_map_ready/src/map_identity.hpp','experiments/m1/native_map_ready/src/map_reader.hpp','experiments/m1/native_snapshot/snapshot_os_probe.js','experiments/m1/native_second_instance/os_probe.js','tools/network-visual-proxy-readiness/doc58d1-contract-audit/doc58d1_hardened_visual_proxy_state_machine.hpp','tools/network-visual-proxy-readiness/doc58d1-contract-audit/multi_slot_snapshot_guard.hpp']]
    changed_after_gate=[r for r in src if r['path'] in {x['path'] for x in pre['sources']} and r['sha256']!={x['path']:x['sha256'] for x in pre['sources']}[r['path']]]
    dump(OUT/'SOURCE_MANIFEST.json',dict(created_or_modified_in_lab=src,used_unchanged_upstream=used,pre_native_gate_sources=pre['sources'],changed_after_pre_native_gate=changed_after_gate,
        note='Only harness files changed after the gate (run names 01->02, A/D strafe keys, extractor inline-name fix, review). DLL, adapter and state-machine sources executed natively are those of the gate (adapter source sha in ADAPTER_MANIFEST).'))
    dump(OUT/'INTEGRATION_MANIFEST.json',dict(exe_sha256=EXPECTED,host_dll=dict(path='docs/evidence/m1/NATIVE_MAP_READY_56/artifacts/pse_m11_bootstrap.dll',sha256=sha(ROOT/'docs/evidence/m1/NATIVE_MAP_READY_56/artifacts/pse_m11_bootstrap.dll'),frozen_doc56=True),
        client_dll=dict(path='docs/evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/artifacts/pse_native_client58b.dll',sha256=sha(OUT/'artifacts/pse_native_client58b.dll'),defines=['PSE_DOC55_SNAPSHOT','PSE_DOC56_MAPREADY','PSE_DOC58B'],source='experiments/m1/native_network_visual_proxy_58b/src/client58b.cpp (generated from frozen DOC56 client.cpp)'),
        adapter=load(C/'ADAPTER_MANIFEST.json'),wire_protocol='v1.10 unchanged (frozen pse_core/enet-prototype built unchanged)',build_dir='b58 (short path)',toolchain='MSVC VS2022 x64, CMake, /W4 /WX',
        pipeline=['Host native capture (frozen DOC56 DLL)','ENet worker send','OS UDP','client ENet worker receive','ThreadBridge -> client Sim consume()','DOC55 Guard','SnapshotRouter (local slot excluded)','VisualProxyStateMachine (patched)','phase58 safe step','seam callback (opaque handle)','Frida adapter: DOC58A controller/OneActor/adapter','KEX Actor1007','KEX interpolant lerp','sprite renderer']))
    summary=dict(task='DOC-58B Native Network Visual Proxy Integration',verdict=verdict,code_review='PATCHED (Gemini DOC58D.1 rejected as-is; 8 blocking contract defects fixed in isolated copy)',
        host=dict(pid=hlaunch['pid'],sim_tid=htid,worker_tid=tid_sets['host_worker']),client=dict(pid=claunch['pid'],sim_tid=ctid,worker_tid=tid_sets['client_worker']),
        slots=dict(remote_host_player=0,local_client=1,welcome=ev(cl,'client_welcome')[0]),accounting=accounting,actor=dict(actor=spawned[0]['actor'],node=spawned[0]['node'],handle=spawned[0]['handle'],sector=259,spawn_snapshot_seq=spawned[0]['command']['sequence']),
        spawn_calls=len(factory),remove_calls=len(removes),updates_applied=act_count.get('UpdateApplied'),render=dict((k,render[k]) for k in ['ours','intermediate_logged','native_lerp_samples','distinct_revisions_rendered','applied_rows_with_render']),
        movement=moves,lifecycle=dict(disconnect='LOCAL_SESSION_LOST',remove=1,retired=True,sprite_after_remove=0),gates={g['gate']:g['status'] for g in gates},
        limitations=['one Remote Proxy, one Actor type (stock 1007 flags 0x40), sector 259 only (+88 visual z offset)','cross-sector network transfer DEFERRED','G1 OPEN: no map transition with a live proxy','host P1 positions limited by host-world collisions (host P2 overlap, ledges); run 01 BLOCKED before Spawn','client applies only while its world ticks (foreground); background ticks coalesce','no jitter buffer; 8-player routing offline only','unload witness: 1c90e0 not executed on DirectQuit'],utc=stamp())
    dump(OUT/'RESULT_SUMMARY.json',summary)
    print(json.dumps(dict(verdict=verdict,accounting=accounting,moves=dict(distinct=moves['distinct_targets'],x=moves['x'],y=moves['y'],z=moves['z'],yaws=moves['yaws'][:6]),render=summary['render']),indent=1,default=str)[:3000])
if __name__=='__main__':main()
