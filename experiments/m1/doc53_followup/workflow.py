"""DOC53 follow-up: immutable baseline, bounded commands and default-timer traces."""
from pathlib import Path
import ctypes, datetime, difflib, hashlib, importlib.util, json, shutil, subprocess, sys
LAB=Path(__file__).resolve().parent
ROOT=LAB.parents[2]
OUT=ROOT/'docs/evidence/m1/DOC53_FOLLOWUP'
CM='C:/Program Files/CMake/bin/cmake.exe'
TARGETS=['pse_m12a','pse_native_client','background_contract','pse_test_suite','pse_stress_test','pse_fault_injection','pse_bridge_acceptance','r1_live','r2_fixed','routing_contract','admission_contract','timeout_contract','sequence_contract','sequence_live','native_client_contract']
def stamp():return datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
def sha(p):
    h=hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def dump(p,o):
    p.parent.mkdir(parents=True,exist_ok=True)
    with p.open('x',encoding='utf8') as f:json.dump(o,f,ensure_ascii=False,indent=2)
def game_check():
    spec=importlib.util.spec_from_file_location('audit53',LAB.parent/'background_host/audit.py')
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
    return m.game_check()
def timer():
    vals=[ctypes.c_ulong() for _ in range(3)]
    fn=ctypes.WinDLL('ntdll').NtQueryTimerResolution
    fn.argtypes=[ctypes.POINTER(ctypes.c_ulong)]*3;fn.restype=ctypes.c_long
    rc=fn(*[ctypes.byref(v) for v in vals])
    return dict(ntstatus=rc,maximum_100ns=vals[0].value,minimum_100ns=vals[1].value,current_100ns=vals[2].value,changes_requested=False)
def command(argv,tag,timeout=240,require=True):
    folder=OUT/(tag+'_'+stamp());folder.mkdir(parents=True)
    before=timer()
    with (folder/'console.log').open('xb') as f:
        r=subprocess.run(list(map(str,argv)),cwd=folder,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
    o=dict(argv=list(map(str,argv)),cwd=str(folder),exit_code=r.returncode,log=str((folder/'console.log').relative_to(ROOT)),timer_before=before,timer_after=timer())
    if Path(argv[0]).is_file():o['binary_sha256']=sha(argv[0])
    dump(folder/'RESULT.json',o);print(json.dumps(o),flush=True)
    if require:assert r.returncode==0
    return o
def baseline():
    paths=set()
    for d in ['docs/evidence','experiments/m0','experiments/m1','tools']:
        for p in (ROOT/d).rglob('*'):
            if p.is_file() and not p.is_relative_to(OUT) and not p.is_relative_to(LAB) and not {'__pycache__','game','profile','.git'}.intersection(p.relative_to(ROOT/d).parts):paths.add(p)
    paths.update((ROOT/'docs').glob('*.md'))
    for n in ['PROGRESS.md','EVIDENCE_INDEX.md']:paths.discard(ROOT/'docs'/n)
    g=game_check();assert not g['errors'] and not g['client_copy_errors'] and g['original_full_snapshot_equal']
    dump(OUT/'BASELINE.json',dict(utc=stamp(),files=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in sorted(paths)],game=g,timer=timer()))
    for p in paths:
        if (p.is_relative_to(ROOT/'tools/enet-prototype') or p.is_relative_to(ROOT/'experiments/m1')) and p.suffix in ['.cpp','.hpp','.h','.txt','.py'] and not {'build','third_party','qa_enet'}.intersection(p.parts):
            dst=OUT/'before'/p.relative_to(ROOT);dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dst)
    for n in ['PROGRESS.md','EVIDENCE_INDEX.md']:shutil.copy2(ROOT/'docs'/n,OUT/(n+'.before'))
    print('BASELINE PASS',len(paths),'files',flush=True)
