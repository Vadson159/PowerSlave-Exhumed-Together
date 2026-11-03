"""DOC57 file-only preparation, exact-build audit, regression execution and integrity.

Does not launch or instrument a game. Reuses accepted source and binary artifacts.
"""
from pathlib import Path
import datetime, difflib, hashlib, importlib.util, json, os, shutil, struct, subprocess, sys, zipfile

ROOT = Path(r'S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod')
LAB = ROOT/'experiments/m1/native_visual_proxy'
OUT = ROOT/'docs/evidence/m1/NATIVE_VISUAL_PROXY_57'
STAGE = Path(r'C:\Users\vadso\doc57_work\stage')
EXPECTED = 'b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
EXE = ROOT/'experiments/m0/game/anubis_Shipping_Steam_x64.exe'
KPF = ROOT/'experiments/m0/game/PowerslaveEX.kpf'

def sha(p):
    h=hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def stamp():return datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
def dump(p,o):
    p.parent.mkdir(parents=True,exist_ok=True)
    with p.open('x',encoding='utf8') as f:json.dump(o,f,indent=2,ensure_ascii=False)
def load(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def games():
    spec=importlib.util.spec_from_file_location('accepted57_game_audit',ROOT/'experiments/m1/background_host/audit.py')
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
    return m.game_check()
def baseline():
    assert not (OUT/'BASELINE.json').exists()
    paths=[]
    for name in ['docs','experiments','tools']:
        for p in (ROOT/name).rglob('*'):
            if p.is_file() and not p.is_relative_to(OUT) and not p.is_relative_to(LAB) and not {'game','profile','__pycache__','.git'}.intersection(p.relative_to(ROOT).parts):paths.append(p)
    g=games()
    assert not g['errors'] and not g['client_copy_errors'] and g['original_full_snapshot_equal'],g
    dump(OUT/'BASELINE.json',dict(utc=stamp(),files=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in sorted(paths)],game=g,git_repository=False))
    for n in ['PROGRESS.md','EVIDENCE_INDEX.md']:
        shutil.copy2(ROOT/'docs'/n,OUT/(n+'.before'))
    print('Baseline:',len(paths),'files',flush=True)
