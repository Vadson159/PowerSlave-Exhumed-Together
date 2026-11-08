"""Review the completed P4 run01 as PARTIAL; never turns zero proxy events into acceptance."""
from pathlib import Path
import collections
import json
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'docs/evidence/m1/PSE_TOGETHER_P4'
def read(p): return json.loads(p.read_text(encoding='utf8'))
def main():
    result={'overall':'PARTIAL','proxy_acceptance':False,'current_host_runtime_accepted':False,'runs':{}}
    for name in ['P4_HOST_01','P4_CLIENT_01']:
        run=OUT/name;launch=read(run/'launch.json')
        rows=[json.loads(s) for s in (run/'native.jsonl').read_text(encoding='utf8').splitlines()]
        counts=collections.Counter(r['event'] for r in rows)
        bg=[r for r in rows if r['event']=='p4_client_background_tick']
        faults=[r for r in rows if r['event'] in ['blocked','native_fault','native_proxy_fault','hook_error']]
        restores=[r for r in rows if 'restor' in r['event'] and 'exact16' in r]
        exit=read(run/'exit.json');os=read(run/'OS_SIGNALED.json')
        checks={'exit0':exit['exit_code']==0,'retained_handle_exit0':os['wait']==0 and os['duplicate_wait']==0 and os['exit_code']==0,
            'protected_saves_equal':read(run/'protected_before.json')==read(run/'protected_after.json'),
            'all_logged_control_restores_exact':bool(restores) and all(r['exact16'] for r in restores),'no_native_fault_events':not faults,
            'bg_logged_samples_neutral':all(r['local_this_tick']==1 and r['focus_after']==1 and not any(r[k] for k in ['buttons','held_forward','held_back']) for r in bg)}
        result['runs'][name]={'pid':launch['pid'],'simulation_tid':launch['primary_thread_id'],'checks':checks,'event_counts':counts,
            'background_logged_samples':len(bg),'background_tick_counter_max':max((r['count'] for r in bg),default=0),'control_restores':restores,
            'native_fault_events':faults,'spawned_proxy_count':counts['proxy_spawned'],
            'p2_native_spawn_attempts':sum(r['event']=='native_call_attempt' and r.get('operation')=='spawn' for r in rows)}
    result['limitations']=['Client focus lost before first MapReady caused timeout/rejoin; no reconnect sign-off (P3).',
        'Host support precondition rejected native origin-at-floor; source corrected after run01, new candidate needs native run.',
        'No native proxy Spawn/Update/Remove, portal renderer observation, or jitter playout acceptance.',
        'Background observations have no live proxy and contain no positive held Attack/mouse precondition.',
        'Host guards=false at exit: external Player storage was never initialized; not an integrity failure or proof of guards.']
    result['bounded_checks_pass']=all(all(r['checks'].values()) for r in result['runs'].values())
    (OUT/'RUNTIME_RUN01_PARTIAL.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    print('PARTIAL; bounded observations',result['bounded_checks_pass'])
    for name,r in result['runs'].items(): print(name,'proxy',r['spawned_proxy_count'],'P2',r['p2_native_spawn_attempts'],'background counter',r['background_tick_counter_max'])
    return 0 if result['bounded_checks_pass'] else 1
if __name__=='__main__': raise SystemExit(main())
