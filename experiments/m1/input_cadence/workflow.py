"""DOC49 build/evidence lifecycle. Game launching is a separate, gated helper."""
from pathlib import Path
import datetime, difflib, hashlib, importlib.util, json, os, shutil, subprocess, sys
LAB=Path(__file__).resolve().parent; ROOT=LAB.parents[2]
OUT=ROOT/'docs/evidence/m1/M12B_CADENCE_49'
CM='C:/Program Files/CMake/bin/cmake.exe'
PRODUCT=['experiments/m1/network_input/src/native_client.cpp','experiments/m1/network_input/src/network_input.cpp','experiments/m1/network_input/src/network_input.hpp']
sys.path.insert(0,str(ROOT/'experiments/m1/window_close'))
import workflow as prior
def sha(p):return prior.sha(p)
def dump(p,o):
    with p.open('x',encoding='utf8') as f:json.dump(o,f,indent=2,ensure_ascii=False)
def stamp():return datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
def baseline():
    OUT.mkdir(parents=True,exist_ok=False)
    paths=prior.source_paths()+list((ROOT/'tools/m12b-input-cadence-qa').rglob('*'))
    for n in ['40','41','43','44','45','46','47','48']:paths+=list((ROOT/'docs').glob(n+'_*.md'))
    paths+=list((ROOT/'docs/evidence/m1/M12B_WINDOW_CLOSE_48').rglob('*'))
    paths+=list((ROOT/'docs/evidence/m1/M12B_WINDOW_CLOSE_01').rglob('*'))
    records=[]
    for p in sorted(set(paths)):
        if not p.is_file():continue
        records.append(dict(path=str(p.relative_to(ROOT)),sha256=sha(p)))
    game=prior.game_check();assert not game['errors'] and game['original_full_snapshot_equal']
    dump(OUT/'BASELINE.json',dict(utc=stamp(),files=records,game=game))
    for p in PRODUCT:
        dest=OUT/'before'/p;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/p,dest)
    shutil.copy2(ROOT/PRODUCT[0],LAB/'before_native_client.cpp')
    old=ROOT/'experiments/m1/window_close/build/Release/m12a_native_client.exe'
    shutil.copy2(old,OUT/'stock_before.exe')
    for name in ['PROGRESS.md','EVIDENCE_INDEX.md']:shutil.copy2(ROOT/'docs'/name,OUT/(name+'.before49'))
    dump(OUT/'BEFORE_BINARY.json',dict(path=str(old.relative_to(ROOT)),sha256=sha(old)))
    print(json.dumps(dict(files=len(records),stock_before_sha256=sha(old),integrity='PASS')))
def command(argv,name):
    log=OUT/(name+'.log')
    with log.open('xb') as f:r=subprocess.run(argv,cwd=ROOT,stdout=f,stderr=subprocess.STDOUT)
    dump(OUT/(name+'.json'),dict(argv=argv,exit_code=r.returncode,log=log.name));print(name,r.returncode,flush=True)
    assert r.returncode==0
def build():
    tag=stamp();folder=LAB/'build'
    command([CM,'-S',str(LAB),'-B',str(folder),'-G','Visual Studio 17 2022','-A','x64','-DCMAKE_SYSTEM_VERSION=10.0.26100.0'],'CONFIGURE_'+tag)
    targets=sys.argv[2:] or ['before_qa_client','after_qa_client','cadence_host','timeout_contract','pse_m12a','m12a_native_client','r1_live','r2_fixed','routing_contract','admission_contract']
    command([CM,'--build',str(folder),'--config','Release','--parallel','2','--target']+targets,'BUILD_'+tag)
    if not sys.argv[2:]:command([CM,'--build',str(folder/'integration/transport'),'--config','Release','--parallel','2','--target','pse_test_suite','pse_stress_test','pse_fault_injection','pse_bridge_acceptance'],'SUITES_BUILD_'+tag)
    files=[]
    for p in prior.source_paths()+[p for p in LAB.rglob('*') if p.is_file() and not {'build','profile','__pycache__'}.intersection(p.parts)]:
        dest=OUT/('sources_'+tag)/p.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest)
        files.append(dict(path=str(p.relative_to(ROOT)),sha256=sha(p)))
    artifacts=[]
    for p in (folder/'Release').glob('*'):
        if p.suffix not in ['.exe','.dll','.pdb']:continue
        dest=OUT/('artifacts_'+tag)/p.name;dest.parent.mkdir(exist_ok=True);shutil.copy2(p,dest)
        artifacts.append(dict(path=str(p.relative_to(ROOT)),sha256=sha(p)))
    dump(OUT/('BUILD_MANIFEST_'+tag+'.json'),dict(configuration='Release x64 /MT; one production DLL with pristine ENet; separate QA-only ENet accounting',sources=files,artifacts=artifacts))
def regress():
    names=['pse_test_suite','pse_stress_test','pse_fault_injection','pse_bridge_acceptance','r1_live','r2_fixed','routing_contract','admission_contract','timeout_contract']
    results=[]
    for n in names:
        exe=LAB/'build/Release'/(n+'.exe');folder=OUT/('REG_'+n+'_'+stamp());folder.mkdir()
        argv=[str(exe)]+(['r2'] if n=='r2_fixed' else [])
        with (folder/'console.log').open('xb') as f:r=subprocess.run(argv,cwd=folder,stdout=f,stderr=subprocess.STDOUT,timeout=180)
        row=dict(name=n,argv=argv,exit_code=r.returncode,sha256=sha(exe),log=str((folder/'console.log').relative_to(OUT)));dump(folder/'RESULT.json',row);results.append(row);print(json.dumps(row),flush=True)
    dump(OUT/('REGRESSION_'+stamp()+'.json'),results);assert all(r['exit_code']==0 for r in results)
def integrity():
    b=json.loads((OUT/'BASELINE.json').read_text(encoding='utf8'));changes=[];bad=[]
    for r in b['files']:
        if sha(ROOT/r['path'])!=r['sha256']:
            (changes if r['path'].replace('\\','/') in PRODUCT else bad).append(r['path'])
    game=prior.game_check()
    o=dict(utc=stamp(),checked=len(b['files']),production_changes=changes,unexpected_changes=bad,game=game,protected_equal=game['protected']==b['game']['protected'])
    o['status']='PASS' if not bad and not game['errors'] and game['original_full_snapshot_equal'] and o['protected_equal'] else 'FAIL'
    dump(OUT/('INTEGRITY_'+stamp()+'.json'),o)
    diff=[]
    for p in PRODUCT:diff+=list(difflib.unified_diff((OUT/'before'/p).read_text(encoding='utf8').splitlines(True),(ROOT/p).read_text(encoding='utf8').splitlines(True),fromfile=p+' (before)',tofile=p))
    (OUT/'SOURCE_CHANGES.patch').write_text(''.join(diff),encoding='utf8');print(json.dumps({k:v for k,v in o.items() if k!='game'}));assert o['status']=='PASS'
if __name__=='__main__':{'baseline':baseline,'build':build,'regress':regress,'integrity':integrity}[sys.argv[1]]()
