"""DOC53 fresh isolated build, retained regression commands and source manifests."""
from pathlib import Path
import ctypes, difflib, json, shutil, subprocess, sys
from audit import ROOT, LAB, OUT, dump, stamp, sha, game_check
CM='C:/Program Files/CMake/bin/cmake.exe'
TARGETS=['pse_m12a','background_contract','r1_live','r2_fixed','routing_contract','admission_contract','timeout_contract','sequence_contract','sequence_live','native_client_contract']
SUITES=['pse_test_suite','pse_stress_test','pse_fault_injection','pse_bridge_acceptance']
def command(argv,tag,timeout=240):
    folder=OUT/(tag+'_'+stamp());folder.mkdir()
    with (folder/'console.log').open('xb') as f:r=subprocess.run(list(map(str,argv)),cwd=folder,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
    o=dict(argv=list(map(str,argv)),exit_code=r.returncode,log=str(folder.relative_to(OUT)/'console.log'))
    if Path(argv[0]).is_file():o['binary_sha256']=sha(argv[0])
    dump(folder/'RESULT.json',o);print(json.dumps(o),flush=True);assert r.returncode==0;return o
def build():
    command([CM,'-S',LAB,'-B',LAB/'build','-G','Visual Studio 17 2022','-A','x64','-DCMAKE_SYSTEM_VERSION=10.0.26100.0'],'CONFIGURE')
    command([CM,'--build',LAB/'build','--config','Release','--parallel','2','--target']+TARGETS,'BUILD')
    command([CM,'--build',LAB/'build/integration/integration/transport','--config','Release','--parallel','2','--target']+SUITES,'BUILD_SUITES')
def tests():
    names=['background_contract']+SUITES+TARGETS[2:]
    if len(sys.argv)>2 and sys.argv[2]=='remaining': names=SUITES[1:]+TARGETS[2:]
    rows=[command([LAB/'build/Release'/(n+'.exe')]+(['r2'] if n=='r2_fixed' else []),'TEST_'+n) for n in names]
    dump(OUT/('REGRESSIONS_'+stamp()+'.json'),rows)
def timer_fixture():
    command([CM,'-S',LAB,'-B',LAB/'build'],'CONFIGURE_TIMER_FIXTURE')
    command([CM,'--build',LAB/'build','--config','Release','--parallel','2','--target','pse_test_suite_timer_fixture'],'BUILD_TIMER_FIXTURE')
    command([LAB/'build/Release/pse_test_suite_timer_fixture.exe'],'TEST_TIMER_FIXTURE')
def source_paths():
    return sorted({p for d in ['experiments/m1/native_bootstrap','experiments/m1/network_input','experiments/m1/native_second_instance','experiments/m1/background_host','tools/enet-prototype'] for p in (ROOT/d).rglob('*') if p.is_file() and not {'build','profile','game','__pycache__','.git','field-test'}.intersection(p.relative_to(ROOT/d).parts)})
def freeze():
    records=[];artifacts=[]
    for p in source_paths():
        dst=OUT/'frozen_sources'/p.relative_to(ROOT);dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dst)
        records.append(dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)))
    for p in (LAB/'build/Release').glob('*'):
        if p.suffix in ['.dll','.exe','.pdb']:
            dst=OUT/'artifacts'/p.name;dst.parent.mkdir(exist_ok=True);shutil.copy2(p,dst);artifacts.append(dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p),size=p.stat().st_size))
    dump(OUT/'BUILD_MANIFEST.json',dict(exe_sha256='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d',sources=records,artifacts=artifacts,configuration='Release x64 C++20 /MT /W4 /WX; pristine ENet/MinHook; Host DOC53; Client retained DOC51'))
    diff=[]
    p=ROOT/'experiments/m1/native_bootstrap/src/bootstrap.cpp'
    diff+=list(difflib.unified_diff((OUT/'before'/p.relative_to(ROOT)).read_text(encoding='utf8').splitlines(True),p.read_text(encoding='utf8').splitlines(True),fromfile='before53/'+p.relative_to(ROOT).as_posix(),tofile=p.relative_to(ROOT).as_posix()))
    for p in sorted((ROOT/'experiments/m1/native_bootstrap/src').glob('background_*.hpp')):
        diff+=list(difflib.unified_diff([],p.read_text(encoding='utf8').splitlines(True),fromfile='/dev/null',tofile=p.relative_to(ROOT).as_posix()))
    (OUT/'SOURCE_CHANGES.patch').write_text(''.join(diff),encoding='utf8')
    print('Frozen',len(records),'sources',len(artifacts),'artifacts')
def preflight():
    g=game_check();assert not g['errors'] and not g['client_copy_errors'] and g['original_full_snapshot_equal']
    dll=LAB/'build/Release/pse_m11_bootstrap.dll';lib=ctypes.WinDLL(str(dll));fn=lib.M11Prepare;fn.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32];fn.restype=ctypes.c_uint32
    p=OUT/'MUST_NOT_EXIST.jsonl';rc=fn(str(p),ctypes.windll.kernel32.GetCurrentThreadId());assert rc==193 and not p.exists()
    dump(OUT/'WRONG_PROCESS_PREFLIGHT.json',dict(dll=str(dll),sha256=sha(dll),return_code=rc,no_log=True,game=g));print('Wrong process rejection PASS: 193')
if __name__=='__main__':{'build':build,'tests':tests,'freeze':freeze,'preflight':preflight,'timer_fixture':timer_fixture}[sys.argv[1]]()
