"""DOC51 fresh build, independent copies, retained commands and integrity."""
from pathlib import Path
import ctypes, datetime, difflib, hashlib, importlib.util, json, os, shutil, subprocess, sys
LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2]
OUT=ROOT/'docs/evidence/m1/NATIVE_SECOND_INSTANCE_51'
CM='C:/Program Files/CMake/bin/cmake.exe'
sys.path.insert(0,str(ROOT/'experiments/m0'))
import prepare
def sha(p): return prepare.digest(Path(p))
def stamp(): return datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
def dump(p,o):
    p.parent.mkdir(parents=True,exist_ok=True)
    with p.open('x',encoding='utf8') as f:json.dump(o,f,indent=2,ensure_ascii=False)
def game_check():
    spec=importlib.util.spec_from_file_location('old_workflow51',ROOT/'experiments/m1/window_close/workflow.py')
    old=importlib.util.module_from_spec(spec);spec.loader.exec_module(old)
    return old.game_check()
def protected_baseline():
    game=game_check();assert not game['errors'] and game['original_full_snapshot_equal']
    files=[]
    for folder in [ROOT/'docs/evidence/m1',ROOT/'experiments/m0']:
        for p in folder.rglob('*'):
            if p.is_file() and OUT not in p.parents and not {'game','__pycache__'}.intersection(p.relative_to(folder).parts):
                files.append(dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)))
    dump(OUT/'PROTECTED_BASELINE.json',dict(game=game,files=files))
    print('Protected baseline',len(files),flush=True)
def copy_client():
    assert not (LAB/'game').exists()
    prep=json.loads((ROOT/'docs/evidence/m0/M0_001_PREPARATION.json').read_text(encoding='utf8'))
    records=[]
    for r in prep['independent_copy_files']:
        src=prepare.COPY/r['relative_path'];dst=LAB/'game'/r['relative_path']
        assert sha(src)==r['sha256']
        dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
        assert not os.path.samefile(src,dst) and not os.path.samefile(prepare.SOURCE/r['relative_path'],dst)
        assert sha(dst)==r['sha256'];records.append(r)
    dump(OUT/'CLIENT_COPY.json',dict(source=str(prepare.COPY),copy=str(LAB/'game'),files=records,independent=True))
    print('Client independent copy',len(records),sum(r['size'] for r in records),flush=True)
def command(argv,tag,timeout=240,cwd=ROOT):
    folder=OUT/(tag+'_'+stamp());folder.mkdir()
    with (folder/'console.log').open('xb') as f:
        r=subprocess.run(list(map(str,argv)),cwd=cwd,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
    row=dict(argv=list(map(str,argv)),exit_code=r.returncode,log=str(folder.relative_to(OUT)/'console.log'))
    if Path(argv[0]).is_file():row['binary_sha256']=sha(argv[0])
    dump(folder/'RESULT.json',row);print(json.dumps(row),flush=True)
    assert r.returncode==0
    return row
def build():
    command([CM,'-S',LAB,'-B',LAB/'build','-G','Visual Studio 17 2022','-A','x64','-DCMAKE_SYSTEM_VERSION=10.0.26100.0'],'CONFIGURE')
    command([CM,'--build',LAB/'build','--config','Release','--parallel','2','--target','pse_native_client','pse_m12a','r1_live','routing_contract','r2_fixed','admission_contract','timeout_contract','sequence_contract','sequence_live','native_client_contract'],'BUILD')
    command([CM,'--build',LAB/'build/integration/transport','--config','Release','--parallel','2','--target','pse_test_suite','pse_stress_test','pse_fault_injection','pse_bridge_acceptance'],'BUILD_SUITES')
def sources():
    roots=['experiments/m1/native_bootstrap','experiments/m1/network_input','experiments/m1/native_second_instance','tools/enet-prototype']
    return sorted({p for d in roots for p in (ROOT/d).rglob('*') if p.is_file() and not {'build','profile','game','.git','__pycache__','field-test'}.intersection(p.relative_to(ROOT/d).parts)})
def freeze():
    suffix='_MAIN' if len(sys.argv)>2 and sys.argv[2]=='main' else ''
    records=[];artifacts=[]
    for p in sources():
        dst=OUT/('frozen_sources'+suffix)/p.relative_to(ROOT);dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dst)
        records.append(dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)))
    for p in sorted((LAB/'build/Release').glob('*')):
        if p.suffix in ['.dll','.exe','.pdb']:
            dst=OUT/('artifacts'+suffix)/p.name;dst.parent.mkdir(exist_ok=True);shutil.copy2(p,dst)
            artifacts.append(dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p),size=p.stat().st_size))
    dump(OUT/('BUILD_MANIFEST'+suffix+'.json'),dict(exe_sha256=prepare.EXPECTED,sources=records,artifacts=artifacts,configuration='MSVC19.44 Release x64 C++20 /MT; pristine ENet; host PSE_M12A current DOC46/47/49/50; native client opt-in cadence'))
    print('Frozen',len(records),'sources',len(artifacts),'artifacts')
