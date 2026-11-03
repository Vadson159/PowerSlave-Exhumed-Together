"""DOC-58B.2 workflow: baseline/integrity and bounded build/test commands. Every path stays inside the S: mod root.
Usage (python -X utf8 -B): workflow.py baseline | integrity <TAG>"""
from pathlib import Path
import os
ROOT=Path(r'S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod')
LAB=ROOT/'experiments/m1/native_actor_ownership_58b2'
OUT=ROOT/'docs/evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2'
SCRATCH=LAB/'scratch'
SCRATCH.mkdir(parents=True,exist_ok=True)
os.environ.update(TEMP=str(SCRATCH),TMP=str(SCRATCH),PYTHONDONTWRITEBYTECODE='1')
import datetime,hashlib,importlib.util,json,subprocess,sys,tempfile
tempfile.tempdir=str(SCRATCH)
EXPECTED='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
HOST_EXE=ROOT/'experiments/m0/game/anubis_Shipping_Steam_x64.exe'
CLIENT_EXE=ROOT/'experiments/m1/native_second_instance/game/anubis_Shipping_Steam_x64.exe'
OWN_DOC='docs/58B2_NATIVE_ACTOR_OWNERSHIP_UNLOAD.md'
ALLOWED_AT_END=['docs/PROGRESS.md','docs/EVIDENCE_INDEX.md']
SHORT_BUILD=ROOT/'b582'
def sha(p):
    h=hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def load(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def stamp():return datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
def inside(p):
    p=Path(p).resolve();assert p.is_relative_to(ROOT.resolve()),p;return p
def dump(p,d,overwrite=False):
    p=inside(p);p.parent.mkdir(parents=True,exist_ok=True)
    with p.open('w' if overwrite else 'x',encoding='utf8') as f:json.dump(d,f,indent=2,ensure_ascii=False)
def games():
    s=importlib.util.spec_from_file_location('accepted53_integrity',ROOT/'experiments/m1/background_host/audit.py')
    m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m.game_check()
SKIP={'game','profile','__pycache__','.git','build','scratch','build_gemini'}
def historical_paths():
    out=[]
    for part in ['docs','experiments','tools']:
        for p in (ROOT/part).rglob('*'):
            if not p.is_file() or p.is_relative_to(OUT) or p.is_relative_to(LAB):continue
            if SKIP.intersection(p.relative_to(ROOT).parts):continue
            if p.relative_to(ROOT).as_posix()==OWN_DOC:continue
            out.append(p)
    return sorted(out)
def snapshot(folder):
    folder=Path(folder)
    return dict(path=str(folder),exists=folder.exists(),files=[dict(path=p.relative_to(folder).as_posix(),size=p.stat().st_size,sha256=sha(p)) for p in sorted(folder.rglob('*')) if p.is_file()])
def protected_saves():
    prep=load(ROOT/'docs/evidence/m0/M0_001_PREPARATION.json')
    return list(dict.fromkeys([x['path'] for x in prep['protected_profiles_before']]))
def baseline():
    assert not (OUT/'BASELINE.json').exists()
    paths=historical_paths();g=games()
    assert not g['errors'] and not g['client_copy_errors'] and g['original_full_snapshot_equal'],g
    dump(OUT/'BASELINE.json',dict(utc=stamp(),files=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in paths],game=g,
        protected_saves=[snapshot(p) for p in protected_saves()],host_exe_sha256=sha(HOST_EXE),client_exe_sha256=sha(CLIENT_EXE),
        excluded='DOC-58B.2 lab/evidence, own document, build/scratch/profile/game/__pycache__ trees (game copies covered by game_check)'))
    for n in ['PROGRESS.md','EVIDENCE_INDEX.md']:(OUT/(n+'.before')).write_bytes((ROOT/'docs'/n).read_bytes())
    print('BASELINE PASS',len(paths),'historical files',flush=True)
def integrity(tag):
    b=load(OUT/'BASELINE.json');changed=[r['path'] for r in b['files'] if not (ROOT/r['path']).is_file() or sha(ROOT/r['path'])!=r['sha256']]
    unexpected=[p for p in changed if p not in ALLOWED_AT_END]
    known={r['path'] for r in b['files']}
    added=[p.relative_to(ROOT).as_posix() for p in historical_paths() if p.relative_to(ROOT).as_posix() not in known]
    g=games();saves=[snapshot(p) for p in protected_saves()]
    ok=not unexpected and not added and g==b['game'] and saves==b['protected_saves']
    r=dict(status='PASS' if ok else 'FAIL',changed=changed,allowed_changes=ALLOWED_AT_END,unexpected=unexpected,added_historical=added,
        game_equals_baseline=g==b['game'],protected_saves_equal=saves==b['protected_saves'],files=len(b['files']),
        host_exe_sha256=sha(HOST_EXE),client_exe_sha256=sha(CLIENT_EXE),exe_expected=EXPECTED,utc=stamp())
    dump(OUT/(tag+'.json'),r);print('INTEGRITY',tag,r['status'],len(b['files']),unexpected,added,flush=True);return r
def command(argv,tag,timeout=900,require=True,cwd=None):
    folder=OUT/'commands'/tag;folder.mkdir(parents=True,exist_ok=False)
    with (folder/'console.log').open('xb') as f:
        r=subprocess.run(list(map(str,argv)),cwd=str(cwd or folder),env=os.environ.copy(),stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
    d=dict(argv=list(map(str,argv)),cwd=str(cwd or folder),exit_code=r.returncode,log=str((folder/'console.log').relative_to(ROOT)),temp=str(SCRATCH),utc=stamp())
    if Path(str(argv[0])).is_file():d['binary_sha256']=sha(argv[0])
    dump(folder/'RESULT.json',d);print(tag,r.returncode,flush=True)
    if require:assert r.returncode==0,d
    return d
if __name__=='__main__':
    if sys.argv[1]=='baseline':baseline()
    elif sys.argv[1]=='integrity':integrity(sys.argv[2])
    else:raise ValueError(sys.argv[1])
