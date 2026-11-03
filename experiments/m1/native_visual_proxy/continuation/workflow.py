"""DOC57 continuation file/fixture workflow; all task paths are confined to S project."""
from pathlib import Path
import os
ROOT=Path(r'S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod')
PARENT=ROOT/'experiments/m1/native_visual_proxy'
LAB=PARENT/'continuation'
SCRATCH=PARENT/'scratch/continuation'
OUT=ROOT/'docs/evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01'
SCRATCH.mkdir(parents=True,exist_ok=True)
os.environ.update(TEMP=str(SCRATCH),TMP=str(SCRATCH),PYTHONDONTWRITEBYTECODE='1')
import datetime,hashlib,importlib.util,json,subprocess,sys,tempfile
tempfile.tempdir=str(SCRATCH)
EXPECTED='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
EXE=ROOT/'experiments/m0/game/anubis_Shipping_Steam_x64.exe'
KPF=ROOT/'experiments/m0/game/PowerslaveEX.kpf'
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
    s=importlib.util.spec_from_file_location('accepted57c_integrity',ROOT/'experiments/m1/background_host/audit.py')
    m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m.game_check()
def baseline():
    assert not (OUT/'BASELINE.json').exists()
    paths=[]
    for part in ['docs','experiments','tools']:
        for p in (ROOT/part).rglob('*'):
            if p.is_file() and not p.is_relative_to(OUT) and not p.is_relative_to(LAB) and not p.is_relative_to(PARENT/'scratch') and not {'game','profile','__pycache__','.git'}.intersection(p.relative_to(ROOT).parts):paths.append(p)
    # Include every historical failed-run profile artifact declared by its final manifest.
    old=load(OUT.parent/'FILES_FINAL.json')
    for r in old['files']:
        p=ROOT/r['path'];assert p.is_file() and sha(p)==r['sha256'],r['path'];paths.append(p)
    paths=sorted(set(paths));g=games()
    assert not g['errors'] and not g['client_copy_errors'] and g['original_full_snapshot_equal']
    dump(OUT/'BASELINE.json',dict(files=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in paths],game=g,utc=stamp(),exe_sha256=sha(EXE),all_task_paths_on_s=True))
    for n in ['PROGRESS.md','EVIDENCE_INDEX.md']:(OUT/(n+'.before')).write_bytes((ROOT/'docs'/n).read_bytes())
    print('BASELINE PASS',len(paths),'historical files',flush=True)
def command(argv,tag,timeout=240,require=True):
    folder=OUT/(tag+'_'+stamp());folder.mkdir(parents=True)
    with (folder/'console.log').open('xb') as f:r=subprocess.run(list(map(str,argv)),cwd=folder,env=os.environ.copy(),stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
    d=dict(argv=list(map(str,argv)),cwd=str(folder),exit_code=r.returncode,log=str(folder/'console.log'),temp=str(SCRATCH));dump(folder/'RESULT.json',d)
    print(json.dumps(d),flush=True)
    if require:assert r.returncode==0,d
    return d
def syntax():
    checks=[]
    for p in sorted(LAB.glob('*.js')):checks.append(command(['C:/Program Files/nodejs/node.exe','--check',p],'SYNTAX_'+p.stem))
    for p in LAB.glob('*.py'):compile(p.read_text(encoding='utf8'),str(p),'exec')
    dump(OUT/('SYNTAX_'+stamp()+'.json'),checks)
def integrity():
    b=load(OUT/'BASELINE.json');changed=[r['path'] for r in b['files'] if not (ROOT/r['path']).is_file() or sha(ROOT/r['path'])!=r['sha256']]
    unexpected=[p for p in changed if p not in ['docs/PROGRESS.md','docs/EVIDENCE_INDEX.md']]
    g=games();ok=not unexpected and g==b['game']
    dump(OUT/('INTEGRITY_'+stamp()+'.json'),dict(status='PASS' if ok else 'FAIL',changed=changed,unexpected=unexpected,game=g,game_equals_baseline=g==b['game'],files=len(b['files'])))
    print('INTEGRITY',ok,len(b['files']),changed,flush=True);assert ok
def fixture_bind():
    tag=sys.argv[2] if len(sys.argv)>2 else 'A01';assert tag.replace('_','').isalnum()
    ready=load(OUT/(tag+'_READY.json'));assert ready['kex'] is False and ready['temp']==str(SCRATCH)
    cfg=dict(pid=ready['pid'],scope='Python/CModule only; never KEX')
    text='const fixtureCfg='+json.dumps(cfg)+'; if(Process.id!==fixtureCfg.pid)throw Error("fixture PID mismatch");\n'+(LAB/'one_actor.js').read_text(encoding='utf8')+'\n'+(LAB/'harness_fixture.js').read_text(encoding='utf8')
    p=OUT/(tag+'_FIXTURE_BOUND.js')
    with p.open('x',encoding='utf8') as f:f.write(text)
    dump(OUT/(tag+'_FIXTURE_SOURCE.json'),dict(bound_sha256=sha(p),shared_state_sha256=sha(LAB/'one_actor.js'),fixture_sha256=sha(LAB/'harness_fixture.js'),target=ready))
    print(str(p),flush=True)
def fixture_done():
    tag=sys.argv[2] if len(sys.argv)>2 else 'A01';assert tag.replace('_','').isalnum()
    with (OUT/(tag+'_DONE')).open('x',encoding='ascii') as f:f.write('done\n')
if __name__=='__main__':{'baseline':baseline,'syntax':syntax,'integrity':integrity,'fixture_bind':fixture_bind,'fixture_done':fixture_done}[sys.argv[1]]()