def generate_trace():
    folder=LAB/'generated';folder.mkdir(exist_ok=True)
    s=(OUT/'before/tools/enet-prototype/tests/test_suite.cpp').read_text(encoding='utf8')
    s=s.replace('static std::vector<TestResult> g_results;', '''static bool traceActive=false;
static auto traceStart=std::chrono::steady_clock::now();
static double traceMs(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-traceStart).count();}
static std::vector<TestResult> g_results;''')
    s=s.replace('        host.poll(1);','''        auto pollStart=std::chrono::steady_clock::now();
        host.poll(1);
        auto hostDone=std::chrono::steady_clock::now();''')
    s=s.replace('        if (elapsed >= durationMs) break;', '''        if(traceActive) std::cout << "TRACE pump ms=" << traceMs() << " hostMs=" << std::chrono::duration<double,std::milli>(hostDone-pollStart).count() << " clientsMs=" << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-hostDone).count() << " duration=" << durationMs << " elapsed=" << elapsed << std::endl;
        if (elapsed >= durationMs) break;''')
    s=s.replace('        // Inflict lethal damage','''        traceActive=true;traceStart=std::chrono::steady_clock::now();
        clients[0]->onSnapshotCallback=[&](const MsgPlayerSnapshot& snap){
            auto* state=clients[0]->getLocalPlayerState();
            std::cout << "TRACE snapshot ms=" << traceMs() << " seq=" << snap.snapshotSequence << " tick=" << snap.serverTick << " hp=" << (state?state->health:-1) << " life=" << (state?int(state->lifeState):-1) << std::endl;
        };
        clients[0]->onRespawnConfirmCallback=[&](const MsgRespawnConfirm& conf){
            auto* state=clients[0]->getLocalPlayerState();
            std::cout << "TRACE confirm ms=" << traceMs() << " epoch=" << conf.mapEpoch << " death=" << conf.deathSequence << " entityGen=" << conf.entityGeneration << " hp=" << (state?state->health:-1) << " life=" << (state?int(state->lifeState):-1) << std::endl;
        };
        // Inflict lethal damage''')
    # Extra bounded observation follows test9 assertions, preserving their failures.
    marker='    // Scenario 10: Map Epoch Advancement & Map Synchronization'
    s=s.replace(marker,'''    // Diagnostic continuation only: assertions 8/9 already recorded.
    pumpNetwork(host,clients,1200);
    traceActive=false;
'''+marker)
    (folder/'trace_suite.cpp').write_text(s,encoding='utf8')
    coarse=s.replace('host.poll(1);','host.poll(traceActive?16:1);').replace('c->poll(1);','c->poll(traceActive?16:1);').replace('std::chrono::milliseconds(1));','std::chrono::milliseconds(traceActive?16:1));')
    (folder/'coarse_suite.cpp').write_text(coarse,encoding='utf8')
    c=(ROOT/'tools/enet-prototype/src/enet_client.cpp').read_text(encoding='utf8')
    c=c.replace('        m_respawnRequested = false;','''        std::cout << "TRACE respawnRequested " << m_respawnRequested << " -> false on confirm slot=" << int(confirm.slotId) << std::endl;
        m_respawnRequested = false;''')
    c=c.replace('    m_respawnRequested = true;','''    std::cout << "TRACE respawnRequested false -> true request death=" << localState->deathSequence << " snapshotSeq=" << m_lastSnapshot.snapshotSequence << std::endl;
    m_respawnRequested = true;''')
    (folder/'trace_client.cpp').write_text(c,encoding='utf8')
    h=(ROOT/'tools/enet-prototype/src/enet_host.cpp').read_text(encoding='utf8')
    h=h.replace('namespace pse {','namespace pse {\nstatic uint64_t diagnosticSnapshotNotBefore=0;',1)
    h=h.replace('void ENetHostServer::broadcastSnapshot() {','void ENetHostServer::broadcastSnapshot() {\n    if(getSteadyTimeMs()<diagnosticSnapshotNotBefore)return;')
    h=h.replace('        // Send confirmation', '        // Send confirmation') # No timing assumption about source comment.
    needle='    if (res == WorldSimulation::RespawnResult::SUCCESS) {'
    assert needle in h
    h=h.replace(needle,needle+'\n        diagnosticSnapshotNotBefore=getSteadyTimeMs()+550;\n        std::cout << "TRACE controlled snapshot-only delay=550ms, host state already ALIVE" << std::endl;')
    (folder/'trace_host.cpp').write_text(h,encoding='utf8')
    (folder/'fixed_suite.cpp').write_bytes((ROOT/'tools/enet-prototype/tests/test_suite.cpp').read_bytes())
    dump(OUT/('TRACE_SOURCE_'+stamp()+'.json'),dict(files=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in folder.glob('*.cpp')],production_sources_modified=False))
def build():
    command([CM,'-S',LAB,'-B',LAB/'build','-G','Visual Studio 17 2022','-A','x64','-DCMAKE_SYSTEM_VERSION=10.0.26100.0'],'CONFIGURE')
    names=sys.argv[2:] or TARGETS
    suites={'pse_test_suite','pse_stress_test','pse_fault_injection','pse_bridge_acceptance'}
    main=[n for n in names if n not in suites]
    if main:command([CM,'--build',LAB/'build','--config','Release','--parallel','2','--target']+main,'BUILD')
    nested=[n for n in names if n in suites]
    if nested:command([CM,'--build',LAB/'build/integration/integration/integration/transport','--config','Release','--parallel','2','--target']+nested,'BUILD_SUITES')