def sync():
    if not (OUT/'BASELINE.json').exists():baseline()
    prefixes={
      'PROGRESS.md':'''## DOC-57 — Native Visual Proxy Step 0 BLOCKED; остановка

[DOC57](57_NATIVE_VISUAL_PROXY_STEP0.md), [итог](evidence/m1/NATIVE_VISUAL_PROXY_57/RESULT_SUMMARY.json), [read-only witness](evidence/m1/NATIVE_VISUAL_PROXY_57/FAILURE_READONLY_WITNESS.json). Один KEX32064/Sim32172, exact EXE, свежий профиль. Factory1007 действительно создала отдельный stock kexSlaveActor/node в native sector259, flags0x40/refs0; original P1 graph/position/health200 совпали в endpoint observations. Probe ошибочно потребовал nested Frida factory listener до записи returned pointer, остановился; suppression воспроизведено отдельно на Python/CModule, включая traps:'all'. На настоящем кадре дополнительный sprite не подтверждён. Actor ticks>=120, P1 control/inventory, native Remove и deferred retirement UNKNOWN; повторного spawn/run не было.

Обычный WM_CLOSE, same retained+duplicate process handles wait0/exit0, все15 REA sessions закрыты, финальных KEX/task processes0. Все15 существующих regression commands заново exit0: suite46, stress11/fault16/bridge11, routing330/admission112/timeout/sequence282+12/background37/native-client134/snapshot42/mapready210. Native PASS из этих тестов не выводится. [Integrity](evidence/m1/NATIVE_VISUAL_PROXY_57/INTEGRITY_FINAL.json): historical/source/build baseline16368, только PROGRESS/EVIDENCE_INDEX обновлены; original/copies/protected unchanged. Новая DLL/инжектор/ENet/Player/camera отсутствуют, production C++ и DOC53/55/56 contracts не менялись. Следующий gate — исправить и закончить DOC57; DOC58 не готов и не начат.
''',
      'EVIDENCE_INDEX.md':'''## DOC-57 — Native Visual Proxy Step 0 BLOCKED

| Evidence | Назначение |
|---|---|
|[DOC57](57_NATIVE_VISUAL_PROXY_STEP0.md), [RESULT_SUMMARY](evidence/m1/NATIVE_VISUAL_PROXY_57/RESULT_SUMMARY.json)|Один stopped native run: Actor создан, visual/120ticks/Remove/retire не приняты; явные UNKNOWN и handoff|
|[STATIC_AUDIT](evidence/m1/NATIVE_VISUAL_PROXY_57/STATIC_AUDIT.json), [runtime coordinates](evidence/m1/NATIVE_VISUAL_PROXY_57/RUNTIME_COORDINATES.json), [dossiers](evidence/m1/NATIVE_VISUAL_PROXY_57/static/)|13 exact-build native seams, fifth stack argument, independent raw-byte anchors, Actor/Puppet/node/sector separation; corrections Gemini flags/renderer/destructor|
|[Resources](evidence/m1/NATIVE_VISUAL_PROXY_57/resources/), [lab/source](../experiments/m1/native_visual_proxy/README.md)|Stock BodyRest1007, static one-frame/passive properties; failed experimental source, no new DLL/injector|
|[Scout](evidence/m1/NATIVE_VISUAL_PROXY_57/N57_STEP0_01/scout.jsonl), [native journal](evidence/m1/NATIVE_VISUAL_PROXY_57/N57_STEP0_01/native.jsonl), [failure witness](evidence/m1/NATIVE_VISUAL_PROXY_57/FAILURE_READONLY_WITNESS.json)|Tomb/geometry/list preflight, one native call/false block, separate Actor/node/flags40/refs0 and preserved P1 endpoints|
|[Before frame](evidence/m1/NATIVE_VISUAL_PROXY_57/N57_STEP0_01/window_20261103_094235_233518.png), [after frame](evidence/m1/NATIVE_VISUAL_PROXY_57/N57_STEP0_01/window_20261103_094304_434253.png)|Real owned-window frames; additional sprite NOT demonstrated|
|[OS handle](evidence/m1/NATIVE_VISUAL_PROXY_57/N57_STEP0_01/OS_HANDLE_IDENTITY.json), [exit0](evidence/m1/NATIVE_VISUAL_PROXY_57/N57_STEP0_01/OS_SIGNALED.json), [cleanup](evidence/m1/NATIVE_VISUAL_PROXY_57/SESSIONS_CLOSED.json)|Retained/duplicate same object, WM_CLOSE normal exit, 13 task+2 benign sessions closed; no forced termination|
|[REA complete](evidence/m1/NATIVE_VISUAL_PROXY_57/REA_05_FIXTURES.json), [tool fixtures](evidence/m1/NATIVE_VISUAL_PROXY_57/TOOL_FIXTURES.json), [errors](evidence/m1/NATIVE_VISUAL_PROXY_57/HARNESS_ERRORS.md)|224 native-session messages/no truncation; nested listener suppression separately reproduced without KEX; errors retained|
|[Regressions](evidence/m1/NATIVE_VISUAL_PROXY_57/REGRESSIONS_20261103_093001_316289.json), [actual stdout summaries](evidence/m1/NATIVE_VISUAL_PROXY_57/REGRESSION_SUMMARIES.json)|15 fresh executions with argv/cwd/exits/binary SHA, all exit0; no mock-to-native PASS substitution|
|[Baseline](evidence/m1/NATIVE_VISUAL_PROXY_57/BASELINE.json), [integrity](evidence/m1/NATIVE_VISUAL_PROXY_57/INTEGRITY_FINAL.json), [diff](evidence/m1/NATIVE_VISUAL_PROXY_57/SOURCE_CHANGES_FINAL.patch), [files](evidence/m1/NATIVE_VISUAL_PROXY_57/FILES_FINAL.json)|16368 historical/source/build hashes, accepted docs/artifacts and game/profiles integrity, complete task change list|
'''}
    for name,section in prefixes.items():
        dst=STAGE/'docs'/name
        if not dst.exists() and (STAGE/'docs/57_NATIVE_VISUAL_PROXY_STEP0.md').exists():
            data=(ROOT/'docs'/name).read_bytes();nl=b'\r\n' if b'\r\n' in data else b'\n';at=data.index(b'\n')+1
            dst.parent.mkdir(parents=True,exist_ok=True)
            dst.write_bytes(data[:at]+nl+section.encode('utf8').replace(b'\n',nl)+nl+data[at:])
    copied=[]
    for p in STAGE.rglob('*'):
        if not p.is_file():continue
        rel=p.relative_to(STAGE)
        evidence=rel.as_posix().startswith('docs/evidence/m1/NATIVE_VISUAL_PROXY_57/')
        allowed=evidence or rel.as_posix().startswith('experiments/m1/native_visual_proxy/') or rel.as_posix() in ['docs/57_NATIVE_VISUAL_PROXY_STEP0.md','docs/PROGRESS.md','docs/EVIDENCE_INDEX.md']
        assert allowed and '..' not in rel.parts,rel
        dst=ROOT/rel;dst.parent.mkdir(parents=True,exist_ok=True)
        if evidence and dst.exists():assert sha(dst)==sha(p),'Never overwrite recorded Evidence: '+str(dst)
        if not dst.exists() or sha(dst)!=sha(p):shutil.copy2(p,dst)
        copied.append(dict(path=rel.as_posix(),sha256=sha(dst)))
    dest=LAB/'workflow.py';dest.parent.mkdir(parents=True,exist_ok=True)
    if Path(__file__).resolve()!=dest.resolve():shutil.copy2(__file__,dest)
    dump(OUT/('SYNC_'+stamp()+'.json'),copied)
    print('Synchronized',len(copied),'scoped files',flush=True)
