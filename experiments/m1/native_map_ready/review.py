"""DOC56 D1 review: native identity -> epoch -> Welcome -> MapReady -> Host acceptance ->
snapshot eligibility -> UDP -> Client Simulation, plus the DOC55 transport/ownership/exit checks.
Reads only journals, process-observer files and REA records extracted from exported bundles."""
import collections,json,sys
from workflow import ROOT,OUT,dump
E=ROOT/'docs/evidence/m1'
H,C='N56_HOST_01','N56_CLIENT_01'
def rows(name):return [json.loads(line) for line in (E/name/'native.jsonl').read_text(encoding='utf8').splitlines()]
def payloads(name,tag='STATUS_EXIT'):
    o=json.loads((E/name/(tag+'.json')).read_text(encoding='utf8'))['structuredContent']['normalized_result']
    assert not o['messages_truncated'] and not any(r['type']=='error' for r in o['messages'])
    return [m['payload'] for m in o['messages'] if isinstance(m.get('payload'),dict)]
def order(r):return r.get('seq',r.get('record'))
def key(r):return r['slot'],r['snapshot_seq'],r['server_tick']
def one(values,event,**match):
    found=[r for r in values if r['event']==event and all(r.get(k)==v for k,v in match.items())]
    return found[0] if len(found)==1 else None
