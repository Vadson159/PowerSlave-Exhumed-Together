"""Reuse accepted launcher/isolation/window workflow for one new DOC57 run on S."""
from workflow import ROOT,LAB,PARENT,OUT,SCRATCH,EXPECTED,EXE,sha,load,dump,stamp
from pathlib import Path
import hashlib,importlib.util,json,os,sys
os.chdir(ROOT)
sys.path.insert(0,str(ROOT/'experiments/m0'));sys.path.insert(0,str(ROOT/'experiments/m1/native_bootstrap'))
s=importlib.util.spec_from_file_location('accepted57c_runner',ROOT/'experiments/m1/native_bootstrap/runner.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
m.LAB=LAB;m.OUT=OUT;m.IMPLEMENTATION=OUT;m.prepare.OUT=OUT;m.prepare.PROFILE=PARENT/'profile'
RUN='N57_FINAL_01';POSITION=[-7424.0,-6880.0,1240.0]
def config(name):
    run,d=m.owned_run(name);assert sha(EXE)==EXPECTED
    return dict(pid=d['pid'],target=d['target'],simulation_tid=d['primary_thread_id'],binary_sha256=EXPECTED,workspace=str(ROOT),log=str(run/'native.jsonl'),sites=load(OUT/'SITES.json'),position=POSITION,old_position=[-7420.8037109375,-6848.013671875,1152],initial_position=[-7420,-6775,1152],maximum_simulation_ticks=3600)
def bind(name):
    run,d=m.owned_run(name);ready=load(OUT/'PRE_NATIVE_GATE.json');assert ready['status']=='PASS'
    for r in ready['sources']:assert sha(ROOT/r['path'])==r['sha256'],r['path']
    sys.argv=['generate_observer',name];__import__('generate_observer').main()
    sys.argv=['bind_probe',name,'isolation'];__import__('bind_probe').main()
    cfg=config(name);sources=['common.js','inventory_snapshot.js','camera_math.js','sector_membership.js','one_actor.js','probe.js']
    body='\n'.join((LAB/n).read_text(encoding='utf8') for n in sources);cfg['source_sha256']=hashlib.sha256(body.encode()).hexdigest()
    source='const cfg='+json.dumps(cfg)+';\n'+body
    with (run/'probe_bound.js').open('x',encoding='utf8') as f:f.write(source)
    c=Path(d['profile'])/'early_native_logs/kexengine.cfg';c.parent.mkdir(parents=True,exist_ok=True)
    with c.open('w',encoding='ascii') as f:f.write('seta cl_engineNoSleep "1"\nseta cl_engineFPS "180"\n')
    dump(run/'PROBE_MANIFEST.json',dict(config=cfg,sources=[dict(path=(LAB/n).relative_to(ROOT).as_posix(),sha256=sha(LAB/n)) for n in sources],bound_sha256=sha(run/'probe_bound.js'),test_cfg_sha256=sha(c),pre_native_gate_sha256=sha(OUT/'PRE_NATIVE_GATE.json'),new_injector=False))
    print(json.dumps(dict(run=str(run),pid=d['pid'],probe=str(run/'probe_bound.js'))))
def gate(name):
    run,d=m.owned_run(name);records,errors,truncated=m.payloads(run)
    summaries=[p for p in records if p.get('kind')=='isolation_write_guard_summary'];guard=max(summaries,key=lambda p:p.get('write_opens',0),default={})
    outside=[p for p in records if p.get('kind')=='file_write_open_observed' and p.get('succeeded') and not Path(p.get('path','')).resolve().is_relative_to(ROOT)]
    rows=[json.loads(s) for s in (run/'native.jsonl').read_text(encoding='utf8').splitlines()]
    pre=[p for p in rows if p['event']=='native_preflight' and p['tid']==d['primary_thread_id']]
    failures=[p for p in rows if p['event'] in ['experiment_fault','camera_observation_error']]
    passed=bool(pre and guard.get('allowed_disk',0) and not guard.get('errors') and not guard.get('unique_overflow') and not errors and not truncated and not outside and not failures)
    result=dict(status='PASS' if passed else 'PENDING',pid=d['pid'],binary_sha256=EXPECTED,guard=guard,preflight=pre,errors=errors,truncated=truncated,outside=outside,failures=failures)
    path=run/('ISOLATION_GATE.json' if passed else 'ISOLATION_PENDING_'+stamp()+'.json');dump(path,result)
    assert passed,result
    loaded=next(p for p in rows if p['event']=='probe_loaded')
    for value in [1,2]:
        c=dict(pid=d['pid'],target=d['target'],control=loaded['control'],command=value)
        body='const c='+json.dumps(c)+';\n'+"if(Process.id!==c.pid||Process.mainModule.path.toLowerCase()!==c.target.toLowerCase())throw Error('mailbox target');ptr(c.control).writeU32(c.command);send({kind:'vp57c_mailbox',command:c.command,game_writes:0,kex_calls:0});\n"
        with (run/f'command_{value}.js').open('x',encoding='utf8') as f:f.write(body)
    print('NATIVE CAMERA / GEOMETRY / ISOLATION GATE PASS')
def watch(name):
    p=ROOT/'experiments/m1/background_host/watch.py';src=p.read_text(encoding='utf8').replace('from audit import ROOT, LAB, OUT','from workflow import ROOT, LAB, OUT')
    src=src.replace("run=ROOT/'docs/evidence/m1'/('N53_'+role.upper()+'_MAIN_01')",'run=OUT/'+repr(name))
    src=src.replace("LAB.parent/'terminal_shutdown/observer.py'","ROOT/'experiments/m1/terminal_shutdown/observer.py'")
    ns={'__name__':'accepted57c_watch','__file__':str(p)};exec(compile(src,str(p),'exec'),ns);ns['watch']('host')
def frame(name,label):
    assert label in ['before','live','moved','after','post_move']
    m.action(name,'ui',['capture']);run,_=m.owned_run(name)
    rows=[load(p) for p in sorted(run.glob('ui_*.json'))];r=[r for r in rows if r.get('action')=='capture'][-1]
    dump(run/('FRAME_'+label+'.json'),dict(record=r,image_sha256=sha(r['path']),label=label))
if __name__=='__main__':
    cmd,name=sys.argv[1:3];assert name==RUN,'one authorized fresh run only'
    if cmd=='launch':
        assert load(OUT/'PRE_NATIVE_GATE.json')['status']=='PASS';assert not (OUT/name).exists();m.launch(name)
    elif cmd=='bind':bind(name)
    elif cmd=='gate':gate(name)
    elif cmd=='watch':watch(name)
    elif cmd=='action':
        assert 'activate' not in sys.argv[3:];m.action(name,sys.argv[3],sys.argv[4:])
    elif cmd=='frame':frame(name,sys.argv[3])
    else:raise ValueError(cmd)