def pe():
    b=EXE.read_bytes();assert hashlib.sha256(b).hexdigest()==EXPECTED
    h=struct.unpack_from('<I',b,0x3c)[0];count=struct.unpack_from('<H',b,h+6)[0]
    opts=struct.unpack_from('<H',b,h+20)[0];base=struct.unpack_from('<Q',b,h+24+24)[0]
    sect=[]
    for i in range(count):
        o=h+24+opts+i*40;vs,rva,rs,off=struct.unpack_from('<IIII',b,o+8)
        sect.append((rva,rs,off))
    def off(rva):
        for v,n,f in sect:
            if v<=rva<v+n:return f+rva-v
        raise ValueError(hex(rva))
    return b,base,off
def audit():
    b,base,off=pe();rows=[]
    sites={'factory':0x1d4780,'base_spawn':0x1a02f0,'puppet_spawn':0x1dbc60,'link_sector':0x1d3d70,'remove':0x1a02b0,'actor_tick':0x19f2d0,'on_remove':0x195000,'pool_return':0x1d5000,'sweep':0x1b6360,'drain':0x1b61d0,'tick':0x1b6ec0,'update':0x1dfe30,'state_exit':0x1b6670}
    dossiers=[]
    for name,rva in sites.items():
        f=ROOT/f'docs/evidence/ghidra_2022/G_FUNC_{base+rva:x}.json'
        d=load(f);assert d['binary_sha256']==EXPECTED
        dst=OUT/'static'/f.name;dst.parent.mkdir(parents=True,exist_ok=True)
        if not dst.exists():shutil.copy2(f,dst)
        rows.append(dict(name=name,rva=hex(rva),preferred_va=hex(base+rva),runtime_va=None,file_offset=hex(off(rva)),prefix16=b[off(rva):off(rva)+16].hex(),body_bytes=d['body_bytes'],dossier=dst.relative_to(ROOT).as_posix(),dossier_sha256=sha(dst)))
    anchors=[('factory fifth arg: entry RSP+28 after prologue delta58 => RSP+80',0x1d4adf,'8b942480000000'),('moveable low-byte sign test',0x19f540,'4084ff0f8988080000'),('fullbright ActorDef bit20',0x1acb93,'83c920'),('hidden ActorDef bit800',0x1acd1d,'0fbae90b')]
    checked=[]
    for label,rva,expected in anchors:
        actual=b[off(rva):off(rva)+len(bytes.fromhex(expected))].hex()
        assert actual==expected,(label,actual,expected)
        checked.append(dict(label=label,rva=hex(rva),file_offset=hex(off(rva)),bytes=actual))
    vt=[hex(struct.unpack_from('<Q',b,off(0x528608)+i*8)[0]-base) for i in range(19)]
    assert vt[4]=='0x19f2d0' and vt[5]=='0x195000' and vt[6]=='0x1a02b0' and vt[7]=='0x1a02f0'
    resources=[]
    with zipfile.ZipFile(KPF) as z:
        for n in ['defs/defines.txt','defs/actors/misc.txt','sprites/misc/ramses_body_anim.txt','sprites/misc/ramses_body_rest_sprite.txt']:
            data=z.read(n);dest=OUT/'resources'/n;dest.parent.mkdir(parents=True,exist_ok=True)
            if not dest.exists():dest.write_bytes(data)
            resources.append(dict(member=n,sha256=hashlib.sha256(data).hexdigest(),bytes=len(data)))
        resources.append(dict(member='sprites/misc/ramses_body_rest.png',sha256=hashlib.sha256(z.read('sprites/misc/ramses_body_rest.png')).hexdigest()))
    dump(OUT/'STATIC_AUDIT.json',dict(exe_sha256=sha(EXE),kpf_sha256=sha(KPF),preferred_base=hex(base),runtime_base=None,sites=rows,anchors=checked,actor_vtable_rva='0x528608',vtable_slots_rva=vt,resources=resources,factory_abi=dict(convention='Windows x64',rcx='factory this (body uses global definitions/pools)',edx='signed int actorType',r8='const Vec3*',xmm3='float yaw',entry_rsp_28='signed int sector',caller_rsp_20='sector before CALL',return_value='RAX actor pointer, not ObjectNode; no returned owning reference'),native_executed=False))
    dump(LAB/'sites.json',{r['name']:dict(rva=int(r['rva'],16),prefix=r['prefix16']) for r in rows})
    print('Static audit PASS:',len(rows),'sites;',len(anchors),'independent byte anchors',flush=True)