def tests():
    results=[]
    for name in ['pse_test_suite','pse_stress_test','pse_fault_injection','pse_bridge_acceptance','r1_live','r2_fixed','routing_contract','admission_contract','timeout_contract','sequence_contract','sequence_live','native_client_contract']:
        args=['r2'] if name=='r2_fixed' else []
        results.append(command([LAB/'build/Release'/(name+'.exe')]+args,'TEST_'+name,cwd=OUT))
    dump(OUT/'REGRESSIONS.json',results)
def preflight():
    rows=[]
    for name in ['pse_native_client.dll','pse_m11_bootstrap.dll']:
        lib=ctypes.WinDLL(str(LAB/'build/Release'/name));fn=lib.M11Prepare
        fn.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32];fn.restype=ctypes.c_uint32
        p=OUT/('MUST_NOT_EXIST_'+name+'.jsonl');rc=fn(str(p),ctypes.windll.kernel32.GetCurrentThreadId())
        assert rc==193 and not p.exists();rows.append(dict(dll=name,sha256=sha(LAB/'build/Release'/name),return_code=rc,no_log=True))
    dump(OUT/('WRONG_PROCESS_PREFLIGHT_'+stamp()+'.json'),rows);print(json.dumps(rows))
def integrity():
    game=game_check();b=json.loads((OUT/'PROTECTED_BASELINE.json').read_text(encoding='utf8'))
    bad=[r['path'] for r in b['files'] if sha(ROOT/r['path'])!=r['sha256']]
    base=json.loads((OUT/'BASELINE.json').read_text(encoding='utf8'));changed=[];diff=[]
    allowed=['tools/enet-prototype/include/enet_worker.h','tools/enet-prototype/src/enet_worker.cpp','tools/enet-prototype/include/thread_bridge_dto.h','tools/enet-prototype/src/enet_client.cpp','tools/enet-prototype/src/enet_host.cpp']
    for r in base['files']:
        if sha(ROOT/r['path'])!=r['sha256']:
            changed.append(r['path'].replace('\\','/'))
            diff+=list(difflib.unified_diff((OUT/'before'/r['path']).read_text(encoding='utf8').splitlines(True),(ROOT/r['path']).read_text(encoding='utf8').splitlines(True),fromfile=r['path']+' before51',tofile=r['path']))
    new='tools/enet-prototype/include/client_input_cadence.h'
    if (ROOT/new).is_file():diff+=list(difflib.unified_diff([],(ROOT/new).read_text(encoding='utf8').splitlines(True),fromfile='/dev/null',tofile=new))
    (OUT/'SOURCE_CHANGES.patch').write_text(''.join(diff),encoding='utf8')
    client=json.loads((OUT/'CLIENT_COPY.json').read_text(encoding='utf8'));copybad=[r['relative_path'] for r in client['files'] if sha(LAB/'game'/r['relative_path'])!=r['sha256']]
    ok=not bad and not copybad and not game['errors'] and game['original_full_snapshot_equal'] and b['game']['protected']==game['protected'] and set(changed)<=set(allowed)
    obj=dict(status='PASS' if ok else 'FAIL',historical_files_checked=len(b['files']),historical_changed=bad,existing_source_changed=changed,client_copy_errors=copybad,game=game,protected_equal=b['game']['protected']==game['protected'])
    dump(OUT/('INTEGRITY_'+stamp()+'.json'),obj);print(json.dumps({k:v for k,v in obj.items() if k!='game'}));assert ok
if __name__=='__main__':{'baseline':protected_baseline,'copy':copy_client,'build':build,'tests':tests,'freeze':freeze,'preflight':preflight,'integrity':integrity}[sys.argv[1]]()
