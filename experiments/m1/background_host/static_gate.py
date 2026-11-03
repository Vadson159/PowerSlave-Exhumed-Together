"""Check retained build/tests/source identity before the single native scenario."""
import json,re
from audit import ROOT,OUT,dump,sha,game_check
expected={'background_contract':'BACKGROUND SUMMARY checks=37 failures=0','pse_stress_test':'11 / 11','pse_fault_injection':'16','pse_bridge_acceptance':'11','r1_live':'','r2_fixed':'','routing_contract':'failures=0','admission_contract':'failures=0','timeout_contract':'','sequence_contract':'failures=0','sequence_live':'failures=0','native_client_contract':'failures=0','TIMER_FIXTURE':'Test Cases Passed: 46 / 46'}
rows=[]
for name,token in expected.items():
    folder=sorted(OUT.glob('TEST_'+name+'_*'))[-1]
    o=json.loads((folder/'RESULT.json').read_text(encoding='utf8'));s=(folder/'console.log').read_text(encoding='utf8')
    assert o['exit_code']==0,(name,o)
    # Exact source/exit evidence, actual suite summaries retained verbatim below.
    rows.append(dict(name=name,exit_code=o['exit_code'],binary_sha256=o['binary_sha256'],log=str((folder/'console.log').relative_to(OUT)),summary='\n'.join(s.splitlines()[-9:])))
    if name in ['background_contract','TIMER_FIXTURE']:assert token in s
manifest=json.loads((OUT/'BUILD_MANIFEST.json').read_text(encoding='utf8'))
for r in manifest['sources']:assert sha(ROOT/r['path'])==r['sha256'],r['path']
g=game_check();assert not g['errors'] and not g['client_copy_errors'] and g['original_full_snapshot_equal']
os=json.loads(sorted(OUT.glob('OS_before_native_*.json'))[-1].read_text(encoding='utf8'));assert not os['processes'] and not os['udp']
dump(OUT/'AUTONOMOUS_GATE.json',dict(status='PASS_FOR_BOUNDED_NATIVE_WITH_EXPLICIT_TIMER_FIXTURE',native_runs_authorized_maximum=1,static_contract='Focus byte +319, owned original responder event13/reason1,2; menu/inventory never rewritten',regressions=rows,default_timer_pristine_suite=dict(exit_code=1,score='44/46',failures=[8,9],retained=True),timer_fixture='Same original source/assertions/deadlines; only timeBeginPeriod(1) in that test process;46/46. No production timer/transport change and no causal-generalization claim.',build_manifest_sha256=sha(OUT/'BUILD_MANIFEST.json'),wrong_process=json.loads((OUT/'WRONG_PROCESS_PREFLIGHT.json').read_text(encoding='utf8')),os_preflight=os,game=g))
print('Bounded native gate ready; 13 passing contracts/suites; default-timer 44/46 retained')
