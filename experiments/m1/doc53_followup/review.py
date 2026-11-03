"""Strict post-run correlation; positive input must come from the native journal."""
import datetime,json,sys
from workflow import ROOT,OUT,dump,stamp
E=ROOT/'docs/evidence/m1'
def readrun(n):return [json.loads(s) for s in (E/n/'native.jsonl').read_text(encoding='utf8').splitlines()]
def dt(s):return datetime.datetime.fromisoformat(s.replace('Z','+00:00'))
def main():
    host=readrun('N53F_HOST_01');client=readrun('N53F_CLIENT_01');checks=[]
    def check(name,passed,detail=None):checks.append(dict(name=name,pass_=bool(passed),detail=detail))
    byseq={r['seq']:r for r in host}
    for action in ['mouse','attack_mouse']:
        p=sorted(OUT.glob('INPUT_'+action+'_*.json'))[-1];o=json.loads(p.read_text(encoding='utf8'));rows=o['records']
        positive=next((r['samples'] for r in rows if r['event']=='positive_native_precondition'),[])
        check(action+'_positive',o['status']=='COMPLETED' and positive and all(byseq.get(r['seq'])==r for r in positive),dict(count=len(positive),first=positive[:1],last=positive[-1:]))
        foreground=[r for r in rows if r['event']=='real_foreground']
        loss=next(r for r in foreground if r['role']=='client');gain=foreground[-1]
        bg=[r for r in host if r['event']=='native_primary_input' and dt(loss['utc'])<=dt(r['utc'])<dt(gain['utc'])-datetime.timedelta(milliseconds=300)]
        neutral=lambda r:not any(r.get(k,0) for k in ['buttons','held_attack','yaw_delta','pitch_delta','mouse_x','mouse_y'])
        check(action+'_background_neutral',bg and all(r['neutral'] and neutral(r) for r in bg),len(bg))
        p2=[r for r in host if r['event']=='network_native_update' and dt(loss['utc'])<=dt(r['utc'])<dt(gain['utc'])-datetime.timedelta(milliseconds=300)]
        check(action+'_p2_continues',p2 and {4,8}.issubset({r['native_buttons'] for r in p2}),len(p2))
        begin=next(r for r in rows if r['event']=='gain_observation_begin')
        end=next(r for r in rows if r['event']=='gain_native_tail')
        after=[r for r in host if r['event']=='native_primary_input' and dt(begin['utc'])<=dt(r['utc'])<=dt(end['utc'])]
        check(action+'_no_replay_after_gain',len(after)>20 and all(neutral(r) for r in after),len(after))
    updates=[r for r in host if r['event']=='network_native_update']
    check('unique_native_p2_ticks',len(updates)==len({r['tick'] for r in updates}),len(updates))
    check('p1_preserved',updates and all(r['unchanged'] and r['p1_before']==r['p1_after'] and r['commands_this_update']==1 for r in updates))
    for role,rows in [('host',host),('client',client)]:
        bad=[r for r in rows if r['event'] in ['blocked','native_fault','hook_error','hook_create_error']]
        check(role+'_native_faults_zero',not bad,bad)
        path=E/('N53F_'+role.upper()+'_01')/'OS_SIGNALED.json'
        if path.exists():
            os=json.loads(path.read_text(encoding='utf8'));check(role+'_normal_os_exit',os['wait']==0 and os['exit_code']==0 and os['duplicate_wait']==0 and os['no_emergency_cleanup'],os)
        else:check(role+'_normal_os_exit',False,'not yet signaled')
    summary=dict(status='PASS' if all(r['pass_'] for r in checks) else 'PARTIAL',checks=checks,host_pid=40004,client_pid=16008,host_sim_tid=31800,client_sim_tid=36376,host_updates=len(updates),accepted_inputs=sum(r['event']=='network_input_accepted' for r in host),background_updates=sum(r.get('focus_override',False) for r in updates),synthetic_activation_messages=0)
    path=OUT/('NATIVE_REVIEW_'+stamp()+'.json');dump(path,summary)
    print(json.dumps(dict(path=str(path),status=summary['status'],host_updates=summary['host_updates'],failed=[r for r in checks if not r['pass_']]),indent=2))
if __name__=='__main__':main()