def main():
    h,c=rows(H),rows(C);hp,cp=payloads(H),payloads(C);checks=[]
    def check(name,passed,detail=None):checks.append(dict(name=name,passed=bool(passed),detail=detail))
    hl=json.loads((E/H/'launch.json').read_text(encoding='utf8'));cl=json.loads((E/C/'launch.json').read_text(encoding='utf8'))
    hsim,csim=hl['primary_thread_id'],cl['primary_thread_id']
    # 1. executable hash and isolation
    gates={n:json.loads((E/n/'ISOLATION_GATE.json').read_text(encoding='utf8')) for n in [H,C]}
    loaders={n:[p for p in payloads(n) if p.get('kind')=='m11_dll_loader'] for n in [H,C]}
    check('exe_hash_and_isolation',all(l['binary_sha256']=='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d' for l in [hl,cl]) and
        all(g['status']=='PASS' for g in gates.values()) and all(len(v)==1 and v[0]['prepare_result']==0 for v in loaders.values()),
        dict(gates={n:g['guard'] for n,g in gates.items()},dll={n:v[0]['dll_sha256_host_verified'] for n,v in loaders.items()},separate_profiles=[hl['profile'],cl['profile']]))
    # 2. same native map, from the DLL readers (simulation thread) and the independent read-only witnesses
    ho=one(h,'map_identity_observed');co=one(c,'client_map_identity_observed')
    witness={n:[p for p in payloads(n) if p.get('kind')=='n56_map_witness'] for n in [H,C]}
    def same(a,b):return all(a[k]==b[k] for k in ['map','name_length','name_capacity','psm_version','count_a','count_b','mapinfo_key'])
    wok=all(w['kexstr']['text']=='maps/TOMB.MAP' and w['kexstr']['terminator_at_length'] and w['level']['vtable_expected'] and w['level']['slot1_rva']=='0x1c7b70' and w['level']['slot2_rva']=='0x1c90e0'
        for n in [H,C] for w in witness[n] if w['state']==3)
    menu=[w for w in witness[C] if w['state']!=3]
    check('native_map_identity_both_processes',ho and co and ho['fault']==co['fault']=='none' and ho['tid']==hsim and co['tid']==csim and same(ho,co) and ho['map']=='maps/TOMB.MAP' and wok,
        dict(host=ho,client=co,witness=witness))
    check('no_identity_before_gameplay',menu and all(w['kexstr']['length']==0 and w['mapinfo']['value']=='0x0' and w['level']['count_a']==0 for w in menu),menu)
    # 3-8. epoch, Welcome, stability, MapReady, acceptance
    bound=one(h,'map_identity_bound');welcome=one(c,'client_welcome');ready=one(c,'client_map_ready_queued');accepted=one(h,'network_ready')
    rejected=[r for r in h if r['event']=='map_ready_rejected']
    epoch=bound and bound['map_epoch']
    check('host_nonconstant_epoch',bound and bound['tid']==hsim and epoch not in (0,1) and bound['map']==ho['map'],bound)
    check('welcome_host_identity',welcome and welcome['epoch']==epoch and welcome['host_map']==welcome['local_map']==bound['map'] and welcome['generation']>=1 and welcome['tid']==csim,welcome)
    status=[r for r in c if r['event']=='client_map_status']
    first=next((r for r in status if r['state']=='await_native_map'),None);verified=next((r for r in status if r['state']=='identity_verified'),None)
    check('thirty_stable_ticks_then_mapready_same_tick',first and verified and ready and verified['stable_ticks']==30 and verified['tick']-first['tick']+first['stable_ticks']==30 and
        ready['tick']==verified['tick'] and order(ready)>order(welcome) and all(r['fault']=='none' for r in status if order(first)<=order(r)<=order(verified)),dict(first=first,verified=verified,ready=ready))
    check('client_mapready_identity',ready and ready['epoch']==epoch and ready['generation']==welcome['generation'] and ready['slot']==welcome['slot'] and ready['map']==bound['map'],ready)
    check('host_accepts_current_slot_generation_epoch_once',accepted and accepted['epoch']==epoch and accepted['generation']==ready['generation'] and accepted['slot']==ready['slot'] and
        accepted['map_ready_accepted'] and not rejected and len([r for r in h if r['event']=='network_ready'])==1 and accepted['tid']==hsim,dict(accepted=accepted,rejected=rejected))
    # 9. eligibility strictly after acceptance; no capture before it
    elig=[r for r in h if r['event']=='snapshot_eligibility'];cap=[r for r in h if r['event']=='native_snapshot_captured']
    check('snapshot_eligibility_only_after_acceptance',elig and elig[0]['eligible'] and order(elig[0])>order(accepted) and cap and order(cap[0])>order(elig[0]) and all(r['map_epoch']==epoch for r in cap),
        dict(eligibility=elig,first_capture=cap[:1]))
    # 10-11. real snapshots through both socket buffers
    received=[r for r in c if r['event']=='native_snapshot_received'];rejectedSnap=[r for r in c if r['event']=='native_snapshot_rejected']
    hs=[r for r in h if r['event']=='snapshot_worker_send'];cr=[r for r in c if r['event']=='snapshot_worker_receive']
    us=[r for r in hp if r.get('kind')=='n55_udp_snapshot_send'];ur=[r for r in cp if r.get('kind')=='n55_udp_snapshot_receive']
    hw={r['tid'] for r in hs};cw={r['tid'] for r in cr}
    check('worker_threads_distinct_from_simulation',len(hw)==1 and len(cw)==1 and hsim not in hw and csim not in cw,dict(host_worker=sorted(hw),client_worker=sorted(cw)))
    check('socket_observation_on_workers',us and ur and {r['tid'] for r in us}==hw and {r['tid'] for r in ur}==cw)
    for role,values in [('host',us),('client',ur)]:
        check(role+'_actual_udp_opcode20',values and all(r['result']==0 and not r['overlapped'] and r['opcode']==32 and r['wire_bytes']==89 and r['count']==1 and r['header_sequence']==r['snapshot_seq'] and r['map_epoch']==epoch for r in values),len(values))
    indexes=[{key(r):r for r in v} for v in [cap,hs,us,ur,cr]];failures=[];correlation=[]
    for r in received:
        chain=[i.get(key(r)) for i in indexes]
        if any(x is None for x in chain):failures.append(dict(key=key(r),missing=[n for n,x in zip(['host_capture','host_worker','udp_send','udp_receive','client_worker'],chain) if x is None]));continue
        source,worker,send,recv,inbound=chain
        equal=all(x[k]==r[k] for x in chain for k in ['map_epoch','entity_id','entity_generation']) and source['connection_generation']==r['connection_generation']==worker['connection_generation']==inbound['connection_generation']
        equal=equal and source['bits']==send['bits']==recv['bits']==r['bits'] and source['queued'] and r['player_writes']==0
        if not equal:failures.append(dict(key=key(r),reason='numeric_or_identity_mismatch'))
        correlation.append(dict(slot=r['slot'],sequence=r['snapshot_seq'],tick=r['server_tick'],epoch=r['map_epoch'],connection_generation=r['connection_generation'],bits=r['bits'],
            host_capture=order(source),host_worker=order(worker),udp_send=send['order'],udp_receive=recv['order'],client_worker=order(inbound),client_sim=order(r),match=equal))
    check('both_slots_received',received and {r['slot'] for r in received}=={0,1},dict(received=collections.Counter(r['slot'] for r in received),captured=collections.Counter(r['slot'] for r in cap),client_rejected=len(rejectedSnap)))
    check('every_client_sample_matches_entire_chain',received and not failures,dict(matched=len(correlation),failures=failures[:20]))
    for slot in [0,1]:
        view=[r for r in received if r['slot']==slot]
        check('slot%d_monotonic'%slot,view and all(a['snapshot_seq']<b['snapshot_seq'] and a['server_tick']<=b['server_tick'] for a,b in zip(view,view[1:])),len(view))
    check('client_snapshots_after_own_baseline',received and order(received[0])>order(one(c,'client_input_domain_ready')))
    # 12-13. W/S to P2 and P1 ownership
    updates=[r for r in h if r['event']=='network_native_update'];motion=[r for r in h if r['event']=='network_native_motion']
    check('client_ws_drives_native_p2',{4,8}.issubset({r['native_buttons'] for r in updates}) and len({tuple(r['after']) for r in motion})>5,dict(buttons=collections.Counter(r['native_buttons'] for r in updates),positions=len({tuple(r['after']) for r in motion})))
    check('native_p2_unique_updates',updates and len(updates)==len({r['tick'] for r in updates}),len(updates))
    check('p1_preserved_single_native_commands',updates and all(r['unchanged'] and r['p1_before']==r['p1_after'] and r['commands_this_update']==1 for r in updates))
    inputs={(r['slot'],r['generation'],r['input_seq']) for r in cp if r.get('kind')=='n51_udp_input_send_return' and r['result']==0}
    acc=[r for r in h if r['event']=='network_input_accepted']
    check('accepted_inputs_have_actual_udp_provenance',acc and all((r['slot'],r['generation'],r['input_seq']) in inputs for r in acc),dict(accepted=len(acc),udp_sent=len(inputs)))
    firstInput=min((r['order'] for r in cp if r.get('kind')=='n51_udp_input_send_return'),default=None)
    check('input_cadence_starts_only_after_mapready',firstInput is not None and accepted and acc and order(acc[0])>order(accepted),dict(first_client_udp_input_order=firstInput,first_host_input_record=order(acc[0]) if acc else None))
    # 14-15. faults, affinity, shutdown
    for role,values,osvalues,name,sim in [('host',h,hp,H,hsim),('client',c,cp,C,csim)]:
        check(role+'_native_faults_zero',not [r for r in values if r['event'] in ['blocked','native_fault','hook_error','hook_create_error']])
        check(role+'_map_events_on_simulation_thread',all(r['tid']==sim for r in values if r['event'].startswith(('map_','client_map','network_ready','snapshot_eligibility'))))
        joined=next(r for r in values if r['event']==('network_joined' if role=='host' else 'client_worker_joined'))
        check(role+'_spsc_affinity_zero',joined['affinity_violations']==0,joined)
        wtid=list(hw if role=='host' else cw)[0]
        waits=[r for r in osvalues if r.get('kind')=='n51_thread_wait_return' and r.get('target_tid')==wtid];closes=[r for r in osvalues if r.get('kind')=='n51_socket_close_return' and r['tid']==wtid]
        check(role+'_worker_wait_and_socket_close',waits and closes and all(r['result']==0 and r['exit_code']==0 for r in waits) and all(r['result']==0 for r in closes),dict(waits=waits,closes=closes))
        restored=[r for r in values if r['event'] in ['control_restore','client_control_restored']]
        check(role+'_join_precedes_native_restores',len(restored)==(4 if role=='host' else 1) and all(order(r)>order(joined) and r.get('exact16') for r in restored))
        os=json.loads((E/name/'OS_SIGNALED.json').read_text(encoding='utf8'));ex=json.loads((E/name/'exit.json').read_text(encoding='utf8'))
        check(role+'_normal_process_exit',os['wait']==0 and os['exit_code']==0 and os['duplicate_wait']==0 and os['no_emergency_cleanup'] and ex['exit_code']==0,dict(os=os,launcher=ex))
    resets=[r for r in h if r['event']=='native_call_return' and r.get('operation')=='exit_reset']
    check('once_only_native_reset_preserves_p1',len(resets)==1 and resets[0]['handle_null'] and resets[0]['p1_unchanged'] and resets[0]['guards'])
    with (OUT/'D1_CORRELATION.jsonl').open('x',encoding='utf8') as f:
        for r in correlation:f.write(json.dumps(r)+'\n')
    chain=dict(native_identity=dict(host=ho and {k:ho[k] for k in ['seq','utc','map','count_a','count_b']},client=co and {k:co[k] for k in ['record','utc','map','count_a','count_b']}),
        epoch=bound,welcome=welcome,stable=dict(first=first,verified=verified),map_ready=ready,host_acceptance=accepted,eligibility=elig[:1],
        first_capture=cap[:1],first_udp_send=us[:1],first_udp_receive=ur[:1],first_client_sim=received[:1])
    result=dict(verdict='PASS' if all(r['passed'] for r in checks) else 'PARTIAL',scope='One Host + one Client, Tomb of Ramses, trusted-client v1.10 MapReady; no world sync',
        pids=dict(host=hl['pid'],client=cl['pid']),simulation_tids=dict(host=hsim,client=csim),worker_tids=dict(host=sorted(hw),client=sorted(cw)),
        counts=dict(captured=len(cap),host_worker_sent=len(hs),udp_send=len(us),udp_receive=len(ur),client_worker=len(cr),client_accepted=len(received),client_rejected=len(rejectedSnap),p2_updates=len(updates)),
        chain=chain,checks=checks,
        limitations=['MsgMapReady bytes are not decoded by the accepted socket probes; its transport is shown by Host-side acceptance from the authenticated peer and input cadence gating, not by a UDP buffer capture',
                     'Host does not verify the client map (v1.10 epoch echo)','D2/transition/reconnect not covered'])
    dump(OUT/'D1_RESULT_SUMMARY.json',result)
    print(json.dumps(dict(verdict=result['verdict'],counts=result['counts'],failed=[r['name'] for r in checks if not r['passed']]),indent=1))
if __name__=='__main__':main()
