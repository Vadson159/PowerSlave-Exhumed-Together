"""DOC-48 fresh build, manifests and file-only integrity. No game launch."""
from pathlib import Path
import datetime, hashlib, json, os, shutil, subprocess, sys
LAB = Path(__file__).resolve().parent
ROOT = LAB.parents[2]
OUT = ROOT/'docs/evidence/m1/M12B_WINDOW_CLOSE_48'
CM = 'C:/Program Files/CMake/bin/cmake.exe'
sys.path.insert(0, str(ROOT/'experiments/m0'))
import prepare
def sha(p): return prepare.digest(p)
def dump(p, o):
    with p.open('x', encoding='utf8') as f: json.dump(o, f, indent=2, ensure_ascii=False)
def stamp(): return datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
def source_paths():
    roots = ['experiments/m1/native_bootstrap','experiments/m1/network_input','tools/enet-prototype',
             'experiments/m1/generation_routing','experiments/m1/admission_rejoin']
    return sorted({p for r in roots for p in (ROOT/r).rglob('*') if p.is_file()
                   and not {'build','profile','__pycache__','field-test'}.intersection(p.parts)})
def game_check():
    prep=json.loads((ROOT/'docs/evidence/m0/M0_001_PREPARATION.json').read_text(encoding='utf8'))
    bad=[]
    for r in prep['independent_copy_files']:
        a,b=prepare.SOURCE/r['relative_path'], prepare.COPY/r['relative_path']
        if sha(a)!=r['sha256'] or sha(b)!=r['sha256'] or os.path.samefile(a,b): bad.append(r['relative_path'])
    return dict(files=len(prep['independent_copy_files']), errors=bad,
                original_full_snapshot_equal=prepare.snapshot(prepare.SOURCE)==prep['original_installation_before'],
                protected=[prepare.snapshot(Path(r['path'])) for r in prep['protected_profiles_before']])
def baseline():
    OUT.mkdir(parents=True,exist_ok=False)
    paths=source_paths()
    paths+=list((ROOT/'tools/m12b-input-cadence-qa').rglob('*'))
    for n in ['40','41','43','44','45','46','47']:
        paths+=list((ROOT/'docs').glob(n+'_*.md'))
    for name in ['M12A_NATIVE_01','M12A_DIRECT_QUIT_01','M12B_GENERATION_46','M12B_ADMISSION_47']:
        paths+=list((ROOT/'docs/evidence/m1'/name).rglob('*'))
    paths += list((ROOT/'experiments/m1/network_input/build/Release').glob('*.dll'))
    paths += list((ROOT/'experiments/m1/native_bootstrap/build/Release').glob('*.dll'))
    files=[dict(path=str(p.relative_to(ROOT)),sha256=sha(p)) for p in sorted(set(paths)) if p.is_file()]
    game=game_check()
    assert not game['errors'] and game['original_full_snapshot_equal']
    dump(OUT/'BASELINE.json',dict(utc=stamp(),immutable=files,game=game))
    for n in ['PROGRESS.md','EVIDENCE_INDEX.md']: shutil.copy2(ROOT/'docs'/n,OUT/(n+'.before48'))
    print(json.dumps(dict(status='PASS',immutable=len(files),game_files=game['files'])))
def commands(args, tag):
    records=[]
    for i,argv in enumerate(args):
        p=OUT/f'{tag}_{i}.log'
        with p.open('xb') as f: r=subprocess.run(argv,cwd=ROOT,stdout=f,stderr=subprocess.STDOUT)
        records.append(dict(argv=argv,exit_code=r.returncode,log=p.name))
        print(json.dumps(records[-1]),flush=True)
        if r.returncode: break
    dump(OUT/(tag+'.json'),dict(commands=records))
    assert len(records)==len(args) and all(r['exit_code']==0 for r in records)