def regressions():
    historical=load(ROOT/'docs/evidence/m1/NATIVE_MAP_READY_56/REGRESSIONS_20261103_013004_356903.json')
    results=[]
    for row in historical:
        argv=row['argv'];assert sha(argv[0])==row['binary_sha256']
        name=Path(argv[0]).stem;folder=OUT/('TEST_'+name+'_'+stamp());folder.mkdir()
        with (folder/'console.log').open('xb') as log:
            r=subprocess.run(argv,cwd=folder,stdout=log,stderr=subprocess.STDOUT,timeout=240)
        value=dict(argv=argv,cwd=str(folder),exit_code=r.returncode,binary_sha256=sha(argv[0]),log=(folder/'console.log').relative_to(ROOT).as_posix(),new_execution=True,timer_fixture=False)
        dump(folder/'RESULT.json',value);results.append(value);print(name,r.returncode,flush=True)
    dump(OUT/('REGRESSIONS_'+stamp()+'.json'),results)
    assert all(v['exit_code']==0 for v in results)
def integrity():
    b=load(OUT/'BASELINE.json');changed=[]
    for r in b['files']:
        p=ROOT/r['path']
        if not p.is_file() or sha(p)!=r['sha256']:changed.append(r['path'])
    allowed=['docs/PROGRESS.md','docs/EVIDENCE_INDEX.md']
    unexpected=[p for p in changed if p not in allowed]
    g=games();same=g==b['game']
    result=dict(changed=changed,unexpected=unexpected,game=g,game_equals_baseline=same,baseline_files=len(b['files']))
    dump(OUT/('INTEGRITY_'+stamp()+'.json'),result)
    assert not unexpected and same,result
    print('Integrity PASS',len(b['files']),'historical/source/build files',flush=True)
def manifest():
    files=[];patch=[]
    for p in sorted(LAB.rglob('*')):
        if p.is_file() and not {'profile','__pycache__'}.intersection(p.relative_to(LAB).parts):
            rel=p.relative_to(ROOT).as_posix();files.append(dict(path=rel,sha256=sha(p)))
            if p.suffix in ['.py','.js','.md','.json']:patch.extend(difflib.unified_diff([],p.read_text(encoding='utf8').splitlines(True),fromfile='/dev/null',tofile=rel))
    dump(OUT/('SOURCE_MANIFEST_'+stamp()+'.json'),dict(files=files,exe_sha256=sha(EXE),kpf_sha256=sha(KPF),experimental_native_dll=None,reason='REA Frida directly calls exact-build native factory/remove on attested simulation thread; existing accepted injector/isolation reused'))
    (OUT/('SOURCE_CHANGES_'+stamp()+'.patch')).write_text(''.join(patch),encoding='utf8')
    print('Manifest:',len(files),'files',flush=True)
