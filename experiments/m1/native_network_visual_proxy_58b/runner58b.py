"""DOC58B two-process runner. Reuses the accepted M0 launcher/isolation, the DOC51 DLL loader and the
DOC53 retained-handle watcher. Host = frozen DOC56 host DLL (unchanged). Client = DOC58B client DLL + Frida adapter.
Usage (from S: root, Python -X utf8 -B): runner58b.py launch|bind|gate|watch|action <role> <run> [...]"""
from workflow import ROOT,LAB,OUT,EXPECTED,sha,load,dump,stamp
from pathlib import Path
import hashlib,importlib.util,json,os,shutil,sys
os.chdir(ROOT)
sys.path.insert(0,str(ROOT/'experiments/m0'));sys.path.insert(0,str(ROOT/'experiments/m1/native_bootstrap'))
s=importlib.util.spec_from_file_location('accepted_m11_runner',ROOT/'experiments/m1/native_bootstrap/runner.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
m.LAB=LAB;m.OUT=OUT;m.IMPLEMENTATION=OUT;m.prepare.OUT=OUT;m.prepare.PROFILE=LAB/'profile'
# Run 01 (N58B_HOST_01/N58B_CLIENT_01) ended BLOCKED before Spawn: host P1 left the attested area over a one-way ledge.
RUNS={'host':'N58B_HOST_02','client':'N58B_CLIENT_02'}
HOST_DLL=ROOT/'docs/evidence/m1/NATIVE_MAP_READY_56/artifacts/pse_m11_bootstrap.dll'
CLIENT_DLL=OUT/'artifacts/pse_native_client58b.dll'
DOC58A=ROOT/'experiments/m1/native_visual_proxy_movement'
SITES=load(ROOT/'docs/evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/GATE_N58A_C02/SITES.json')
DLL_OWNED=['tick','update','state_exit']  # MinHook-owned by the client DLL (Tick/Update/StateExit); Frida never attaches there
def configure(role):
    assert role in RUNS
    m.prepare.COPY=ROOT/'experiments/m0/game' if role=='host' else ROOT/'experiments/m1/native_second_instance/game'
def verify_artifacts():
    man=load(ROOT/'docs/evidence/m1/NATIVE_MAP_READY_56/BUILD_MANIFEST.json')
    for r in man['artifacts']:
        if Path(r['path']).name.startswith('pse_m11_bootstrap'):assert sha(ROOT/r['path'])==r['sha256'],r['path']
    gate=load(OUT/'PRE_NATIVE_GATE.json');assert gate['status']=='PASS'
    for r in gate['frozen']:assert sha(ROOT/r['path'])==r['sha256'],r['path']
    return gate
def adapter_source(cfg):
    names=load(LAB/'bound_sources.json');body='\n'.join((LAB/n).read_text(encoding='utf8') for n in names)
    cfg['source_sha256']=hashlib.sha256(body.encode()).hexdigest()
    return 'const cfg='+json.dumps(cfg)+';\n'+body,[dict(path=(LAB/n).resolve().relative_to(ROOT).as_posix(),sha256=sha(LAB/n)) for n in names]
def bind(role,name):
    run,launch=m.owned_run(name);gate=verify_artifacts()
    sys.argv=['generate_observer',name];__import__('generate_observer').main()
    sys.argv=['bind_probe',name,'isolation'];__import__('bind_probe').main()
    art=run/'native_artifact';art.mkdir()
    src=HOST_DLL if role=='host' else CLIENT_DLL
    for ext in ['.dll','.pdb']:shutil.copy2(src.with_suffix(ext),(art/src.name).with_suffix(ext))
    dll=art/src.name
    cfg=dict(pid=launch['pid'],target=launch['target'],primary_tid=launch['primary_thread_id'],dll=str(dll),dll_name=src.name,dll_sha256=sha(dll),native_log=str(run/'native.jsonl'),role=role)
    dump(run/'NATIVE_ARTIFACT.json',dict(config=cfg,pre_native_gate_sha256=sha(OUT/'PRE_NATIVE_GATE.json'),host_dll_is_frozen_doc56=role=='host',new_injector=False))
    loader=(ROOT/'experiments/m1/native_second_instance/runner.py').read_text(encoding='utf8');a=loader.index('if(Process.id!==cfg.pid');b=loader.index("'''",a)
    (run/'load_native.js').write_text('const cfg = '+json.dumps(cfg)+';\n'+loader[a:b],encoding='utf8')
    (run/'os_probe.js').write_bytes((ROOT/'experiments/m1/native_second_instance/os_probe.js').read_bytes())
    (run/'snapshot_os_probe.js').write_text('const cfg = '+json.dumps(cfg)+';\n'+(ROOT/'experiments/m1/native_snapshot/snapshot_os_probe.js').read_text(encoding='utf8'),encoding='utf8')
    c=Path(launch['profile'])/'early_native_logs/kexengine.cfg';c.parent.mkdir(parents=True,exist_ok=True)
    c.write_text('seta cl_engineNoSleep "1"\nseta cl_engineFPS "180"\n',encoding='ascii')
    dump(run/'TEST_CONFIG.json',dict(path=str(c),sha256=sha(c),text=c.read_text(),test_profile_only=True))
    if role=='client':
        acfg=dict(pid=launch['pid'],target=launch['target'],simulation_tid=launch['primary_thread_id'],binary_sha256=EXPECTED,log=str(run/'adapter.jsonl'),
            sites={k:v for k,v in SITES.items() if k not in DLL_OWNED},dll_owned={k:SITES[k] for k in DLL_OWNED},dll_name=src.name,dll_path=str(dll),
            position=[-7420.0,-6775.0,1240.0],z_offset=88.0,step_limit=160.0,area_sector=259,area_box=[-7552.0,-7296.0,-6912.0,-6656.0],area_z=1240.0,maximum_live_steps=36000)
        text,sources=adapter_source(acfg)
        with (run/'adapter_bound.js').open('x',encoding='utf8') as f:f.write(text)
        dump(run/'ADAPTER_MANIFEST.json',dict(config=acfg,sources=sources,bound_sha256=sha(run/'adapter_bound.js'),doc58a_files_unchanged=[r for r in sources if '/native_visual_proxy_movement/' in r['path']]))
        w=(LAB/'unload_witness.js').read_text(encoding='utf8')
        (run/'unload_witness.js').write_text('const cfg='+json.dumps(dict(pid=launch['pid'],target=launch['target'],simulation_tid=launch['primary_thread_id'],log=str(run/'unload_witness.jsonl')))+';\n'+w,encoding='utf8')
    print(json.dumps(cfg))
def gate(role,name):
    try:m.gate(name)
    except SystemExit as e:
        if e.code:raise
    run,launch=m.owned_run(name)
    if role!='client':return
    p=run/'permit_native.js';p.write_text(p.read_text(encoding='utf8').replace('pse_m11_bootstrap.dll','pse_native_client58b.dll'),encoding='utf8')
    head='const c='+json.dumps(dict(pid=launch['pid'],target=launch['target'],dll='pse_native_client58b.dll'))+';\n'+"if(Process.id!==c.pid||Process.mainModule.path.toLowerCase()!==c.target.toLowerCase())throw Error('target');const d=Process.getModuleByName(c.dll);\n"
    status="const st=new NativeFunction(d.getExportByName('PSE58B_Status'),'uint32',[],'win64')();send({kind:'n58b_status',status:st,seam:st&0xff,quiescent:(st>>8)&1,proxy_state_plus1:(st>>16)&0xff,utc:new Date().toISOString()});\n"
    (run/'status.js').write_text(head+status,encoding='utf8')
    (run/'request_shutdown.js').write_text(head+"const r=new NativeFunction(d.getExportByName('PSE58B_RequestProxyShutdown'),'uint32',[],'win64')();send({kind:'n58b_request_shutdown',result:r,utc:new Date().toISOString()});\n",encoding='utf8')
    (run/'request_detach.js').write_text(head+"const r=new NativeFunction(d.getExportByName('PSE58B_RequestDetach'),'uint32',[],'win64')();send({kind:'n58b_request_detach',result:r,utc:new Date().toISOString()});\n"+status,encoding='utf8')
def mailbox(name,value):
    run,launch=m.owned_run(name);rows=[json.loads(x) for x in (run/'adapter.jsonl').read_text(encoding='utf8').splitlines()]
    control=next(r for r in rows if r['event']=='probe_loaded')['control']
    body='const c='+json.dumps(dict(pid=launch['pid'],target=launch['target'],control=control,command=int(value)))+';\n'+"if(Process.id!==c.pid||Process.mainModule.path.toLowerCase()!==c.target.toLowerCase())throw Error('mailbox target');ptr(c.control).writeU32(c.command);send({kind:'n58b_mailbox',command:c.command,game_writes:0,kex_calls:0,utc:new Date().toISOString()});\n"
    p=run/f'mailbox_{value}.js'
    with p.open('x',encoding='utf8') as f:f.write(body)
    print(p)
def watch(role,name):
    p=ROOT/'experiments/m1/background_host/watch.py';src=p.read_text(encoding='utf8').replace('from audit import ROOT, LAB, OUT','from workflow import ROOT, LAB, OUT')
    src=src.replace("run=ROOT/'docs/evidence/m1'/('N53_'+role.upper()+'_MAIN_01')",'run=OUT/'+repr(name)).replace("LAB.parent/'terminal_shutdown/observer.py'","ROOT/'experiments/m1/terminal_shutdown/observer.py'")
    src=src.replace('time.monotonic()-begin<1200','time.monotonic()-begin<3600')  # one longer two-process session; observer itself unchanged
    ns={'__name__':'accepted53_watch_58b','__file__':str(p)};exec(compile(src,str(p),'exec'),ns);ns['watch'](role)
if __name__=='__main__':
    cmd,role,name=sys.argv[1:4];assert RUNS.get(role)==name,'only the authorized fresh DOC58B runs';configure(role)
    if cmd=='launch':verify_artifacts();assert not (OUT/name).exists();m.launch(name)
    elif cmd=='bind':bind(role,name)
    elif cmd=='gate':gate(role,name)
    elif cmd=='watch':watch(role,name)
    elif cmd=='mailbox':mailbox(name,sys.argv[4])
    elif cmd=='action':assert 'activate' not in sys.argv[4:];m.action(name,sys.argv[4],sys.argv[5:])
    else:raise ValueError(cmd)
