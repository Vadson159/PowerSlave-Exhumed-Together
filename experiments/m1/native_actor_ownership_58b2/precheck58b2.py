"""DOC-58B.2 pre-native gate (no game started). Usage: precheck58b2.py <TAG>; writes PRE_NATIVE_GATE.json only on PASS."""
from workflow import *
sys.path.insert(0,str(ROOT/'docs/research'))
from inventory import PE
B58OUT=ROOT/'docs/evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B'
SITES=load(ROOT/'docs/evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/GATE_N58A_C02/SITES.json')
OBS={'unload':(0x1c90e0,'48895c241048896c2418488974242057'),'arena_purge':(0xc58b0,'48895c240848896c2420448944241848'),
     'block_dispatch':(0xc5310,None),'actor_dtor':(0x19f210,'40534883ec20488bd9488d05e8933800')}
CLIENT_DLL_HOOKS={'Tick':0x1b6ec0,'Update':0x1dfe30,'StateExit':0x1b6670,'Shutdown':0xfcc20,'EngineShutdown':0xa4c90,'FinalExit':0xfc580}
def main(tag):
    checks=[]
    def c(name,ok,detail=None):checks.append(dict(check=name,status='PASS' if ok else 'FAIL',detail=detail))
    exes={'original':Path(r'S:\Games\SteamLibrary\steamapps\common\PowerSlave Exhumed\anubis_Shipping_Steam_x64.exe'),'host_copy':HOST_EXE,'client_copy':CLIENT_EXE}
    h={k:sha(v) for k,v in exes.items()};c('exact_exe_sha256',all(v==EXPECTED for v in h.values()),h)
    g=load(B58OUT/'PRE_NATIVE_GATE.json');frozen=[(r['path'],sha(ROOT/r['path'])==r['sha256']) for r in g['frozen']]
    c('doc58b_pre_native_gate_pass_and_frozen_unchanged',g['status']=='PASS' and all(x for _,x in frozen),frozen)
    man=load(B58OUT/'INTEGRATION_MANIFEST.json');dll=B58OUT/'artifacts/pse_native_client58b.dll'
    c('frozen_doc58b_client_dll',sha(dll).startswith('3249b735'),dict(path=str(dll.relative_to(ROOT)),sha256=sha(dll)))
    host=ROOT/'docs/evidence/m1/NATIVE_MAP_READY_56/artifacts/pse_m11_bootstrap.dll';c('frozen_doc56_host_dll',sha(host).startswith('2b66f075'),sha(host))
    pes={k:PE(str(v)) for k,v in exes.items() if k!='original'}
    obs={}
    for n,(rva,exp) in OBS.items():
        got={k:p.b[p.off(rva):p.off(rva)+16].hex() for k,p in pes.items()};obs[n]=dict(rva=hex(rva),prefix=got)
        ok=len(set(got.values()))==1 and (exp is None or got['client_copy']==exp) and not got['client_copy'].startswith('e9')
        c('observer_site_prefix_'+n,ok,obs[n])
    ad=[(k,v['rva'],pes['client_copy'].b[pes['client_copy'].off(v['rva']):pes['client_copy'].off(v['rva'])+16].hex()==v['prefix']) for k,v in SITES.items()]
    c('adapter_and_dll_site_prefixes_client_copy',all(x for *_,x in ad),ad)
    rv={r for r,_ in OBS.values()}
    c('no_duplicate_hook_observer_vs_adapter',not rv&{v['rva'] for v in SITES.values()},sorted(hex(x) for x in rv))
    c('no_duplicate_hook_observer_vs_client_dll',not rv&set(CLIENT_DLL_HOOKS.values()),{k:hex(v) for k,v in CLIENT_DLL_HOOKS.items()})
    src=(B58OUT.parent.parent.parent.parent/'experiments/m1/native_network_visual_proxy_58b/src/client58b.cpp').read_text(encoding='utf8')
    c('client_dll_source_hook_set_has_no_actor_dtor','profile::ActorDtor' not in src[src.index('void CALLBACK bootstrap'):src.index('void CALLBACK bootstrap')+900],'client58b.cpp bootstrap() hook array')
    tests={}
    for t,needle in [('TEST_DOC58B_02','TOTAL 32 FAILED 0'),('TEST_DOC58B1_02','TOTAL 18 FAILED 0'),('TEST_DOC58B2_02','TOTAL 21 FAILED 0'),('TEST_ADAPTER58B_02','TOTAL 11 FAILED 0'),('TEST_DOC58A_02','"passed": 39'),('BUILD_02',None)]:
        r=load(OUT/'commands'/t/'RESULT.json');log=(ROOT/r['log']).read_text(encoding='utf8',errors='replace')
        tests[t]=dict(exit=r['exit_code'],ok=r['exit_code']==0 and (needle is None or needle in log))
    bl=(ROOT/load(OUT/'commands/BUILD_02/RESULT.json')['log']).read_text(encoding='utf8',errors='replace')
    tests['BUILD_02']['warnings']=bl.count(': warning ');c('offline_contracts_and_w4_wx_build',all(v['ok'] for v in tests.values()) and tests['BUILD_02']['warnings']==0,tests)
    js=SCRATCH/'ownership_syntax_gate.js';js.write_text('const cfg={};\n'+(LAB/'ownership58b2.js').read_text(encoding='utf8'),encoding='utf8')
    r=subprocess.run(['C:/Program Files/nodejs/node.exe','--check',str(js)],capture_output=True,text=True);c('observer_js_syntax',r.returncode==0,r.stderr[-400:])
    ps=subprocess.run(['tasklist','/FI','IMAGENAME eq anubis_Shipping_Steam_x64.exe','/FO','CSV','/NH'],capture_output=True,text=True).stdout
    c('no_game_process_running','anubis' not in ps.lower(),ps.strip()[:200])
    i=integrity('INTEGRITY_PRE_NATIVE_'+tag);c('integrity_pre_native',i['status']=='PASS',i['status'])
    status='PASS' if all(x['status']=='PASS' for x in checks) else 'FAIL'
    rec=dict(status=status,utc=stamp(),checks=checks,observer_sources=dict(path=str((LAB/'ownership58b2.js').relative_to(ROOT)),sha256=sha(LAB/'ownership58b2.js')),
        strategy='ENGINE_OWNED_UNLOAD for a live ACTIVE proxy (static: af4490/af4600 purge); no Remove is issued in the run before the exit (Remove-in-flight is a documented native hazard)',
        frozen_runtime=['DOC56 host DLL','DOC58B client DLL','DOC58B adapter+DOC58A helpers'],patched_state_machine_loaded_natively=False)
    dump(OUT/('PRE_NATIVE_GATE.json' if status=='PASS' else 'PRE_NATIVE_GATE_FAIL_'+tag+'.json'),rec)
    print(status,[x['check'] for x in checks if x['status']!='PASS'])
if __name__=='__main__':main(sys.argv[1])
