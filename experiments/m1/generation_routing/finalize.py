"""DOC-46 derived audit/manifest. File-only; no native/game process or injection."""
from pathlib import Path
import datetime, difflib, hashlib, json, re, shutil, sys
LAB=Path(__file__).resolve().parent
ROOT=LAB.parents[2]
OUT=ROOT/'docs/evidence/m1/M12B_GENERATION_46'
ALLOWED={'tools/enet-prototype/include/thread_bridge_dto.h','tools/enet-prototype/include/enet_worker.h','tools/enet-prototype/src/enet_worker.cpp','tools/enet-prototype/src/mock_simulation.cpp','experiments/m1/network_input/src/network_input.cpp'}
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def load(p): return json.loads(p.read_text(encoding='utf-8-sig'))
def dump(p,v): p.write_text(json.dumps(v,indent=2,ensure_ascii=False),encoding='utf8')
def main():
    baseline=load(OUT/'BASELINE.json')
    changes=[]; patches=[]
    frozen=OUT/'after'
    for r in baseline['sources']:
        relative=r['path'].replace('\\','/'); p=ROOT/r['path']; digest=sha(p)
        if digest!=r['sha256']:
            changes.append(relative)
            patches.extend(difflib.unified_diff((OUT/'before'/r['path']).read_text(encoding='utf8').splitlines(True),p.read_text(encoding='utf8').splitlines(True),fromfile=relative+' (before)',tofile=relative+' (after)'))
        dest=frozen/r['path'];dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest)
    (OUT/'SOURCE_CHANGES.patch').write_text(''.join(patches),encoding='utf8')
    old_changed=[r['path'] for r in baseline['immutable'] if sha(ROOT/r['path'])!=r['sha256']]
    game_changed=[r['path'] for r in baseline['game'] if sha(Path(r['path']))!=r['sha256']]
    sys.path.insert(0,str(ROOT/'experiments/m0'));import prepare
    saves_equal=baseline['protected']==[prepare.snapshot(Path(r['path'])) for r in baseline['protected']]
    integrity=dict(status='PASS' if set(changes)==ALLOWED and not old_changed and not game_changed and saves_equal else 'FAIL',changed_sources=changes,expected_changes=sorted(ALLOWED),historical_immutable_files_checked=len(baseline['immutable']),historical_changes=old_changed,original_and_copy_files_checked=len(baseline['game']),game_changes=game_changed,protected_profiles_equal=saves_equal,native_hooks_source_unchanged=sha(ROOT/'experiments/m1/native_bootstrap/src/bootstrap.cpp')==next(r['sha256'] for r in baseline['sources'] if r['path'].replace('\\','/')=='experiments/m1/native_bootstrap/src/bootstrap.cpp'),wire_files_unchanged=all(sha(ROOT/r['path'])==r['sha256'] for r in baseline['sources'] if Path(r['path']).name in ['net_protocol.h','net_protocol.cpp','replication_protocol.h','replication_protocol.cpp']),game_launches=0)
    dump(OUT/'INTEGRITY.json',integrity)
    results=[]
    for p in sorted(OUT.glob('RUN_*/RESULT.json')):
        r=load(p);r['record']=str(p.relative_to(OUT));r['console']=(OUT/r['log']).read_text(encoding='utf-8-sig');results.append(r)
    def latest(phase,name): return next(r for r in reversed(results) if r['phase']==phase and r['name']==name)
    tests={}
    before=latest('before','r1_live');after=latest('after','r1_live')
    tests['R1_before_live_defect']=before['exit_code']==10 and 'gen2 after drain: client_connected=0 authenticated=0 session_wantsPlayer=0' in before['console']
    tests['R1_after_live_fixed']=after['exit_code']==0 and 'gen2 after drain: client_connected=1 authenticated=1 session_wantsPlayer=1' in after['console']
    original=latest('after','r1_original')
    tests['original_R1_false_verdict_documented']=original['exit_code']==10 and 'gen2 after drain: client_connected=1 authenticated=1 session_wantsPlayer=1' in original['console'] and 'DEFECT_REPRODUCED' in original['console']
    contract=latest('after','routing_contract')
    tests['routing_contract']=contract['exit_code']==0 and 'ROUTING SUMMARY cases=5 failed=0' in contract['console'] and not re.search(r'^FAIL |^CASE FAILED',contract['console'],re.M)
    counts={'pse_test_suite':r'Test Cases Passed: 46 / 46','pse_stress_test':r'Passed: 11 / 11','pse_fault_injection':r'PASS:\s*(?:\x1b\[[0-9;]*m)*\s*16','pse_bridge_acceptance':r'Acceptance Passed: 11 / 11'}
    for name,pattern in counts.items():
        r=latest('after',name);clean=re.sub(r'\x1b\[[0-9;]*m','',r['console']);tests[name]=r['exit_code']==0 and bool(re.search(pattern,clean))
    builds=[load(p) for p in sorted(OUT.glob('BUILD_after_*.json'))]
    tests['native_consumer_compile_only']=any(b['status']=='PASS' and 'native_compile_check' in b['commands'][-1]['argv'] for b in builds)
    tests['source_and_file_integrity']=integrity['status']=='PASS'
    selected=[latest('before','r1_original'),before,original,after,contract]+[latest('after',n) for n in counts]
    summary=dict(status='PASS' if all(tests.values()) else 'PARTIAL',scope='generation-scoped control routing only; not full M1.2b or new native acceptance',checks=tests,results=[{k:v for k,v in r.items() if k!='console'} for r in selected],evidence_records=[r['record'] for r in selected],native_runtime_repeated=False,new_game_DLLs=0,P2_1_through_P2_5_not_remediated=True)
    dump(OUT/'RESULT_SUMMARY.json',summary)
    current=[]
    for folder in [frozen,LAB]:
        for p in sorted(folder.rglob('*')):
            if not p.is_file() or {'build','__pycache__'}.intersection(p.parts):continue
            current.append(dict(path=str(p.relative_to(ROOT)),sha256=sha(p),size=p.stat().st_size))
    artifacts=[]
    for p in sorted((LAB/'build/after').rglob('*')):
        if p.is_file() and p.suffix in ['.exe','.lib','.obj'] and ('Release' in p.parts): artifacts.append(dict(path=str(p.relative_to(ROOT)),sha256=sha(p),size=p.stat().st_size))
    dump(OUT/'BUILD_MANIFEST.json',dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),configuration='MSVC19.44, SDK10.0.26100.0, x64 Release C++20 /MT; Session/tests/native compile /W4 /WX',sources=current,artifacts=artifacts,baseline='BASELINE.json',diff='SOURCE_CHANGES.patch'))
    print(json.dumps(dict(status=summary['status'],checks=tests,changed_sources=changes)))
    raise SystemExit(summary['status']!='PASS')
if __name__=='__main__':main()
