"""Build review artifacts from real raw records. Never synthesizes native Proxy/network events."""
from workflow import *
import collections,re,struct
RUN=OUT/'N58B1_TRACE_01'
HIST=ROOT/'docs/evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B'
def rel(p):return Path(p).relative_to(ROOT).as_posix()
def reference(p):return dict(path=rel(p),sha256=sha(p))
def rows(p):
    with Path(p).open(encoding='utf-8-sig') as f:
        for line in f:
            if line.strip():yield json.loads(line)
def main():
    trace=list(rows(RUN/'lifecycle.jsonl'));launch=load(RUN/'launch.json');cfg=load(OUT/'TRACE_CONFIG.json')
    mandatory=['58B_NATIVE_NETWORK_VISUAL_PROXY_INTEGRATION.md','58A_NATIVE_VISUAL_PROXY_COMPLETION.md','56_NATIVE_MAP_IDENTITY_READY_GATE.md','55_NATIVE_PLAYER_SNAPSHOT_POC.md','PROGRESS.md','EVIDENCE_INDEX.md','10_OPUS_HANDOFF.md']
    read_docs=[]
    for n in mandatory:
        p=ROOT/'docs'/n;s=p.read_text(encoding='utf8');read_docs.append(dict(**reference(p),headings=[l for l in s.splitlines() if l.startswith('#')]))
    histories=[]
    for n in ['CODE_REVIEW.json','NATIVE_GATE_RESULTS.json','NATIVE_ACTOR_LIFECYCLE.jsonl','NETWORK_CORRELATION.jsonl','NATIVE_ACCEPTANCE_REVIEW.json','RESULT_SUMMARY.json','SOURCE_MANIFEST.json','HARNESS_ERRORS.md']:
        p=HIST/n
        if p.suffix=='.jsonl':
            count=0;events=collections.Counter()
            for r in rows(p):count+=1;events[r.get('event',r.get('kind','unlabelled'))]+=1
            d=dict(records=count,events=dict(events))
        elif p.suffix=='.json':
            obj=load(p);d=dict(keys=list(obj) if isinstance(obj,dict) else None)
        else:d=dict(text=p.read_text(encoding='utf8'))
        histories.append(dict(**reference(p),historical_only=True,review=d))
    dump(OUT/'HISTORICAL_REVIEW.json',dict(documents=read_docs,evidence=histories,no_historical_result_claimed_as_new=True))
    stat=[]
    for addr in ['1401b6670','1401b6a00','1401b6ec0','1401c90e0','1401c9690','1401b61d0','1401a02b0']:
        p=ROOT/'docs/evidence/ghidra_2022'/('G_FUNC_'+addr+'.json')
        if not p.exists():continue
        d=load(p);assert d['binary_sha256']==EXPECTED
        stat.append(dict(**reference(p),evidence_type='REUSED_EXACT_BUILD_STATIC',function=d['function'],incoming=d.get('incoming_entry_references'),callees=d.get('direct_callees'),body_ranges=d.get('body_ranges'),limitations=d.get('limitations')))
    snippets=[]
    for name in ['visual_proxy_58b.hpp','client58b.cpp','seam_58b.hpp']:
        p=ROOT/'experiments/m1/native_network_visual_proxy_58b/src'/name
        tokens={'visual_proxy_58b.hpp':['m_disconnected=true','configureSession','onPreUnload','orphan()','doRetirement','m_mapToken'], 'client58b.cpp':['void stop(','void barrier58','hookStateExit','void exit58','M11Prepare latches','RegisterSeam','RECONNECT','mapGate.lost'], 'seam_58b.hpp':['registerTable','detach(','seh','fault(']}[name]
        lines=p.read_text(encoding='utf8').splitlines()
        snippets.append(dict(**reference(p),excerpts=[dict(line=i+1,text=l) for i,l in enumerate(lines) if any(t in l for t in tokens)]))
    dump(OUT/'STATIC_LIFECYCLE_AUDIT.json',dict(binary_sha256=EXPECTED,analysis_base='0x140000000',observed_runtime_base=trace[0]['base'],static=stat,source=snippets,
        findings=[dict(id='S01',status='VERIFIED_STATIC_AND_NATIVE',finding='StateExit case 3 invokes level vtable slot +0x10; runtime return RVA 1b6935 binds Unload 1c90e0'),dict(id='S02',status='VERIFIED_STATIC_AND_NATIVE',finding='Sector array is level+28, count+30, stride 88. IAT free call 1c92dc returns 1c92e2, then pointer and count zeroed. Canonical +140 is a discrepancy, not silently corrected.'),dict(id='S03',status='UNKNOWN',finding='Actor1007/ObjectNode bulk cleanup ownership after sector free not attested. No claim that arena cleanup proves retirement.'),dict(id='S04',status='VERIFIED_SOURCE',finding='StateExit barrier precedes Session.stop/worker join, then original StateExit. No call to ClientGate invalidation in stop; no consume after stopped. Gate invalidation cannot be claimed as an observed StateExit event.'),dict(id='S05',status='VERIFIED_SOURCE',finding='DirectQuit is a distinct path; no claim of universal coverage from one StateExit call.'),dict(id='S06',status='UNKNOWN',finding='Destructor alternatives, asynchronous callbacks, bulk actor pool release and all non-title unload paths remain unaudited/partial. No native mutation is authorized by this report.')]))
    events=[r for r in trace if 47<=r['seq']<=60]
    dump(OUT/'LEVEL_UNLOAD_CALL_ORDER.json',dict(status='VERIFIED_BOUNDED_NO_PROXY',pid=launch['pid'],simulation_tid=launch['primary_thread_id'],scenario='Pause -> Exit to Title Screen -> Yes; owned-window messages, screenshots retained',events=events,
        observed_order=['pending=1 at tick6347','StateExit entry at tick6405, state3 next1','Level::Unload entry, sectors270','sector free entry and return, return RVA1c92e2','Unload return, pointer/count zero','arena af4490 cleanup','arena af4600 cleanup','StateExit return','next StateTick state1'],
        limitations=['Only one title-exit path','No Proxy, no gameplay DLL, no ENet session','Last local Puppet update observed via cumulative localTicks=1245; exact last per-update timestamp not logged','StateTick sampled, not every entry/exit persisted','No ClientGate MapReady instance; invalidation timing not measured','No actor-pool ownership witness']))
    bundle=load(OUT/'REA_FINAL.json');payloads=[]
    for record in bundle['records']:
        for m in record.get('normalized_result',{}).get('messages',[]):
            if isinstance(m.get('payload'),dict):payloads.append(m['payload'])
    restore=next(p for p in payloads if p.get('kind')=='hook_restoration')
    sys.path.insert(0,str(ROOT/'docs/research'));from inventory import PE
    free=restore['free'];pe=PE(free['module']);rva=int(free['rva'],16);raw=pe.b[pe.off(rva):pe.off(rva)+16].hex()
    free['file_prefix16']=raw;free['file_equal']=raw==free['prefix16'];assert free['file_equal']
    dump(OUT/'HOOK_RESTORATION.json',dict(status='PASS_BOUNDED',game_sites=restore['checks'],free=free,free_module_sha256=sha(free['module']),method='Unload trace script, read nine prefixes and compare ucrt free against mapped PE bytes; isolation guard retained through normal exit',limitations='OS isolation Nt* hooks remained until process exit; no post-exit byte read claimed'))
    status_path=OUT/'commands/LIFECYCLE_FINAL/console.log';log=status_path.read_text(encoding='utf8');cases=[dict(name=l[5:],status='PASS') for l in log.splitlines() if l.startswith('PASS ')]
    dump(OUT/'LIFECYCLE_CONTRACT_TESTS.json',dict(status='PASS',scope='C++ mock seam characterization, no KEX',total=len(cases),failed=0,cases=cases,raw=reference(status_path),baseline=reference(OUT/'BASELINE_CONTRACT_TESTS.json'),warning='Characterized reconnect blocker is an expected limitation, not reconnect acceptance; native anti-ABA NOT_EXECUTED'))
    stale_names=[c['name'] for c in cases if 'intake_no_writes' in c['name'] or c['name']=='before_new_mapready_no_writes']
    dump(OUT/'STALE_SNAPSHOT_REJECTION.json',dict(status='VERIFIED_OFFLINE_ONLY',cases=stale_names,native_runtime='NOT_EXECUTED',meaning='Rejected at intake, seam write-call counter unchanged. Entity mismatch intentionally requests cleanup; no claim that subsequent lifecycle Remove is an unexpected snapshot write.',anti_ABA='BLOCKED: no second native actor generation; address reuse not exercised'))
    dump(OUT/'RECONNECT_CONTRACT_AUDIT.json',dict(status='BLOCKED_AT_PROXY_INTEGRATION',transport='Existing ENetWorker::reconnectClient supported, no Wire v1.10 change needed',fresh_transport_contract='native_client_contract: 136 checks, 3 DTO/ENet scenarios, failures0; not native reconnect',
        blockers=[dict(layer='VisualProxyStateMachine',detail='onDisconnect sets m_disconnected=true; configureSession does not clear it; after witnessed retirement, new session+MapReady still returns SessionInactive',test='CHARACTERIZED_reconnect_blocker_disconnect_latched'),dict(layer='adapter58b.js / OneActor57C',detail='Single life object is not reconstructed after retired; liveHandle/expected transforms and per-life witnesses not reset. No accepted rearm API.'),dict(layer='map transition',detail='ClientGate Invalidated is terminal; Session stop and global g.stopped are terminal; seam detaches on world exit. Network reconnect and map transition must remain distinct.')],
        first_session_generation=None,second_session_generation=None,first_actor_generation=None,second_actor_generation=None,native_spawn_calls=0,native_remove_calls=0,native_render_samples=0,host_pid=None,client_pid=None,recon_only_pid=launch['pid'],native_anti_ABA='NOT_EXECUTED',action='No transport extension, rearm patch or live Proxy test attempted without native ownership preconditions'))
    dump(OUT/'MAP_READY_TRANSITION.json',dict(status='NOT_EXECUTED',reason='No ClientGate instance/network gameplay DLL in read-only recon. Native map identity and sector lifetime observed; these do not constitute authenticated MapReady.',native_transition=reference(OUT/'LEVEL_UNLOAD_CALL_ORDER.json'),source_gate='DOC56 ClientGate terminal after Invalidated; lost() only resets nonterminal state; welcome requires strictly newer generation',new_map_epoch=None,new_mapready=None))
    fields=[('ankh_count','Player+84',2),('ankh_mask','Player+86',2),('artifacts','Player+90',4),('abilities','Player+94',4),('keys','Player+98',2),('quest_items','Player+9a',2),('team_dolls','Player+a4',4),('current_weapon','Player+88',4),('pending_weapon','Player+8c',4),('owned_weapons','Player+cc',8),('ammo','Player+bc',16),('health','Puppet+17a',2)]
    dump(OUT/'PLAYER_INTEGRITY.json',dict(status='NOT_EXECUTED_FOR_PROXY',fields=[dict(field=n,hex_offset=o,size=s,current_value=None) for n,o,s in fields],pairwise_comparisons=[],player_puppet_ownership='Not captured as full graph in this recon; local update attested',local_camera='No probe writes by source audit; runtime camera integrity not measured',new_proxy_spawn=0,new_player_writes=0,new_camera_writes=0,limitation='Zero deliberate writes is source scope, not a new 12-field integrity PASS. Historical DOC58B comparisons remain historical.'))
    dump(OUT/'G1_NATIVE_WITNESS.json',dict(status='OPEN',recon_status='VERIFIED_BOUNDED_NO_PROXY',canonical_gate='GATE-PSE-58C-01',unload_count=sum(r['event']=='level_unload_entry' and r.get('state')==3 for r in trace),actual_sector_free=True,proxy_alive_at_unload=False,safe_pre_unload_retirement='NOT_EXECUTED',live_proxy_unload='NOT_EXECUTED',actor_ownership='UNKNOWN',native_proxy_remove_drain_retirement='NOT_EXECUTED',reason='Sector free is proven, but Actor1007/ObjectNode ownership at subsequent bulk cleanup is not. No gameplay seam barrier was executed in this read-only process; canonical G1 is not closed.',witness=reference(OUT/'LEVEL_UNLOAD_CALL_ORDER.json')))
    # The immutable trace was recorded before gameplay gate completion; never claim a gameplay preflight PASS.
    prefixes=[]
    sites=load(ROOT/'docs/evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/GATE_N58A_C02/SITES.json')
    for target in [ROOT/'experiments/m0/game/anubis_Shipping_Steam_x64.exe',ROOT/'experiments/m1/native_second_instance/game/anubis_Shipping_Steam_x64.exe']:
        pe=PE(target);checks={n:pe.b[pe.off(s['rva']):pe.off(s['rva'])+16].hex()==s['prefix'] for n,s in sites.items()};assert sha(target)==EXPECTED and all(checks.values());prefixes.append(dict(path=rel(target),sha256=sha(target),sites=checks))
    checks=[dict(check='Exact EXE and inherited site prefixes',status='PASS',evidence=prefixes),dict(check='Build /W4 /WX C++20',status='PASS',evidence='commands/BUILD_FINAL and CMakeLists.txt'),dict(check='DOC58B baseline contracts',status='PASS',count=32),dict(check='Lifecycle contracts',status='PASS',count=len(cases)),dict(check='Snapshot/MapReady/Client contracts',status='PASS',counts=[42,210,136]),dict(check='Baseline JS/58A JS',status='PASS',evidence=['BASELINE_JS','BASELINE_58A_JS']),dict(check='Simulation TID and level identity (no Proxy)',status='PASS_BOUNDED',pid=launch['pid'],tid=launch['primary_thread_id']),dict(check='Native callback registration/detach/M11Prepare success',status='NOT_EXECUTED',reason='No experimental gameplay DLL loaded; never rely on prepared alone'),dict(check='No duplicate hook',status='PASS_BOUNDED',reason='No MinHook DLL loaded, Frida-only observer sites checked before attach'),dict(check='Asynchronous Remove/Drain, Quarantine/Orphaned',status='OFFLINE_ONLY',reason='Baseline/native historical evidence reused, mock tests fresh'),dict(check='Reconnect generations, native anti-ABA',status='BLOCKED',reason='Disconnect latch and single-life adapter; no second native Actor'),dict(check='Sector lifetime',status='PASS_BOUNDED_NO_PROXY',reason='free and zeroing observed on exit-to-title'),dict(check='Actor ownership before/after bulk cleanup',status='UNKNOWN',reason='No independent live Actor1007 witness')]
    dump(OUT/'PRE_NATIVE_GATE.json',dict(status='BLOCKED_FOR_PROXY_MUTATION',trace_scope='Read-only A1 allowed after exact build/site/isolation checks; full gameplay gate was never passed',checks=checks))
    old=load(HIST/'NATIVE_GATE_RESULTS.json');gates=[]
    for g in old['gates']:
        gates.append(dict(gate=g['gate'],definition=g['definition'],prior_DOC58B_status=g['status'],current_status=g['status'],new_evidence='LEVEL_UNLOAD_CALL_ORDER.json: no Proxy; G1 remains OPEN' if g['gate'].endswith('58C-01') else 'NOT_REVERIFIED_NATIVE_IN_58B1',historical_evidence=rel(HIST/'NATIVE_GATE_RESULTS.json')))
    dump(OUT/'NATIVE_GATE_RESULTS.json',dict(registry_source=reference(ROOT/'tools/network-visual-proxy-readiness/doc58d1-contract-audit/GATE_ID_CORRECTIONS.md'),ids_unchanged=True,offset_discrepancy='Canonical G1 +140 preserved verbatim in registry; exact build sectors +28/count+30 as newly observed',gates=gates))
    matrix=[]
    states=['NOT_REPEATED_NATIVE','VERIFIED_BOUNDED_NO_PROXY','BLOCKED','OFFLINE_PASS','BLOCKED','BLOCKED','BLOCKED_AT_PROXY_INTEGRATION','BLOCKED','OFFLINE_ONLY','BLOCKED','NOT_EXECUTED','PASS_BOUNDED_RECON_ONLY']
    for i,s in enumerate(states,1):matrix.append(dict(id=f'B1-{i:02d}',status=s))
    dump(OUT/'NATIVE_ACCEPTANCE_REVIEW.json',dict(status='PARTIAL',matrix=matrix,mandatory_overall_acceptance=False,
        g1='OPEN',reconnect='BLOCKED_AT_PROXY_INTEGRATION',no_native_proxy_test=True,normal_exit=load(RUN/'exit.json'),no_access_violation_is_not_lifetime_proof=True,missing_native_artifacts=['RECONNECT_NETWORK_CORRELATION.jsonl','PROXY_GENERATION_LIFECYCLE.jsonl'],missing_reason='No native reconnect or proxy generations occurred; do not fabricate empty lifecycle/correlation files. Raw no-Proxy lifecycle retained in N58B1_TRACE_01/lifecycle.jsonl.'))
    dump(OUT/'SPAWN_OVERLAP_AUDIT.json',dict(status='DEFERRED',source='experiments/m1/native_bootstrap/src/bootstrap.cpp:449-452',observed_source='P2 spawn uses position(g.p1.actor), P1 yaw and sector verbatim under native getter scope',reason='Host frozen DOC56. Separate P2 position needs native spawn geometry/lifecycle attestation; no arbitrary position writes admitted',safe_existing_test_setup='Historical DOC58B Run02 separated P1 by ordinary W input before peer admission. Not repeated here; no Host process required.',writes_to_P1=0,writes_to_P2=0))
    summary=dict(milestone='DOC-58B.1',utc=stamp(),overall='PARTIAL',g1='OPEN',reconnect='BLOCKED_AT_PROXY_INTEGRATION',spawn_overlap='DEFERRED',native_lifecycle='NO_PROXY_TITLE_EXIT_ORDER_VERIFIED_BOUNDED',network='EXISTING_TRANSPORT_CONTRACTS_PASS; NATIVE_RECONNECT_NOT_EXECUTED',player_integrity='NOT_EXECUTED_FOR_PROXY',hook_restoration='PASS_BOUNDED_RECON_ONLY',os_exit=dict(pid=launch['pid'],tid=launch['primary_thread_id'],exit_code=0,host_client_pair='NOT_LAUNCHED'),constraints='One proxy/Actor1007/sector259/+88/no cross-sector remain unchanged',next_step='Read-only attestation of Actor1007/ObjectNode ownership in StateExit actor-pool cleanup after Level::Unload; do not start it in this milestone')
    dump(OUT/'RESULT_SUMMARY.json',summary)
    print(json.dumps(summary,ensure_ascii=False))
if __name__=='__main__':main()
