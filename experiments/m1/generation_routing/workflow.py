"""DOC-46 file/transport-only workflow. Never launches or loads a KEX DLL."""
from pathlib import Path
import datetime, hashlib, json, os, shutil, subprocess, sys
LAB=Path(__file__).resolve().parent
ROOT=LAB.parents[2]
OUT=ROOT/'docs/evidence/m1/M12B_GENERATION_46'
CM='C:/Program Files/CMake/bin/cmake.exe'
PRODUCT=['tools/enet-prototype/include/thread_bridge_dto.h','tools/enet-prototype/include/enet_worker.h','tools/enet-prototype/src/enet_worker.cpp','tools/enet-prototype/src/mock_simulation.cpp','experiments/m1/network_input/src/network_input.cpp']
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def dump(p,obj): p.write_text(json.dumps(obj,indent=2,ensure_ascii=False),encoding='utf8')
def stamp(): return datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
def snapshot():
    OUT.mkdir(exist_ok=False,parents=True)
    before=OUT/'before'; before.mkdir()
    sources=[]
    for folder in [ROOT/'tools/enet-prototype',ROOT/'experiments/m1/network_input',ROOT/'experiments/m1/native_bootstrap/src']:
        for p in sorted(folder.rglob('*')):
            if not p.is_file() or {'build','profile','field-test','__pycache__','output'}.intersection(p.parts): continue
            if p.suffix not in ['.cpp','.c','.h','.hpp','.py','.js','.txt','.md']: continue
            relative=p.relative_to(ROOT); dest=before/relative
            dest.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(p,dest)
            sources.append(dict(path=str(relative),sha256=sha(p)))
    immutable=[]
    prior=ROOT/'docs/evidence/m1/M12A_IMPLEMENTATION_20261102/FILES.json'
    paths=[ROOT/r['path'] for r in json.loads(prior.read_text(encoding='utf8'))['files']]
    paths += [ROOT/'docs/40_THREAD_BRIDGE_P0_REMEDIATION.md',ROOT/'docs/45_M12A_CRITICAL_ACCEPTANCE.md']
    paths += list((ROOT/'docs/evidence/m1/M12A_AUDIT_45').glob('*'))
    paths += [ROOT/'experiments/m1/native_bootstrap/build/Release/pse_m11_bootstrap.dll',ROOT/'experiments/m1/network_input/build/Release/pse_m11_bootstrap.dll']
    for p in sorted(set(paths)):
        if not p.is_file() or p.name in ['PROGRESS.md','EVIDENCE_INDEX.md']: continue
        immutable.append(dict(path=str(p.relative_to(ROOT)),sha256=sha(p)))
    sys.path.insert(0,str(ROOT/'experiments/m0')); import prepare
    preparation=json.loads((ROOT/'docs/evidence/m0/M0_001_PREPARATION.json').read_text(encoding='utf8'))
    game=[]
    for r in preparation['independent_copy_files']:
        for folder in [prepare.SOURCE,prepare.COPY]:
            p=folder/r['relative_path']; assert prepare.digest(p)==r['sha256']
            game.append(dict(path=str(p),sha256=prepare.digest(p)))
        assert not os.path.samefile(prepare.SOURCE/r['relative_path'],prepare.COPY/r['relative_path'])
    protected=[prepare.snapshot(Path(r['path'])) for r in preparation['protected_profiles_before']]
    dump(OUT/'BASELINE.json',dict(utc=stamp(),sources=sources,immutable=immutable,game=game,protected=protected,no_game_launch=True))
    for name in ['PROGRESS.md','EVIDENCE_INDEX.md']: shutil.copy2(ROOT/'docs'/name,OUT/(name+'.before46'))
    repro=ROOT/'docs/evidence/m1/M12A_AUDIT_45/repro.cpp'
    (LAB/'tests').mkdir(exist_ok=True)
    shutil.copy2(repro,LAB/'tests/r1_original.cpp')
    live=repro.read_text(encoding='utf8').replace('    s.stop("audit_r1");\n    const bool defect = !b.isConnected() || !s.wantsPlayer();','    const bool defect = !b.isConnected() || !b.isAuthenticated() || !s.wantsPlayer();\n    s.stop("audit_r1");')
    assert live!=repro.read_text(encoding='utf8')
    (LAB/'tests/r1_live.cpp').write_text(live,encoding='utf8')
    print(json.dumps(dict(sources=len(sources),immutable=len(immutable),game=len(game),original_repro_sha256=sha(repro))))
def build(phase,targets):
    folder=LAB/'build'/phase; tag='BUILD_'+phase+'_'+stamp(); commands=[]
    args=[[CM,'-S',str(LAB),'-B',str(folder),'-G','Visual Studio 17 2022','-A','x64','-DCMAKE_SYSTEM_VERSION=10.0.26100.0']]
    suites={'pse_test_suite','pse_stress_test','pse_fault_injection','pse_bridge_acceptance'}
    location=folder/'transport' if all(t in suites for t in targets) else folder
    args.append([CM,'--build',str(location),'--config','Release','--parallel','2','--target']+targets)
    for i,argv in enumerate(args):
        result=subprocess.run(argv,cwd=ROOT,capture_output=True)
        log=OUT/f'{tag}_{i}.log'; log.write_bytes(result.stdout+b'\nSTDERR:\n'+result.stderr)
        commands.append(dict(argv=argv,exit_code=result.returncode,log=log.name))
        print(result.stdout.decode(errors='replace')[-2500:],flush=True)
        if result.returncode: break
    manifest=dict(status='PASS' if len(commands)==2 and not commands[-1]['exit_code'] else 'FAIL',commands=commands,sources=[dict(path=p,sha256=sha(ROOT/p)) for p in PRODUCT])
    dump(OUT/(tag+'.json'),manifest)
    return manifest['status']=='PASS'
def run(phase,names):
    results=[]
    for name in names:
        exe=LAB/'build'/phase/('transport/Release' if name.startswith('pse_') else 'Release')/(name+'.exe')
        tag='RUN_'+phase+'_'+name+'_'+stamp(); folder=OUT/tag; folder.mkdir()
        with (folder/'console.log').open('xb') as f:
            try: r=subprocess.run([str(exe)],cwd=folder,stdout=f,stderr=subprocess.STDOUT,timeout=240); code=r.returncode
            except subprocess.TimeoutExpired: code='TIMEOUT'
        shutil.copy2(exe,folder/exe.name)
        result=dict(phase=phase,name=name,exit_code=code,exe_sha256=sha(exe),log=str((folder/'console.log').relative_to(OUT)))
        dump(folder/'RESULT.json',result); results.append(result); print(json.dumps(result),flush=True)
    dump(OUT/('RUNS_'+phase+'_'+stamp()+'.json'),results)
    # Original R1's post-stop verdict always returns10; preserve it as a known
    # harness result, never count it as product acceptance. r1_live is the gate.
    expected=lambda name: 10 if name=='r1_original' or phase=='before' and name=='r1_live' else 0
    return all(r['exit_code']==expected(r['name']) for r in results)
def main():
    if sys.argv[1]=='baseline': snapshot()
    elif sys.argv[1]=='build': raise SystemExit(not build(sys.argv[2],sys.argv[3:]))
    elif sys.argv[1]=='run': raise SystemExit(not run(sys.argv[2],sys.argv[3:]))
    else: raise ValueError('unknown command')
if __name__=='__main__': main()
