"""DOC58B workflow: baseline/integrity, bounded build/test commands. All paths confined to the S: project."""
from pathlib import Path
import os
ROOT=Path(r'S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod')
LAB=ROOT/'experiments/m1/native_network_visual_proxy_58b'
OUT=ROOT/'docs/evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B'
SCRATCH=LAB/'scratch'
SCRATCH.mkdir(parents=True,exist_ok=True)
os.environ.update(TEMP=str(SCRATCH),TMP=str(SCRATCH),PYTHONDONTWRITEBYTECODE='1')
import datetime,hashlib,importlib.util,json,subprocess,sys,tempfile
tempfile.tempdir=str(SCRATCH)
EXPECTED='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
CM='C:/Program Files/CMake/bin/cmake.exe'
NODE='C:/Program Files/nodejs/node.exe'
GEMINI=ROOT/'tools/network-visual-proxy-readiness/doc58d1-contract-audit'
MOVE58A=ROOT/'experiments/m1/native_visual_proxy_movement'
HOST_EXE=ROOT/'experiments/m0/game/anubis_Shipping_Steam_x64.exe'
CLIENT_EXE=ROOT/'experiments/m1/native_second_instance/game/anubis_Shipping_Steam_x64.exe'
def sha(p):
    h=hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def load(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def stamp():return datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
def inside(p):
    p=Path(p).resolve();assert p.is_relative_to(ROOT.resolve()),p;return p
def dump(p,d):
    p=inside(p);p.parent.mkdir(parents=True,exist_ok=True)
    with p.open('x',encoding='utf8') as f:json.dump(d,f,indent=2,ensure_ascii=False)
def games():
    s=importlib.util.spec_from_file_location('accepted53_integrity',ROOT/'experiments/m1/background_host/audit.py')
    m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m.game_check()
EXCLUDED_PARTS={'game','profile','__pycache__','.git','build','scratch'}
def historical_paths():
    paths=[]
    for part in ['docs','experiments','tools']:
        for p in (ROOT/part).rglob('*'):
            if not p.is_file() or p.is_relative_to(OUT) or p.is_relative_to(LAB):continue
            if EXCLUDED_PARTS.intersection(p.relative_to(ROOT).parts):continue
            paths.append(p)
    return sorted(paths)
def baseline():
    assert not (OUT/'BASELINE.json').exists()
    paths=historical_paths();g=games()
    assert not g['errors'] and not g['client_copy_errors'] and g['original_full_snapshot_equal'],g
    dump(OUT/'BASELINE.json',dict(files=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in paths],game=g,utc=stamp(),
        host_exe_sha256=sha(HOST_EXE),client_exe_sha256=sha(CLIENT_EXE),all_task_paths_on_s=True,
        excluded='new 58B lab/evidence and build/scratch/profile/game/__pycache__ trees (game copies covered by game_check)'))
    for n in ['PROGRESS.md','EVIDENCE_INDEX.md']:(OUT/(n+'.before')).write_bytes((ROOT/'docs'/n).read_bytes())
    print('BASELINE PASS',len(paths),'historical files',flush=True)
def integrity(tag='INTEGRITY'):
    b=load(OUT/'BASELINE.json');changed=[r['path'] for r in b['files'] if not (ROOT/r['path']).is_file() or sha(ROOT/r['path'])!=r['sha256']]
    allowed=['docs/PROGRESS.md','docs/EVIDENCE_INDEX.md']
    unexpected=[p for p in changed if p not in allowed]
    known={r['path'] for r in b['files']}
    added=[p.relative_to(ROOT).as_posix() for p in historical_paths() if p.relative_to(ROOT).as_posix() not in known]
    added_outside_task=[p for p in added if p not in ['docs/58B_NATIVE_NETWORK_VISUAL_PROXY_INTEGRATION.md']]
    g=games();ok=not unexpected and g==b['game'] and not added_outside_task
    result=dict(status='PASS' if ok else 'FAIL',changed=changed,allowed_changes=allowed,unexpected=unexpected,added=added,added_outside_task=added_outside_task,
        game=g,game_equals_baseline=g==b['game'],files=len(b['files']),host_exe_sha256=sha(HOST_EXE),client_exe_sha256=sha(CLIENT_EXE))
    dump(OUT/(tag+'_'+stamp()+'.json'),result)
    print('INTEGRITY',ok,len(b['files']),changed,added_outside_task,flush=True);return result
def command(argv,tag,timeout=600,require=True,cwd=None):
    folder=OUT/'commands'/(tag+'_'+stamp());folder.mkdir(parents=True)
    with (folder/'console.log').open('xb') as f:
        r=subprocess.run(list(map(str,argv)),cwd=str(cwd or folder),env=os.environ.copy(),stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
    d=dict(argv=list(map(str,argv)),cwd=str(cwd or folder),exit_code=r.returncode,log=str((folder/'console.log').relative_to(ROOT)),temp=str(SCRATCH),utc=stamp())
    if Path(str(argv[0])).is_file():d['binary_sha256']=sha(argv[0])
    dump(folder/'RESULT.json',d);print(json.dumps(d),flush=True)
    if require:assert r.returncode==0,d
    return d
if __name__=='__main__':
    {'baseline':baseline,'integrity':lambda:integrity()}[sys.argv[1]]()