def ready():
    audit()
    checks=[]
    for p in LAB.glob('*.js'):
        r=subprocess.run(['C:/Program Files/nodejs/node.exe','--check',str(p)],capture_output=True,text=True)
        checks.append(dict(path=p.name,exit_code=r.returncode,stdout=r.stdout,stderr=r.stderr))
    for p in LAB.glob('*.py'):compile(p.read_text(encoding='utf8'),str(p),'exec')
    dump(OUT/('SYNTAX_'+stamp()+'.json'),checks);assert all(r['exit_code']==0 for r in checks)
    manifest();regressions()
    dump(OUT/'AUTONOMOUS_GATE.json',dict(status='PASS',scope='Static byte/resources audit, syntax and newly executed existing foundation regression binaries; no native gameplay claimed',source_frozen_before_game=True))
def finalize():
    assert load(OUT/'RESULT_SUMMARY.json')['verdict']=='BLOCKED'
    # Regenerate only this task's computed manifests after document QA. Preserve
    # their previous bytes first; raw observations and historical inputs stay immutable.
    archive=OUT/('computed_before_'+stamp())
    for name in ['INTEGRITY_FINAL.json','SOURCE_CHANGES_FINAL.patch','CHANGED_FILES.json','FILES_FINAL.json']:
        p=OUT/name
        if p.exists():
            assert p.resolve().is_relative_to(OUT.resolve())
            archive.mkdir(exist_ok=True);shutil.copy2(p,archive/name)
            assert sha(p)==sha(archive/name);p.unlink()
    manifest();integrity()
    final_integrity=OUT/'INTEGRITY_FINAL.json';assert not final_integrity.exists()
    shutil.copy2(sorted(OUT.glob('INTEGRITY_2*.json'))[-1],final_integrity)
    baseline=load(OUT/'BASELINE.json')
    changed=[r['path'] for r in baseline['files'] if not (ROOT/r['path']).exists() or sha(ROOT/r['path'])!=r['sha256']]
    patch=[]
    for n in ['PROGRESS.md','EVIDENCE_INDEX.md']:
        patch.extend(difflib.unified_diff((OUT/(n+'.before')).read_text(encoding='utf8').splitlines(True),(ROOT/'docs'/n).read_text(encoding='utf8').splitlines(True),fromfile='before/docs/'+n,tofile='docs/'+n))
    source_paths=[ROOT/'docs/57_NATIVE_VISUAL_PROXY_STEP0.md']+[p for p in sorted(LAB.rglob('*')) if p.is_file() and not {'profile','__pycache__'}.intersection(p.relative_to(LAB).parts)]
    for p in source_paths:
        if p.suffix in ['.py','.js','.md','.json']:
            patch.extend(difflib.unified_diff([],p.read_text(encoding='utf8').splitlines(True),fromfile='/dev/null',tofile=p.relative_to(ROOT).as_posix()))
    (OUT/'SOURCE_CHANGES_FINAL.patch').write_text(''.join(patch),encoding='utf8')
    dump(OUT/'CHANGED_FILES.json',dict(changed_existing=changed,new_document='docs/57_NATIVE_VISUAL_PROXY_STEP0.md',new_lab=[p.relative_to(ROOT).as_posix() for p in LAB.rglob('*') if p.is_file()],new_evidence=[p.relative_to(ROOT).as_posix() for p in OUT.rglob('*') if p.is_file()],new_native_dll=None,production_cpp_changes=[]))
    paths=sorted(set(source_paths+[ROOT/'docs/PROGRESS.md',ROOT/'docs/EVIDENCE_INDEX.md']+[p for p in OUT.rglob('*') if p.is_file()]+[p for p in (LAB/'profile').rglob('*') if p.is_file()]))
    dump(OUT/'FILES_FINAL.json',dict(note='Manifest excludes itself; other listed artifacts hashed after finalization',files=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p),size=p.stat().st_size) for p in paths]))
    print('Finalized BLOCKED evidence; existing changes:',changed,flush=True)
if __name__=='__main__':
    {'sync':sync,'audit':audit,'tests':regressions,'integrity':integrity,'manifest':manifest,'ready':ready,'finalize':finalize}[sys.argv[1]]()
