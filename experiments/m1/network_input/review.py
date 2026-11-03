"""Offline consistency audit of real native journals, client sends and OS evidence.
Does not launch anything; counts are measurements, not additional gameplay tests.
"""
from pathlib import Path
import hashlib, json, math, itertools
LAB=Path(__file__).resolve().parent
ROOT=LAB.parents[2]
OUT=ROOT/'docs/evidence/m1'
def load(p): return json.loads(p.read_text(encoding='utf-8-sig'))
def lines(p): return [json.loads(s) for s in p.read_text(encoding='utf-8-sig').splitlines()]
def distance(a,b): return math.hypot(a[0]-b[0],a[1]-b[1])
def main(name,direct):
    run=OUT/name; native=lines(run/'native.jsonl'); client=lines(run/'client.jsonl'); launch=load(run/'launch.json')
    expected=launch['primary_thread_id']
    checks={}
    def check(k,c): checks[k]=bool(c)
    def events(k): return [r for r in native if r['event']==k]
    accepted=events('network_input_accepted'); updates=events('network_native_update'); motion=events('network_native_motion')
    sent={(r['generation'],r['input_seq']):r for r in client if r['event']=='sent'}
    accepted_keys={(r['generation'],r['input_seq']):r for r in accepted}
    check('native_journal_contiguous', [r['seq'] for r in native]==list(range(1,len(native)+1)))
    check('no_native_faults',not [r for r in native if r['event'] in ['blocked','native_fault','hook_error','hook_create_error']])
    check('all_runtime_records_sim_tid',all(r['tid']==expected for r in native if r['event'] not in ['dll_prepared','bootstrap_queued']))
    check('host_commands_match_client_sends',bool(accepted) and all((r['generation'],r['input_seq']) in sent and r['forward']==sent[(r['generation'],r['input_seq'])]['forward'] and r['slot']==sent[(r['generation'],r['input_seq'])]['slot']==1 for r in accepted))
    check('monotonic_per_generation',all(all(b['input_seq']>a['input_seq'] for a,b in zip(v,v[1:])) for _,group in itertools.groupby(accepted,key=lambda x:x['generation']) for v in [list(group)]))
    check('native_updates_link_to_accepted_inputs',all(r['input_seq']==0 and r['native_buttons']==0 or (r['generation'],r['input_seq']) in accepted_keys and r['accepted_tick']==accepted_keys[(r['generation'],r['input_seq'])]['tick']<=r['tick'] for r in updates))
    check('one_update_and_command_per_tick',len({r['tick'] for r in updates})==len(updates) and all(r['commands_this_update']==1 for r in updates))
    by_tick={r['tick']:r for r in motion}
    check('motion_after_native_traversal_matches_update',len(motion)==len(updates) and all(r['tick'] in by_tick and all(r[k]==by_tick[r['tick']][k] for k in ['slot','generation','input_seq','native_buttons','p2_actor']) and by_tick[r['tick']]['updates']==by_tick[r['tick']]['puppet_ticks'] for r in updates))
    check('native_positions_finite_and_guards',bool(motion) and all(r['guards'] and all(math.isfinite(x) for x in r['before']+r['after']) for r in motion))
    check('p1_full_fields_preserved_every_update',bool(updates) and all(r['unchanged'] and r['p1_before']==r['p1_after'] for r in updates))
    check('separate_native_player_and_actor',all(r['p2_player']!=r['p1_before']['owner'] and r['p2_actor']!=r['p1_before']['actor'] for r in updates))
    spawn=events('independent_spawn'); check('one_spawn_only',len(spawn)==1 and len([r for r in events('native_call_attempt') if r['operation']=='spawn'])==1)
    check('actor_identity_stable_across_connection_generations',len({r['p2_actor'] for r in updates})==1)
    segments=[]
    for (gen,buttons),group in itertools.groupby(motion,key=lambda x:(x['generation'],x['native_buttons'])):
        group=list(group)
        segments.append(dict(generation=gen,buttons=buttons,first_tick=group[0]['tick'],last_tick=group[-1]['tick'],count=len(group),first_input=group[0]['input_seq'],last_input=group[-1]['input_seq'],start=group[0]['before'],end=group[-1]['after'],xy_displacement=distance(group[0]['before'],group[-1]['after']),xy_path=sum(distance(r['before'],r['after']) for r in group),first_max_xy_step=max(distance(r['before'],r['after']) for r in group[:10]),tail_max_xy_step=max(distance(r['before'],r['after']) for r in group[-10:]),last_xy_step=distance(group[-1]['before'],group[-1]['after'])))
    holds=[s for s in segments if s['buttons']==4]
    check('hold_causes_measured_xy_motion',bool(holds) and any(s['xy_displacement']>10 for s in holds))
    timeouts=[r for r in events('network_neutral') if r['reason']=='input_timeout']
    if not direct:
        check('p1_host_W_received_during_P2_updates',any(r['p1_before']['input'][0]&4 for r in updates))
        # A stationary initial neutral block is NOT evidence of release. Require
        # an actual received zero command immediately after a native W block,
        # then measure decay of native coasting in that exact interval. KEX
        # inertia/collision is left intact; never promise an instant position stop.
        releases=[s for prev,s in zip(segments,segments[1:]) if prev['buttons']==4 and s['buttons']==0 and prev['generation']==s['generation'] and accepted_keys.get((s['generation'],s['first_input']),{}).get('forward')==0]
        check('release_removes_drive_and_native_motion_decelerates',any(s['count']>=30 and s['first_max_xy_step']>s['tail_max_xy_step']*10 and s['last_xy_step']<.05 for s in releases))
        check('eventual_post_input_rest',any(s['buttons']==0 and s['first_tick']>holds[-1]['last_tick'] and s['count']>=60 and s['tail_max_xy_step']<.001 for s in segments))
        check('explicit_release_received',any(a['forward']>0 and b['forward']==0 for a,b in zip(accepted,accepted[1:])))
        check('timeout_after_six_ticks',bool(timeouts) and all(r['tick']-accepted_keys[(r['generation'],r['input_seq'])]['tick']==6 and next(m for m in motion if m['tick']==r['tick'])['native_buttons']==0 for r in timeouts))
        disconnect=[r for r in events('network_neutral') if r['reason']=='disconnect' and r['previous_buttons']==4]
        check('disconnect_clears_live_hold',bool(disconnect) and all(next(m for m in motion if m['tick']==r['tick'])['native_buttons']==0 for r in disconnect))
        check('reconnect_advances_generation_without_spawn',any(r['generation']==2 for r in accepted) and len(spawn)==1)
        check('owned_actor_destroyed_at_StateExit',len(events('owned_actor_dtor_return'))==1 and len(events('state_exit_return'))==1)
    else:
        check('direct_quit_live_player_without_StateExit',not events('state_exit_entry') and not events('owned_actor_dtor_entry') and events('shutdown_entry')[0]['live_external_handle'])
    joined=events('network_joined'); restore=events('control_restore'); reset=[r for r in events('native_call_return') if r.get('operation')=='exit_reset']; final=events('native_final_exit')
    check('join_before_native_reset_and_engine_cleanup',len(joined)==len(reset)==len(final)==1 and joined[0]['seq']<reset[0]['seq']<events('engine_shutdown_entry')[0]['seq']<final[0]['seq'])
    check('terminal_queues_retired_without_loss',joined and not joined[0]['worker_running'] and not joined[0]['lost_on_stop'] and not joined[0]['retired_outbound'] and not joined[0]['affinity_violations'] and all(r['inbound']==r['outbound']==0 for r in events('network_retired')))
    check('exact_controls_and_native_unbind',len(restore)>=4 and all(r['exact16'] for r in restore) and reset and reset[0]['handle_null'] and reset[0]['p1_unchanged'] and reset[0]['guards'])
    check('native_final_quiescent',final and final[0]['external_handle_null'] and final[0]['guards'] and not final[0]['ticks_after_shutdown'] and not final[0]['foreign_calls'] and not final[0]['failed'])
    check('exit0_and_protected_saves_unchanged',load(run/'exit.json')['exit_code']==0 and load(run/'protected_before.json')==load(run/'protected_after.json'))
    status=load(run/'status_final.json')['structuredContent']['normalized_result']
    check('provider_complete_without_errors',status['state']=='detached' and not status['messages_truncated'] and not [m for m in status['messages'] if m['type']=='error'])
    payloads=[m['payload'] for m in status['messages'] if isinstance(m.get('payload'),dict)]
    bound=[p for p in payloads if p.get('kind')=='m12a_socket_bind' and p['port']==28745 and p['result']==0]
    sockets={p['socket'] for p in bound}
    io=[p for p in payloads if p.get('kind')=='m12a_socket_io' and p['socket'] in sockets]
    workers={p['tid'] for p in io}
    closed=[p for p in payloads if p.get('kind')=='m12a_socket_close_return' and p['socket'] in sockets and p['result']==0 and p['tid'] in workers]
    check('independent_socket_io_and_close_on_network_tid',len(workers)==1 and expected not in workers and bool(closed))
    waits=[p for p in payloads if p.get('kind')=='m12a_thread_wait_return' and p['target_tid'] in workers and p['tid']==expected and p['result']==0 and p['exit_code']==0]
    if direct: check('OS_thread_join_wait_succeeded',bool(waits))
    else:
        os_state=load(run/'os_after_state_exit.json')
        check('OS_worker_tid_and_endpoint_absent_after_StateExit',not os_state['network_tid_exists'] and not os_state['endpoints'] and os_state['network_tid'] in workers)
    check('isolation_gate_pass',load(run/'ISOLATION_GATE.json')['status']=='PASS')
    guards=[p for p in payloads if p.get('kind')=='isolation_write_guard_summary']
    check('no_final_guard_errors_or_overflow',guards and all(not p['errors'] and not p['unique_overflow'] for p in guards))
    cfg=load(run/'NATIVE_ARTIFACT.json')['config']; dll=Path(cfg['dll'])
    check('frozen_dll_exact_sha',hashlib.sha256(dll.read_bytes()).hexdigest()==cfg['dll_sha256'])
    summary=dict(status='PASS' if all(checks.values()) else 'PARTIAL',scope='Bounded M1.2a; measurements from one native run, no world replication/capacity claim',pid=launch['pid'],simulation_tid=expected,network_tids=sorted(workers),dll_sha256=cfg['dll_sha256'],records=len(native),accepted_inputs=len(accepted),native_updates=len(updates),p1_nonzero_input_updates=sum(bool(r['p1_before']['input'][0]) for r in updates),checks=checks,segments=segments,timeouts=timeouts,spawn=spawn,join=joined,reset=reset,OS_join=waits,socket_close=closed,final=final)
    (run/'RESULT_SUMMARY.json').write_text(json.dumps(summary,indent=2),encoding='utf8')
    print(json.dumps(dict(run=name,status=summary['status'],failed=[k for k,v in checks.items() if not v],native_updates=len(updates),accepted_inputs=len(accepted),segments=segments),indent=2))
    return summary
if __name__=='__main__':
    reports=[main('M12A_NATIVE_01',False),main('M12A_DIRECT_QUIT_01',True)]
    raise SystemExit(any(r['status']!='PASS' for r in reports))
