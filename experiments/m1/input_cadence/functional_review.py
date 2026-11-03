"""Check the actual before/after clients' functional network scenarios offline."""
from pathlib import Path
import json,sys
from matrix import rows,commands,dump
LAB=Path(__file__).resolve().parent;OUT=LAB.parents[2]/'docs/evidence/m1/M12B_CADENCE_49'
def main():
    reports=[]
    for name in ['before','after']:
        prefix=sys.argv[1] if len(sys.argv)>1 else ''
        p=OUT/('NET_'+prefix+name+'_functional');m=json.loads((p/'META.json').read_text());h=rows(p/'host/host.jsonl');c=rows(p/'client/client.jsonl');q=rows(p/'client/qa.jsonl');proxy=rows(p/'proxy.jsonl');restart=rows(p/'restart/client.jsonl')
        timeline={r['stage']:r['us'] for r in m['timeline']};checks={}
        def check(k,v):checks[k]=bool(v)
        welcomes=[r for r in c if r['event']=='welcome'];check('actual_client_reconnect_gen1_to_gen2',[r['generation'] for r in welcomes]==[1,2])
        check('disconnect_held_immediately_neutral',any(r['event']=='network_neutral' and r.get('reason')=='disconnect' and r['previous_buttons']==4 for r in h))
        gen2=[r for r in h if r['event']=='network_input_accepted' and r['generation']==2]
        check('rejoined_generation_begins_neutral',gen2 and gen2[0]['native_buttons']==0)
        check('restart_without_secret_never_welcomed',not any(r['event']=='welcome' for r in restart) and m['restart_exit']==0)
        check('restart_preserves_reservation',sum(r['event']=='network_session' and r['type'] in [0,2] for r in h)==2)
        firstneutral=next(r for r in m['timeline'] if r['stage']=='control_neutral')['us'];releaseend=next(r for r in m['timeline'] if r['stage']=='control_hold' and r['us']>firstneutral)['us']
        held=[r for r in h if r['event']=='network_input_accepted' and r['native_buttons']==4 and r['us']<firstneutral]
        release=[r for r in h if r['event']=='network_input_accepted' and r['native_buttons']==0 and firstneutral<=r['us']<=releaseend]
        check('first_release_packet_really_dropped',sum(r.get('reason')=='first_release' for r in proxy)==1)
        check('later_neutral_releases_without_timeout',release and not any(r['event']=='network_input_expired' and firstneutral<=r['us']<=release[0]['us'] for r in h))
        silence_start=next(r['us'] for r in m['timeline'] if r['stage']=='control_silent')
        silence_end=next(r['us'] for r in m['timeline'] if r['stage']=='control_neutral' and r['us']>silence_start)
        checks['silence_observes_exact_6_tick_OR_contract']=any(r['event']=='network_input_expired' and r['threshold_ticks']==6 and (r['elapsed_ticks']>=6 or r['elapsed_wall_ms']>=150) and silence_start<=r['us']<silence_end for r in h)
        for start,end,key in [('stall_begin','stall_end','hold_stall'),('release_stall_begin','release_stall_end','release_stall')]:
            a,b=timeline[start],timeline[end]
            check(key+'_timeout_fail_safe',any(r['event']=='network_input_expired' and a<r['us']<b for r in h))
            queued=[]
            for r in q:
                if r['kind']=='queued' and b<=r['us']<b+50000:queued+=commands(bytes.fromhex(r['data']))
            check(key+'_no_catchup_burst',1<=len(queued)<=4)
            if key=='release_stall':check('release_while_suspended_uses_latest_state',queued and queued[0]['forward']==0)
        check('clean_process_exits',m['client_exit']==m['host_exit']==0)
        r=dict(client=name,checks=checks,passed=sum(checks.values()),total=len(checks),
          release_after_control_ms=(release[0]['us']-firstneutral)/1000 if release else None,
          missing_key_restart='Expected safe REJECT; no on-disk secret persistence added',
          stall_windows=[r for r in m['timeline'] if 'stall' in r['stage']])
        reports.append(r)
    dump(OUT/('FUNCTIONAL_REVIEW_'+(prefix or 'initial')+'.json'),reports)
    print(json.dumps([dict(client=r['client'],passed=r['passed'],total=r['total'],failed=[k for k,v in r['checks'].items() if not v]) for r in reports]))
    assert all(all(r['checks'].values()) for r in reports)
if __name__=='__main__':main()
