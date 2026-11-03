"""DOC-47 file-only evidence/integrity packaging; never launches game code."""
from pathlib import Path
import datetime,difflib,hashlib,json,re,shutil,sys
LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2];OUT=ROOT/'docs/evidence/m1/M12B_ADMISSION_47'
ALLOWED={'tools/enet-prototype/include/simulation.h','tools/enet-prototype/src/simulation.cpp','tools/enet-prototype/include/enet_host.h','tools/enet-prototype/src/enet_host.cpp','experiments/m1/network_input/src/network_input.cpp'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def dump(p,v):p.write_text(json.dumps(v,indent=2,ensure_ascii=False),encoding='utf8')
def main():
    baseline=load(OUT/'BASELINE.json');changes=[];patch=[];sources=[]
    for r in baseline['sources']:
        rel=r['path'].replace('\\','/');p=ROOT/r['path']
        if sha(p)!=r['sha256']:
            changes.append(rel);patch.extend(difflib.unified_diff((OUT/'before'/r['path']).read_text(encoding='utf8').splitlines(True),p.read_text(encoding='utf8').splitlines(True),fromfile=rel+' (before)',tofile=rel+' (after)'))
        dest=OUT/'after'/r['path'];dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest)
        sources.append(dict(path=rel,sha256=sha(p)))
    (OUT/'SOURCE_CHANGES.patch').write_text(''.join(patch),encoding='utf8')
    historical=[r['path'] for r in baseline['immutable'] if sha(ROOT/r['path'])!=r['sha256']]
    game=[r['path'] for r in baseline['game'] if sha(Path(r['path']))!=r['sha256']]
    sys.path.insert(0,str(ROOT/'experiments/m0'));import prepare
    saves=baseline['protected']==[prepare.snapshot(Path(r['path'])) for r in baseline['protected']]
    immutable=['tools/enet-prototype/include/thread_bridge_dto.h','tools/enet-prototype/include/enet_worker.h','tools/enet-prototype/src/enet_worker.cpp','tools/enet-prototype/src/thread_bridge.cpp','tools/enet-prototype/include/thread_bridge.h','tools/enet-prototype/include/spsc_queue.h','experiments/m1/native_bootstrap/src/bootstrap.cpp','tools/enet-prototype/include/net_protocol.h','tools/enet-prototype/src/net_protocol.cpp','tools/enet-prototype/include/replication_protocol.h','tools/enet-prototype/src/replication_protocol.cpp']
    untouched=all(sha(ROOT/n)==next(r['sha256'] for r in baseline['sources'] if r['path'].replace('\\','/')==n) for n in immutable)
    integrity=dict(status='PASS' if set(changes)==ALLOWED and not historical and not game and saves and untouched else 'FAIL',changed_sources=changes,historical_files_checked=len(baseline['immutable']),historical_changes=historical,game_files_checked=len(baseline['game']),game_changes=game,protected_profiles_equal=saves,DOC46_routing_wire_native_and_SPSC_unchanged=untouched,protected_source_paths=immutable,game_launches=0)
    dump(OUT/'INTEGRITY.json',integrity)
    runs=[]
    for p in sorted(OUT.glob('RUN_*/RESULT.json')):
        r=load(p);r['console']=(OUT/r['log']).read_text(encoding='utf-8-sig');r['record']=str(p.relative_to(OUT));runs.append(r)
    def latest(phase,name):return next(r for r in reversed(runs) if r['phase']==phase and r['name']==name)
    before=latest('before','r2_original');after=latest('after','r2_original');fixed=latest('after','r2_fixed');contract=latest('after','admission_contract')
    checks={'R2_before_reproduced':before['exit_code']==10 and 'slot=2' in before['console'] and 'authenticated=0 session_wantsPlayer=0' in before['console'],
            'R2_original_after':after['exit_code']==0 and 'alpha rejoin with valid key: authenticated=1 session_wantsPlayer=1' in after['console'],
            'R2_strict_live_after':fixed['exit_code']==0 and 'R2 LIVE: beta_connected=0 alpha_slot=1 alpha_gen=2 alpha_ready=1 fixed=1' in fixed['console'],
            'admission_contract':contract['exit_code']==0 and 'ADMISSION SUMMARY scenarios=4 failures=0' in contract['console'] and 'cycle=7 beta_reject=2 forged_reject=8 alpha_slot=1 alpha_generation=9 neutral=0' in contract['console'] and 'extra_slots_empty=1 auth_events=1 ended_events=1 custom_packets=0' in contract['console'] and 'Rejected unauthenticated peer expired' in contract['console'],
            'R1_DOC46':latest('after','r1_live')['exit_code']==0 and 'gen2 after drain: client_connected=1 authenticated=1 session_wantsPlayer=1' in latest('after','r1_live')['console'],
            'routing_DOC46':latest('after','routing_contract')['exit_code']==0 and 'ROUTING SUMMARY cases=5 failed=0' in latest('after','routing_contract')['console'],
            'integrity':integrity['status']=='PASS'}
    for name,pattern in {'pse_test_suite':'Test Cases Passed: 46 / 46','pse_stress_test':'Passed: 11 / 11','pse_fault_injection':r'PASS:\s*16','pse_bridge_acceptance':'Acceptance Passed: 11 / 11'}.items():
        r=latest('after',name);checks[name]=r['exit_code']==0 and bool(re.search(pattern,re.sub(r'\x1b\[[0-9;]*m','',r['console'])))
    summary=dict(status='PASS' if all(checks.values()) else 'PARTIAL',scope='DOC45 P2-1 single-client admission/rejoin only; not full M1.2b',checks=checks,runs=[{k:v for k,v in r.items() if k!='console'} for r in runs],game_runs=0,policy=dict(maxClients=1,maxClientSlots=1,rejectedPeerTimeoutMs=250,reservation_lifetime='until host session ledger recreation; no participant rotation'))
    dump(OUT/'RESULT_SUMMARY.json',summary)
    for p in LAB.rglob('*'):
        if p.is_file() and not {'build','__pycache__'}.intersection(p.parts):sources.append(dict(path=str(p.relative_to(ROOT)),sha256=sha(p)))
    artifacts=[dict(path=str(p.relative_to(ROOT)),sha256=sha(p),size=p.stat().st_size) for p in (LAB/'build/after').rglob('*') if p.is_file() and p.suffix in ['.exe','.lib'] and 'Release' in p.parts]
    dump(OUT/'BUILD_MANIFEST.json',dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),configuration='MSVC19.44 SDK10.0.26100.0 Release x64 C++20 /MT; Session and admission_contract /W4 /WX',sources=sources,artifacts=artifacts))
    print(json.dumps(dict(status=summary['status'],checks=checks,changed_sources=changes)));raise SystemExit(summary['status']!='PASS')
if __name__=='__main__':main()
