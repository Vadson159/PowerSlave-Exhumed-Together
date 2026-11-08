"""Recompute bounded P8 run04 facts from raw logs. Never executes KEX.

PASS here means the listed evidence assertions, not S2.4 acceptance or combat,
capacity8, direction-art quality, map transitions, or independent mode-V review.
"""
from pathlib import Path
from collections import defaultdict
import bisect
import hashlib
import json

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'docs/evidence/m1/PSE_TOGETHER_P8'
def read(p):return json.loads(p.read_text(encoding='utf8'))
def rows(p):
    result=[]
    for line in p.read_text(encoding='utf8').splitlines():
        if line.strip():result.append(json.loads(line))
    return result
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    c=OUT/'P8_CLIENT_04';h=OUT/'P8_HOST_04';native=rows(c/'native.jsonl');host=rows(h/'native.jsonl')
    observer=rows(c/'observer_LIVE.jsonl');checks=[]
    def check(name,ok,**details):checks.append(dict(name=name,passed=bool(ok),**details))
    for run in (c,h):
        check(run.name+' isolation',read(run/'ISOLATION_GATE.json')['status']=='PASS')
        exit=read(run/'exit.json');signaled=read(run/'OS_SIGNALED.json')
        check(run.name+' retained process handle signaled exit0',exit['exit_code']==0 and signaled['wait']==0 and signaled['exit_code']==0)
        check(run.name+' protected saves unchanged',read(run/'protected_before.json')==read(run/'protected_after.json'))
        status=read(run/'STATUS_EXIT.json')['structuredContent']['normalized_result']
        check(run.name+' Frida untruncated',not status['messages_truncated'] and status['state']=='detached')
        check(run.name+' Frida no script errors',not [m for m in status['messages'] if m.get('type')=='error'])
    check('no native faults',not [r for r in native+host if r['event'] in ('native_proxy_fault','native_fault','blocked','native_proxy_controller_stopped')])
    gate=[r for r in native if r['event']=='p8_resource_gate']
    check('16 distinct action-free resources',len(gate)==1 and gate[0]['distinct_resources']==16 and gate[0]['actions']==0)
    spawned=[r for r in native if r['event']=='proxy_spawned']
    check('one owned proxy',len(spawned)==1 and len([r for r in native if r['event']=='native_factory_call'])==1)
    check('both host/client art binding observed',bool([r for r in host if r['event']=='p8_host_animation']) and bool([r for r in native if r['event']=='p8_animation']))
    arts=[r for r in native if r['event']=='p8_animation'];times=[r['utc'] for r in arts];frames=defaultdict(set)
    for r in observer:
        if r['event']!='p8_frame_resource':continue
        i=bisect.bisect_right(times,r['utc'])-1
        if i>=0:frames[arts[i]['resource']].add(int(r['frame_token'],16))
    frame_summary={}
    for char in ('expedition_ranger','ruins_scout'):
        for pose,count in [('idle',1),('walk',4),('shoot',2),('death',7)]:
            key=f'player/{char}_proxy_{pose}';observed=sorted(frames[key])
            stride=bool(observed) and observed==[observed[0]+i*0x110 for i in range(count)]
            check(key+' native frame progression',len(observed)==count and stride,frame_count=len(observed))
            frame_summary[key]=dict(frame_tokens=[hex(v) for v in observed],count=len(observed))
    check('death holds last frame in both previews',all(any(r['event']=='p8_animation' and r['resource']==f'player/{char}_proxy_death' and r['frame']==6 and not r['changed'] for r in native) for char in ('expedition_ranger','ruins_scout')))
    summaries=[r for r in observer if r['event']=='observer_summary'];summary=summaries[-1]
    check('renderer matched owned proxy',summary['ours_sprites']>0 and summary['matching_sprites']==summary['ours_sprites'] and not summary['ambiguous_sprites'] and not summary['unknown_identity_sprites'])
    check('renderer/DTO no errors or foreign callbacks',not summary['errors'] and not summary['foreign_callbacks'] and not summary['invalid_phase_or_identity'])
    comparisons=[r for r in observer if r['event']=='p1_state_comparison']
    check('local player fields equal',bool(comparisons) and all(r['status']=='EQUAL' and not r['differences'] for r in comparisons),comparisons=len(comparisons))
    check('bounded movement refusal did not fault animation',any(r['event']=='native_proxy_stable' and r['refused']>0 for r in native))
    removes=[r for r in native if r['event']=='native_remove_request'];retired=[r for r in native if r['event']=='deferred_retired']
    check('one Remove and native deferred retirement',len(removes)==1 and len(retired)==1 and retired[0]['success'] and retired[0]['all_weak_pointers_null'])
    restores=[r for r in native if r['event']=='native_proxy_hook_restored']
    check('six proxy hooks exact restored',len(restores)==6 and all(r['exact16'] and r['status']==0 for r in restores))
    original=read(OUT/'INTEGRITY_FINAL.json');before=read(OUT/'INTEGRITY_BEFORE.json')
    check('original 77 and protected saves unchanged',original==before and original['files']==77 and original['original_full_snapshot_equal'] and not original['errors'] and not original['client_copy_errors'])
    frozen=read(OUT/'PRE_NATIVE_GATE_RUN04.json')
    check('pre-native gate passed',frozen['status']=='PASS')
    for row in frozen['frozen']:
        check('frozen '+row['path'],sha(ROOT/row['path'])==row['sha256'])
    result=dict(status='PASS' if all(c['passed'] for c in checks) else 'FAIL',scope='Bounded evidence assertions only; NOT S2.4 acceptance',
                package_status='REVIEW / PARTIAL',checks=checks,frame_resources=frame_summary,renderer=summary,
                native_spawn=spawned,comparisons=len(comparisons),game_runs=['P8_HOST_04','P8_CLIENT_04'])
    (OUT/'RUN04_CHECKS.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    print(result['status'],sum(c['passed'] for c in checks),'/',len(checks),'checks; NOT full acceptance')
    return 0 if result['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