def build():
    targets=['pse_m12a','m12a_native_client','r1_live','r2_fixed','routing_contract','admission_contract']
    commands([[CM,'-S',str(LAB),'-B',str(LAB/'build'),'-G','Visual Studio 17 2022','-A','x64','-DCMAKE_SYSTEM_VERSION=10.0.26100.0'],
              [CM,'--build',str(LAB/'build'),'--config','Release','--parallel','2','--target']+targets,
              [CM,'--build',str(LAB/'build/integration/transport'),'--config','Release','--parallel','2','--target',
               'pse_test_suite','pse_stress_test','pse_fault_injection','pse_bridge_acceptance']], 'BUILD_'+stamp())
    frozen=OUT/'frozen_sources'
    sources=[]
    for p in source_paths()+[p for p in LAB.glob('*') if p.is_file()]:
        dest=frozen/p.relative_to(ROOT); dest.parent.mkdir(exist_ok=True,parents=True); shutil.copy2(p,dest)
        sources.append(dict(path=str(p.relative_to(ROOT)),sha256=sha(p)))
    sources=sorted(sources,key=lambda r:r['path'])
    artifacts=[]
    for p in sorted((LAB/'build/Release').glob('*')):
        if p.suffix in ['.dll','.exe','.pdb']:
            dest=OUT/'artifacts'/p.name; dest.parent.mkdir(exist_ok=True); shutil.copy2(p,dest)
            artifacts.append(dict(path=str(p.relative_to(ROOT)),sha256=sha(p),size=p.stat().st_size))
    aggregate=hashlib.sha256(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    dump(OUT/'BUILD_MANIFEST.json',dict(exe_sha256=prepare.EXPECTED,sources=sources,source_manifest_sha256=aggregate,
         source_manifest_hash_encoding='SHA256 UTF8 json.dumps(sources,sort_keys=True,separators=(comma,colon))',artifacts=artifacts,
         configuration='Release x64 MSVC /MT, C++20 adapter, PSE_M12A; current DOC-46 + DOC-47; no source modification'))
def tests():
    results=[]
    for name in ['r1_live','routing_contract','r2_fixed','admission_contract','pse_test_suite','pse_stress_test','pse_fault_injection','pse_bridge_acceptance']:
        exe=LAB/'build/Release'/(name+'.exe'); folder=OUT/('RUN_'+name);folder.mkdir()
        argv=[str(exe)]+(['r2'] if name=='r2_fixed' else [])
        with (folder/'console.log').open('xb') as f:
            r=subprocess.run(argv,cwd=folder,stdout=f,stderr=subprocess.STDOUT,timeout=180)
        row=dict(name=name,argv=argv,exit_code=r.returncode,exe_sha256=sha(exe));dump(folder/'RESULT.json',row)
        results.append(row);print(json.dumps(row),flush=True)
    dump(OUT/'REGRESSION.json',results);assert all(r['exit_code']==0 for r in results)
def integrity():
    b=json.loads((OUT/'BASELINE.json').read_text(encoding='utf8')); game=game_check()
    changed=[r['path'] for r in b['immutable'] if sha(ROOT/r['path'])!=r['sha256']]
    o=dict(utc=stamp(),immutable_checked=len(b['immutable']),changed=changed,game=game,
           protected_equal=game['protected']==b['game']['protected'])
    o['status']='PASS' if not changed and not game['errors'] and game['original_full_snapshot_equal'] and o['protected_equal'] else 'FAIL'
    dump(OUT/('INTEGRITY_'+stamp()+'.json'),o);print(json.dumps({k:v for k,v in o.items() if k!='game'}))
def r2_corrected():
    exe=LAB/'build/Release/r2_fixed.exe';folder=OUT/'RUN_r2_fixed_correct_argv';folder.mkdir()
    argv=[str(exe),'r2']
    with (folder/'console.log').open('xb') as f: r=subprocess.run(argv,cwd=folder,stdout=f,stderr=subprocess.STDOUT,timeout=60)
    dump(folder/'RESULT.json',dict(argv=argv,exit_code=r.returncode,exe_sha256=sha(exe),
         correction='Initial invocation omitted r2, executing historical R1 post-stop predicate; no test or product source changed'))
    print(r.returncode);assert r.returncode==0
if __name__=='__main__': {'baseline':baseline,'build':build,'tests':tests,'integrity':integrity,'r2_corrected':r2_corrected}[sys.argv[1]]()