def tests():
    rows=[]
    for n in sys.argv[2:] or TARGETS[2:]:
        rows.append(command([LAB/'build/Release'/(n+'.exe')]+(['r2'] if n=='r2_fixed' else []),'TEST_'+n,require=False))
    dump(OUT/('REGRESSIONS_'+stamp()+'.json'),rows)
    assert all(r['exit_code']==0 for r in rows)
def integrity():
    b=json.loads((OUT/'BASELINE.json').read_text(encoding='utf8'))
    changed=[r['path'] for r in b['files'] if not (ROOT/r['path']).is_file() or sha(ROOT/r['path'])!=r['sha256']]
    dump(OUT/('CHANGED_'+stamp()+'.json'),dict(changed=changed,game=game_check()))
    print(json.dumps(changed,indent=2))
def freeze():
    records=[];artifacts=[]
    for d in ['experiments/m1/native_bootstrap','experiments/m1/network_input','experiments/m1/native_second_instance','tools/enet-prototype']:
        for p in sorted((ROOT/d).rglob('*')):
            if not p.is_file() or {'build','profile','game','__pycache__','.git','field-test'}.intersection(p.relative_to(ROOT/d).parts):continue
            if p.suffix not in ['.cpp','.c','.h','.hpp','.txt']:continue
            dst=OUT/'frozen_sources'/p.relative_to(ROOT);dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dst)
            records.append(dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)))
    for name in ['pse_m11_bootstrap.dll','pse_m11_bootstrap.pdb','pse_native_client.dll','pse_native_client.pdb']:
        source=LAB/'build/Release'/name
        if name.startswith('pse_native_client'):source=ROOT/'docs/evidence/m1/NATIVE_SECOND_INSTANCE_51/artifacts_MAIN'/name
        dst=OUT/'artifacts'/name;dst.parent.mkdir(exist_ok=True);shutil.copy2(source,dst)
        artifacts.append(dict(path=dst.relative_to(ROOT).as_posix(),sha256=sha(dst),size=dst.stat().st_size))
    dump(OUT/'BUILD_MANIFEST.json',dict(sources=records,artifacts=artifacts,client_retained_doc51=True,host_changes='Read-only input telemetry only',exe_sha256='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'))
    changes=[]
    for rel in ['tools/enet-prototype/tests/test_suite.cpp','tools/enet-prototype/tests/test_bridge_acceptance.cpp','experiments/m1/native_bootstrap/src/bootstrap.cpp']:
        changes+=list(difflib.unified_diff((OUT/'before'/rel).read_text(encoding='utf8').splitlines(True),(ROOT/rel).read_text(encoding='utf8').splitlines(True),fromfile='before/'+rel,tofile=rel))
    (OUT/'SOURCE_CHANGES.patch').write_text(''.join(changes),encoding='utf8')
    print('FROZEN',len(records),'sources')
def gate():
    rows=[]
    for name in TARGETS[2:]:
        folder=sorted(OUT.glob('TEST_'+name+'_*'))[-1]
        o=json.loads((folder/'RESULT.json').read_text(encoding='utf8'));assert o['exit_code']==0,(name,o)
        rows.append(o)
    manifest=json.loads((OUT/'BUILD_MANIFEST.json').read_text(encoding='utf8'))
    for r in manifest['sources']:assert sha(ROOT/r['path'])==r['sha256']
    dll=OUT/'artifacts/pse_m11_bootstrap.dll';lib=ctypes.WinDLL(str(dll));fn=lib.M11Prepare
    fn.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32];fn.restype=ctypes.c_uint32
    path=OUT/'MUST_NOT_EXIST.jsonl';rc=fn(str(path),ctypes.windll.kernel32.GetCurrentThreadId());assert rc==193 and not path.exists()
    g=game_check();baseline=json.loads((OUT/'BASELINE.json').read_text(encoding='utf8'))
    assert not g['errors'] and not g['client_copy_errors'] and g['original_full_snapshot_equal'] and g['protected']==baseline['game']['protected']
    dump(OUT/'AUTONOMOUS_GATE.json',dict(status='PASS',regressions=rows,wrong_process_return=rc,game=g,manifest_sha256=sha(OUT/'BUILD_MANIFEST.json'),scope='A2/A3 only; DOC55 not yet authorized by technical gate'))
    print('AUTONOMOUS GATE PASS')
if __name__=='__main__':{'baseline':baseline,'trace':generate_trace,'build':build,'tests':tests,'integrity':integrity,'freeze':freeze,'gate':gate}[sys.argv[1]]()
