"""DOC50 isolated build/evidence. Never launches KEX or writes protected paths."""
from pathlib import Path
import datetime, difflib, hashlib, importlib.util, json, shutil, subprocess, sys
LAB=Path(__file__).resolve().parent; ROOT=LAB.parents[2]
OUT=ROOT/'docs/evidence/m1/M12B_SEQUENCE_50'
spec=importlib.util.spec_from_file_location('doc48_workflow',ROOT/'experiments/m1/window_close/workflow.py')
prior=importlib.util.module_from_spec(spec);spec.loader.exec_module(prior)
CM=prior.CM
PRODUCT=['tools/enet-prototype/src/enet_worker.cpp','tools/enet-prototype/include/enet_worker.h','experiments/m1/network_input/src/network_input.cpp']
def sha(p):return prior.sha(Path(p))
def dump(p,o):
    with p.open('x',encoding='utf8') as f:json.dump(o,f,indent=2,ensure_ascii=False)
def stamp():return datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
def baseline():
    OUT.mkdir(parents=True,exist_ok=False)
    paths=prior.source_paths()
    paths += [p for p in (ROOT/'experiments/m1/input_cadence').rglob('*') if not {'build','profile','__pycache__'}.intersection(p.parts)]
    paths += list((ROOT/'tools/m12b-input-cadence-qa').rglob('*'))
    paths += [p for p in (ROOT/'docs').glob('*.md') if p.name not in ['PROGRESS.md','EVIDENCE_INDEX.md']]
    # Preserve the historical evidence in full, including binaries and raw logs.
    paths += list((ROOT/'docs/evidence/m1').rglob('*'))
    files=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in sorted(set(paths)) if p.is_file() and OUT not in p.parents]
    game=prior.game_check();assert not game['errors'] and game['original_full_snapshot_equal']
    dump(OUT/'BASELINE.json',dict(utc=stamp(),files=files,game=game))
    for n in PRODUCT:
        dest=OUT/'before'/n;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/n,dest)
    for n in ['PROGRESS.md','EVIDENCE_INDEX.md']:shutil.copy2(ROOT/'docs'/n,OUT/(n+'.before50'))
    print('BASELINE',len(files),'game',game['files'],flush=True)
def command(argv,name,cwd=ROOT,expected=0):
    folder=OUT/(name+'_'+stamp());folder.mkdir()
    with (folder/'console.log').open('xb') as f:r=subprocess.run(argv,cwd=cwd,stdout=f,stderr=subprocess.STDOUT,timeout=240)
    o=dict(argv=list(map(str,argv)),exit_code=r.returncode,expected=expected,log=str(folder.relative_to(OUT)/'console.log'))
    if Path(argv[0]).is_file():o['binary_sha256']=sha(argv[0])
    dump(folder/'RESULT.json',o);print(json.dumps(o),flush=True)
    assert r.returncode==expected
    return o
def build():
    phase=sys.argv[2];b=LAB/'build'/phase
    command([CM,'-S',str(LAB),'-B',str(b),'-G','Visual Studio 17 2022','-A','x64','-DCMAKE_SYSTEM_VERSION=10.0.26100.0'],'CONFIGURE_'+phase)
    targets=['sequence_contract','sequence_live'] if phase=='before' else ['sequence_contract','sequence_live','native_compile_check','timeout_contract','r1_live','r2_fixed','routing_contract','admission_contract']
    command([CM,'--build',str(b),'--config','Release','--parallel','2','--target']+targets,'BUILD_'+phase)
    if phase!='before':command([CM,'--build',str(b/'integration/transport'),'--config','Release','--parallel','2','--target','pse_test_suite','pse_stress_test','pse_fault_injection','pse_bridge_acceptance'],'SUITES_BUILD_'+phase)
    frozen=phase+'_'+stamp();sources=[]
    for p in prior.source_paths()+[p for p in LAB.glob('*') if p.is_file()]:
        dest=OUT/frozen/p.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest)
        sources.append(dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)))
    artifacts=[]
    for p in (b/'Release').glob('*.exe'):
        dest=OUT/frozen/'artifacts'/p.name;dest.parent.mkdir(exist_ok=True,parents=True);shutil.copy2(p,dest)
        artifacts.append(dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)))
    dump(OUT/('BUILD_MANIFEST_'+phase+'_'+stamp()+'.json'),dict(frozen=frozen,sources=sources,artifacts=artifacts,configuration='MSVC Release x64 C++20 /MT; pristine ENet; native object compile only'))
def run():
    phase=sys.argv[2];names=sys.argv[3:] or ['sequence_contract','sequence_live','pse_test_suite','pse_stress_test','pse_fault_injection','pse_bridge_acceptance','r1_live','r2_fixed','routing_contract','admission_contract','timeout_contract']
    results=[]
    for name in names:
        exe=LAB/'build'/phase/'Release'/(name+'.exe')
        args= ['before'] if phase=='before' else (['r2'] if name=='r2_fixed' else [])
        results.append(command([str(exe)]+args,'RUN_'+phase+'_'+name,OUT))
    dump(OUT/('TESTS_'+phase+'_'+stamp()+'.json'),results)
def integrity():
    b=json.loads((OUT/'BASELINE.json').read_text(encoding='utf8'));changes=[];bad=[]
    for r in b['files']:
        if sha(ROOT/r['path'])!=r['sha256']:(changes if r['path'] in PRODUCT else bad).append(r['path'])
    game=prior.game_check()
    o=dict(utc=stamp(),checked=len(b['files']),production_changes=changes,unexpected=bad,game=game,protected_equal=b['game']['protected']==game['protected'])
    o['status']='PASS' if not bad and not game['errors'] and game['original_full_snapshot_equal'] and o['protected_equal'] else 'FAIL'
    dump(OUT/('INTEGRITY_'+stamp()+'.json'),o)
    diff=[]
    for n in PRODUCT:diff+=list(difflib.unified_diff((OUT/'before'/n).read_text(encoding='utf8').splitlines(True),(ROOT/n).read_text(encoding='utf8').splitlines(True),fromfile=n+' (before)',tofile=n))
    new='tools/enet-prototype/include/input_sequence.h'
    diff+=list(difflib.unified_diff([], (ROOT/new).read_text(encoding='utf8').splitlines(True),fromfile='/dev/null',tofile=new))
    (OUT/'SOURCE_CHANGES.patch').write_text(''.join(diff),encoding='utf8')
    print(json.dumps({k:v for k,v in o.items() if k!='game'}));assert o['status']=='PASS'
if __name__=='__main__':{'baseline':baseline,'build':build,'run':run,'integrity':integrity}[sys.argv[1]]()
