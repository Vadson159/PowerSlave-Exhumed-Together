"""Offline review of the one authorized DOC49 KEX run. No process actions."""
from pathlib import Path
import collections,datetime,hashlib,json,math
LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2];OUT=ROOT/'docs/evidence/m1/M12B_CADENCE_49';RUN=ROOT/'docs/evidence/m1/M12B_CADENCE_NATIVE_01'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def rows(p):return [json.loads(s) for s in p.read_text(encoding='utf8').splitlines()]
def stamp(utc):return datetime.datetime.fromisoformat(utc.replace('Z','+00:00')).timestamp()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    r=rows(RUN/'native.jsonl');c=rows(RUN/'client.jsonl');launch=read(RUN/'launch.json');artifact=read(RUN/'NATIVE_ARTIFACT.json')
    e=collections.defaultdict(list)
    for x in r:e[x['event']].append(x)
    full=read(RUN/'status_final.json')['structuredContent']['normalized_result'];payload=[m['payload'] for m in full['messages'] if isinstance(m.get('payload'),dict)]
    os=collections.defaultdict(list)
    for x in payload:os[x.get('kind')].append(x)
    updates=e['network_native_update'];motion=e['network_native_motion'];accepted=e['network_input_accepted'];sim=launch['primary_thread_id'];one=lambda k:e[k][0]
    first_hold=next(i for i,x in enumerate(accepted) if x['native_buttons']==4);release=next(x for x in accepted[first_hold+1:] if x['native_buttons']==0)
    last_hold=accepted[accepted.index(release)-1];expiry=one('network_input_expired')
    release_update=next(x for x in updates if x['tick']==release['tick'] and x['input_seq']==release['input_seq'])
    expiry_update=next(x for x in updates if x['tick']==expiry['tick'])
    sent={(x['generation'],x['input_seq']):x for x in c if x['event']=='sent'}
    spawn=one('independent_spawn');distance=max(math.dist(spawn['p2_pos'][:2],x['after'][:2]) for x in motion)
    resets=[x for x in e['native_call_return'] if x.get('operation')=='exit_reset']
    checks={}
    def check(k,v):checks[k]=bool(v)
    check('fresh_frozen_DLL',sha(Path(artifact['config']['dll']))==artifact['config']['dll_sha256']=='0ba9670f234ea3626e5c534422d18afefe572caf0fcf3cedc4323e706b4130db')
    check('one_run_one_spawn',len(e['independent_spawn'])==1 and len(list((ROOT/'docs/evidence/m1').glob('M12B_CADENCE_NATIVE_0*')))==1)
    check('native_hold_moves',sum(x['native_buttons']==4 for x in updates)>10 and distance>5)
    check('release_immediately_applied',release_update['native_buttons']==0 and release['tick']-last_hold['tick']<6)
    check('release_precedes_only_timeout',len(e['network_input_expired'])==1 and release['seq']<expiry['seq'])
    check('silence_exact_current_threshold',expiry['elapsed_ticks']==expiry['threshold_ticks']==6 and expiry['threshold_wall_ms']==150 and expiry_update['native_buttons']==0)
    check('actual_production_default',one('network_started')['timeout_ticks']==6 and one('network_started')['timeout_wall_ms']==150)
    check('P1_compared_fields_preserved',all(x['unchanged'] and x['p1_before']==x['p1_after'] for x in updates))
    check('one_update_commands_per_tick',len({x['tick'] for x in updates})==len(updates) and all(x['commands_this_update']==1 for x in updates))
    check('real_Puppet_tick_health_guards',len(updates)==len(motion)==motion[-1]['puppet_ticks'] and all(x['p1_health']==200 and x['p2_health']==200 and x['guards'] for x in motion))
    check('accepted_commands_match_actual_client',all((x['generation'],x['input_seq']) in sent and sent[(x['generation'],x['input_seq'])]['forward']==x['forward'] for x in accepted))
    check('KEX_journal_simulation_thread',all(x['tid']==sim for x in r if x['event'] not in ['dll_prepared','bootstrap_queued']))
    wait=os['wc_worker_wait_return'];close=os['wc_socket_close_return'];worker=os['wc_socket_io'][0]['tid']
    check('OS_worker_join_before_Reset',len(wait)==1 and wait[0]['target_tid']==worker!=sim and wait[0]['tid']==sim and wait[0]['result']==0 and wait[0]['exit_code']==0 and wait[0]['get_exit_ok'] and wait[0]['utc']<=resets[0]['utc'])
    check('socket_closed_once_on_worker',len(close)==1 and close[0]['tid']==worker and close[0]['result']==0 and close[0]['order']<wait[0]['order'])
    check('one_stop_join_retire',len(e['network_stop_begin'])==len(e['network_joined'])==len(e['network_retired'])==1 and not one('network_joined')['worker_running'] and one('network_joined')['affinity_violations']==0 and one('network_joined')['lost_on_stop']==0)
    check('Reset_then_engine_cleanup',len(resets)==1 and resets[0]['handle_null'] and resets[0]['p1_unchanged'] and resets[0]['guards'] and one('network_joined')['seq']<e['control_restore'][0]['seq']<resets[0]['seq']<one('engine_shutdown_entry')['seq'])
    check('four_exact_restores',len(e['control_restore'])==4 and all(x['exact16'] for x in e['control_restore']))
    final=one('native_final_exit');check('lifetime_final_safe',final['external_handle_null'] and final['guards'] and final['module_pinned'] and final['storage_pinned'] and final['foreign_calls']==0 and final['ticks_after_shutdown']==0 and not final['failed'])
    errors=[x for x in full['messages'] if x['type']=='error'];faults=[x for x in r if x['event'] in ['blocked','native_fault','hook_error','hook_create_error']]
    check('full_capture_no_recorded_faults',full['state']=='detached' and not full['messages_truncated'] and not errors and not faults)
    check('normal_exit',read(RUN/'exit.json')['exit_code']==read(RUN/'client_exit.json')['exit_code']==0)
    check('isolation_profiles_integrity',read(RUN/'ISOLATION_GATE.json')['status']=='PASS' and read(RUN/'protected_before.json')==read(RUN/'protected_after.json') and read(sorted(OUT.glob('INTEGRITY_*.json'))[-1])['status']=='PASS')
    check('WM_CLOSE_observed',len(os['wc_dispatch_message'])==1 and os['wc_dispatch_message'][0]['message']==16)
    check('no_process_or_endpoint',read(OUT/'OS_final.json')['processes']==[] and read(OUT/'OS_final.json')['udp']==[])
    check('no_forced_StateExit_or_destructor',not e['state_exit_entry'] and not e['owned_actor_dtor_entry'])
    guard=os['isolation_write_guard_summary'][-1]
    outside=[x for x in os['file_write_open_observed'] if x.get('succeeded') and not Path(x['path']).resolve().is_relative_to(ROOT)]
    check('no_observed_outside_disk_write_or_guard_error',not outside and guard['errors']==0 and guard['unique_overflow']==0)
    result=dict(native_verdict='PASS' if all(checks.values()) else 'PARTIAL',P2_2_overall='PARTIAL; scheduler fixed, timeout default unchanged pending channel calibration',checks=checks,passed=sum(checks.values()),total=len(checks),
       pid=launch['pid'],simulation_tid=sim,worker_tid=worker,client_pid=read(RUN/'client_process.json')['pid'],native_records=len(r),updates=len(updates),accepted=len(accepted),client_sends=len(sent),
       spawn=spawn,max_xy_displacement=distance,release=release,last_held_before_release=last_hold,release_update=release_update,
       release_delay_from_client_ms=(stamp(release['utc'])-stamp(sent[(release['generation'],release['input_seq'])]['utc']))*1000,
       expiry=expiry,expiry_update=expiry_update,scheduler_cost=next(x for x in c if x['event']=='scheduler_cost'),
       shutdown=[x for x in r if x['event'] in ['shutdown_entry','network_stop_begin','network_joined','network_retired','control_restore','before_unbind','shutdown_quiesced','engine_shutdown_entry','engine_shutdown_return','native_final_exit'] or x.get('operation')=='exit_reset'],
       os_sequence=[x for x in payload if x.get('kind','').startswith('wc_')],capture_messages=len(full['messages']),capture_truncated=full['messages_truncated'],errors=errors,faults=faults,guard=guard,
       limitations=['One native gen1 loopback scenario. No native reconnect/loss/jitter test or full M0/M1.1 repeat.',
         'Release removes drive; native velocity/coasting not forcibly cleared.',
         'P1 fields match the accepted comparison set, not full physics isolation.',
         'Clock/cadence and timeout not guarantees under arbitrary load or real LAN/VPN.',
         'AV/illegal-instruction/stack-overflow logger is not a universal UAF/exception sanitizer.'])
    (RUN/'RESULT_SUMMARY.json').write_text(json.dumps(result,indent=2),encoding='utf8');(OUT/'NATIVE_RESULT.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    print(json.dumps(dict(native_verdict=result['native_verdict'],passed=result['passed'],total=result['total'],failed=[k for k,v in checks.items() if not v],release_ms=result['release_delay_from_client_ms'],updates=len(updates))))
    assert all(checks.values())
if __name__=='__main__':main()
