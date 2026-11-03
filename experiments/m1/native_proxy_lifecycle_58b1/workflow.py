"""DOC-58B.1: read-only audit, isolated output, no imports with historical write effects."""
from pathlib import Path
import os, sys, json, hashlib, datetime, subprocess, tempfile
ROOT=Path(r'S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod')
LAB=ROOT/'experiments/m1/native_proxy_lifecycle_58b1'
OUT=ROOT/'docs/evidence/m1/NATIVE_PROXY_LIFECYCLE_58B1'
SCRATCH=LAB/'scratch'
SCRATCH.mkdir(parents=True,exist_ok=True)
os.environ.update(TEMP=str(SCRATCH),TMP=str(SCRATCH),PYTHONDONTWRITEBYTECODE='1')
tempfile.tempdir=str(SCRATCH)
EXPECTED='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
def sha(p):
    h=hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def load(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def dump(p,d):
    p=Path(p).resolve();assert p.is_relative_to(ROOT)
    p.parent.mkdir(parents=True,exist_ok=True)
    with p.open('x',encoding='utf8') as f:json.dump(d,f,indent=2,ensure_ascii=False)
def stamp():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def snapshot(folder):
    folder=Path(folder)
    return dict(path=str(folder),exists=folder.exists(),files=[dict(path=p.relative_to(folder).as_posix(),size=p.stat().st_size,sha256=sha(p)) for p in sorted(folder.rglob('*')) if p.is_file()])
def protected():
    # Includes all frozen 58B evidence, sources, DLLs and previous milestones; excludes only caches/game/profile.
    skip={'build','scratch','profile','game','__pycache__','.git','build_gemini'}
    return [p for prefix in ('docs','experiments','tools') for p in (ROOT/prefix).rglob('*') if p.is_file() and not p.is_relative_to(LAB) and not p.is_relative_to(OUT) and not skip.intersection(p.relative_to(ROOT).parts) and p.name!='58B1_NATIVE_LEVEL_UNLOAD_RECONNECT.md']
def baseline():
    prep=load(ROOT/'docs/evidence/m0/M0_001_PREPARATION.json')
    roots=list(dict.fromkeys([prep['source']]+[x['path'] for x in prep['protected_profiles_before']]))
    d=dict(utc=stamp(),files=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in sorted(protected())],external=[snapshot(p) for p in roots])
    dump(OUT/'BASELINE.json',d);print('baseline',len(d['files']),[(r['path'],len(r['files'])) for r in d['external']])
def command(argv,tag,timeout=300,require=True):
    folder=OUT/'commands'/tag;folder.mkdir(parents=True,exist_ok=False)
    with (folder/'console.log').open('xb') as f:r=subprocess.run(list(map(str,argv)),cwd=folder,env=os.environ.copy(),stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
    d=dict(argv=list(map(str,argv)),cwd=str(folder),exit_code=r.returncode,log=str(folder/'console.log'),temp=str(SCRATCH),utc=stamp())
    dump(folder/'RESULT.json',d);print(tag,r.returncode,flush=True)
    if require:assert r.returncode==0,d
    return d
if __name__=='__main__':
    if sys.argv[1]=='baseline':baseline()
