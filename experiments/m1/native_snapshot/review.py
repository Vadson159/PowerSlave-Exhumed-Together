"""Correlate every accepted native snapshot through both socket buffers and roles."""
import collections,json,sys
from workflow import ROOT,OUT,dump,stamp
E=ROOT/'docs/evidence/m1'
def rows(name):return [json.loads(line) for line in (E/name/'native.jsonl').read_text(encoding='utf8').splitlines()]
def payloads(name):
    o=json.loads((E/name/'STATUS_EXIT.json').read_text(encoding='utf8'))['structuredContent']['normalized_result']
    assert not o['messages_truncated'] and not any(r['type']=='error' for r in o['messages'])
    return [m['payload'] for m in o['messages'] if isinstance(m.get('payload'),dict)]
def key(r):return r['slot'],r['snapshot_seq'],r['server_tick']
def main():
    suffix='_FINAL' if len(sys.argv)>1 and sys.argv[1]=='final' else ''
    h,c=rows('N55_HOST_01'),rows('N55_CLIENT_01');hp,cp=payloads('N55_HOST_01'),payloads('N55_CLIENT_01');checks=[]
    def check(name,passed,detail=None):checks.append(dict(name=name,passed=bool(passed),detail=detail))
    cap=[r for r in h if r['event']=='native_snapshot_captured'];received=[r for r in c if r['event']=='native_snapshot_received']
    hs=[r for r in h if r['event']=='snapshot_worker_send'];cr=[r for r in c if r['event']=='snapshot_worker_receive']
    us=[r for r in hp if r.get('kind')=='n55_udp_snapshot_send'];ur=[r for r in cp if r.get('kind')=='n55_udp_snapshot_receive']
    indexes=[{key(r):r for r in values} for values in [cap,hs,us,ur,cr]]
    check('both_native_slots',received and {r['slot'] for r in received}=={0,1},dict(received=collections.Counter(r['slot'] for r in received),captured=collections.Counter(r['slot'] for r in cap)))
    check('unique_capture_packet_sequences',len(cap)==len({r['snapshot_seq'] for r in cap}),len(cap))
    check('capture_post_world_unique_p2_tick',cap and all(r['post_world_tick'] and r['p2_updates']==r['p2_puppet_ticks'] for r in cap))
    check('native_simulation_affinity',all(r['tid']==37644 for r in cap) and all(r['tid']==30920 for r in received))
    check('worker_affinity',hs and cr and {r['tid'] for r in hs}=={1976} and {r['tid'] for r in cr}=={34552})
    check('socket_observation_affinity',us and ur and {r['tid'] for r in us}=={1976} and {r['tid'] for r in ur}=={34552})
    for role,values in [('host',us),('client',ur)]:
        check(role+'_actual_udp_opcode20',values and all(r['result']==0 and not r['overlapped'] and r['opcode']==32 and r['wire_bytes']==89 and r['count']==1 and r['header_sequence']==r['snapshot_seq'] for r in values),len(values))
    failures=[];correlation=[]
    metadata=['map_epoch','entity_id','entity_generation']
    for r in received:
        chain=[index.get(key(r)) for index in indexes]
        if any(x is None for x in chain):failures.append(dict(key=key(r),missing=[n for n,x in zip(['host_capture','host_worker','udp_send','udp_receive','client_worker'],chain) if x is None]));continue
        source,worker,send,recv,inbound=chain
        equal=all(x[k]==r[k] for x in chain for k in metadata)
        equal=equal and source['connection_generation']==r['connection_generation']==worker['connection_generation']==inbound['connection_generation']
        equal=equal and source['bits']==send['bits']==recv['bits']==r['bits'] and source['queued'] and r['player_writes']==0
        if not equal:failures.append(dict(key=key(r),reason='numeric_or_identity_mismatch'))
        correlation.append(dict(slot=r['slot'],sequence=r['snapshot_seq'],tick=r['server_tick'],epoch=r['map_epoch'],connection_generation=r['connection_generation'],entity_generation=r['entity_generation'],bits=r['bits'],host_capture_record=source['seq'],host_worker_record=worker['seq'],udp_send_order=send['order'],udp_receive_order=recv['order'],client_worker_record=inbound['record'],client_sim_record=r['record'],match=equal))
    check('every_client_sample_matches_entire_chain',received and not failures,dict(matched=len(correlation),failures=failures[:20]))
    path=OUT/('CORRELATION'+suffix+'.jsonl')
    with path.open('x',encoding='utf8') as f:
        for r in correlation:f.write(json.dumps(r)+'\n')
    for slot in [0,1]:
        view=[r for r in received if r['slot']==slot]
        check('slot'+str(slot)+'_monotonic',view and all(a['snapshot_seq']<b['snapshot_seq'] and a['server_tick']<=b['server_tick'] for a,b in zip(view,view[1:])),dict(count=len(view),first=view[:1],last=view[-1:]))
    epoch=next(r for r in h if r['event']=='snapshot_epoch_bound')
    ready=next(r for r in h if r['event']=='network_ready')
    welcome=next(r for r in c if r['event']=='client_welcome')
    localReady=next(r for r in c if r['event']=='client_input_ready_queued')
    check('authoritative_bounded_epoch_agreement',epoch['map_epoch']==epoch['tick']==ready['epoch']==welcome['epoch']==localReady['epoch'] and epoch['map_epoch']!=1 and all(r['map_epoch']==epoch['map_epoch'] for r in received),dict(epoch=epoch,host_ready=ready,client_welcome=welcome,client_ready=localReady))
    bindings=next(r for r in h if r['event']=='snapshot_entity_bindings')
    check('observed_native_entity_lifetimes',bindings['p1_generation']==1 and bindings['p2_generation']==1 and all(r['entity_generation']==1 and r['entity_id']==21760+r['slot'] for r in received),bindings)
    updates=[r for r in h if r['event']=='network_native_update'];motion=[r for r in h if r['event']=='network_native_motion']
    check('native_p2_unique_updates',updates and len(updates)==len({r['tick'] for r in updates}),len(updates))
    check('p1_preserved_and_single_native_commands',updates and all(r['unchanged'] and r['p1_before']==r['p1_after'] and r['commands_this_update']==1 for r in updates))
    background=[r for r in updates if r['focus_override']]
    check('background_host_and_neutral_p1',background and all(not any(r['p1_after']['input']) and r['p1_neutral'] for r in background),len(background))
    check('client_ws_input_and_native_p2_motion',{4,8}.issubset({r['native_buttons'] for r in updates}) and len({tuple(r['after']) for r in motion})>5,dict(distinct_positions=len({tuple(r['after']) for r in motion})))
    check('transported_p2_positions_vary',len({tuple(r['pos']) for r in received if r['slot']==1})>5)
    for role,values,osvalues in [('host',h,hp),('client',c,cp)]:
        bad=[r for r in values if r['event'] in ['blocked','native_fault','hook_error','hook_create_error']]
        check(role+'_native_faults_zero',not bad,bad)
        check(role+'_os_probe_errors_zero',not any(r.get('kind') in ['n55_probe_error','n51_udp_probe_error'] for r in osvalues))
        os=json.loads((E/('N55_'+role.upper()+'_01')/'OS_SIGNALED.json').read_text(encoding='utf8'))
        check(role+'_normal_process_exit',os['wait']==0 and os['exit_code']==0 and os['duplicate_wait']==0 and os['no_emergency_cleanup'],os)
        joined=next(r for r in values if r['event']==('network_joined' if role=='host' else 'client_worker_joined'))
        check(role+'_spsc_affinity_zero',joined['affinity_violations']==0,joined)
        workerTid=1976 if role=='host' else 34552
        waits=[r for r in osvalues if r.get('kind')=='n51_thread_wait_return' and r.get('target_tid')==workerTid]
        closes=[r for r in osvalues if r.get('kind')=='n51_socket_close_return' and r['tid']==workerTid]
        check(role+'_actual_worker_wait_and_socket_close',waits and closes and all(r['result']==0 and r['exit_code']==0 for r in waits) and all(r['result']==0 for r in closes),dict(waits=waits,closes=closes))
        restored=[r for r in values if r['event'] in ['control_restore','client_control_restored']]
        check(role+'_join_precedes_native_restores',len(restored)==(4 if role=='host' else 1) and all(r.get('seq',r.get('record'))>joined.get('seq',joined.get('record')) and r.get('exact16',False) for r in restored),restored)
    resets=[r for r in h if r['event']=='native_call_return' and r.get('operation')=='exit_reset']
    check('once_only_native_reset_preserves_p1',len(resets)==1 and resets[0]['handle_null'] and resets[0]['p1_unchanged'] and resets[0]['guards'])
    accepted=[r for r in h if r['event']=='network_input_accepted']
    inputs=[r for r in cp if r.get('kind')=='n51_udp_input_send_return' and r['result']==0]
    ids={(r['slot'],r['generation'],r['input_seq']):r for r in inputs}
    check('accepted_inputs_have_actual_udp_provenance',accepted and all((r['slot'],r['generation'],r['input_seq']) in ids for r in accepted),dict(accepted=len(accepted),udp_sent=len(inputs)))
    optional=json.loads(sorted(OUT.glob('INPUT_mouse_*.json'))[-1].read_text(encoding='utf8'))
    result=dict(verdict='PASS' if all(r['passed'] for r in checks) else 'PARTIAL',scope='One Host + one Client, fixed Tomb of Ramses, native transform transport and diagnostic log only',checks=checks,counts=dict(host_captured=len(cap),host_worker_sent=len(hs),udp_snapshot_send=len(us),udp_snapshot_receive=len(ur),client_worker_received=len(cr),client_sim_received=len(received),client_slots=dict(collections.Counter(r['slot'] for r in received)),matched=len(correlation),host_p2_updates=len(updates),background_p2_updates=len(background),accepted_inputs=len(accepted)),optional_orientation_fixture=dict(status=optional['status'],records=optional['records'],interpretation='Foreground request rejected before any relative mouse SendInput; C variation not claimed; A mouse acceptance remains historical'),epoch=epoch['map_epoch'],host=dict(pid=33340,simulation_tid=37644,worker_tid=1976),client=dict(pid=39156,simulation_tid=30920,worker_tid=34552))
    dump(OUT/('RESULT_SUMMARY'+suffix+'.json'),result)
    print(json.dumps(dict(verdict=result['verdict'],counts=result['counts'],failed=[r for r in checks if not r['passed']]),indent=2))
if __name__=='__main__':main()
