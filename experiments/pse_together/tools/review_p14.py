"""Re-read P14 evidence; verifies only bootstrap/isolation/restore/exit scope.

Gameplay BLK acceptance remains NOT_EXECUTED even when smoke checks pass.
"""
from pathlib import Path
import hashlib
import json
import integrity

ROOT=integrity.ROOT
OUT=ROOT/'docs/evidence/m1/PSE_TOGETHER_P14'
def load(p): return json.loads(p.read_text('utf-8'))
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    checks=[]
    def check(name,ok,**details): checks.append(dict(name=name,passed=bool(ok),**details))
    offline=load(OUT/'OFFLINE_03/RESULT.json')
    check('final offline',offline['status']=='PASS' and offline['passed']==offline['total_expected']==26)
    run=OUT/'P14_HOST_02';launch=load(run/'launch.json');rows=[json.loads(s) for s in (run/'native.jsonl').read_text('utf-8').splitlines()]
    events=lambda name:[r for r in rows if r['event']==name]
    check('bootstrap on attested primary',any(r['tid']==launch['primary_thread_id'] for r in events('bootstrap_ready')))
    check('current gameplay isolation',load(run/'ISOLATION_GATE.json')['status']=='PASS')
    check('no native fault/block',not any(events(n) for n in ['native_fault','blocked','hook_error','hook_create_error','p14_minhook_failure','p14_profile_mismatch']))
    restored=events('p14_hook_restore')
    check('five P14 hooks exact16',len(restored)==5 and {r['slot'] for r in restored}==set(range(5)) and all(r['value']==1 and r['tid']==launch['primary_thread_id'] for r in restored))
    controls=events('control_restore')
    check('four control restores exact16',len(controls)==4 and all(r['exact16'] for r in controls))
    check('no gameplay acceptance attempted',not any(events(n) for n in ['independent_spawn','p14_ai_choice','p14_ankh_granted','p14_ankh_consumed','p14_touch_owner']))
    for name in ['P14_HOST_01','P14_HOST_02']:
        r=OUT/name;signaled=load(r/'OS_SIGNALED.json')
        check(name+' same retained handles normal exit',load(r/'exit.json')['exit_code']==0 and signaled['exit_code']==0 and signaled['wait']==signaled['duplicate_wait']==0 and signaled['no_emergency_cleanup'])
        check(name+' protected snapshots equal',load(r/'protected_before.json')==load(r/'protected_after.json'))
    game=integrity.games();pre=load(OUT/'PRE_NATIVE_03.json')
    baseline=next(c['game'] for c in pre['checks'] if c['name']=='original and protected profiles')
    check('final original77/profiles equal pre-native',game==baseline and not game['errors'] and not game['client_copy_errors'] and game['original_full_snapshot_equal'],game=game)
    dll=run/'native_artifact/pse_m11_bootstrap.dll'
    cfg=load(run/'NATIVE_ARTIFACT.json')['config']
    check('loaded DLL equals current candidate',sha(dll)==cfg['dll_sha256']==sha(ROOT/'b60/dll/pse_dll_host_p14/pse_m11_bootstrap.dll'))
    sources=[ROOT/'experiments/pse_together/CMakeLists.txt',ROOT/'experiments/pse_together/include/pse_together/config.hpp']
    sources+=list((ROOT/'experiments/pse_together/src').rglob('*.cpp'))+list((ROOT/'experiments/pse_together/src').rglob('*.hpp'))
    sources+=list((ROOT/'experiments/pse_together/tests').rglob('*.cpp'))
    result=dict(status='PASS' if all(c['passed'] for c in checks) else 'FAIL',scope='P14 bootstrap/isolation/restoration smoke ONLY',checks=checks,
                package_status='REVIEW / PARTIAL',mode='V',gameplay_acceptance='NOT_EXECUTED',dead_respawn_sync='OFFLINE_ONLY',
                sources=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in sorted(sources)])
    (OUT/'FINAL_CHECKS.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(result['status'],sum(c['passed'] for c in checks),'/',len(checks),'smoke checks; gameplay NOT_EXECUTED')
    return 0 if result['status']=='PASS' else 1
if __name__=='__main__': raise SystemExit(main())
