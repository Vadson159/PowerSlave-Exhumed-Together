"""DOC57 wrappers around accepted M0/M11 launcher, isolation and window helpers.
No injector, DLL, network, Player construction or camera implementation here.
"""
from pathlib import Path
import hashlib, importlib.util, json, sys
from workflow import ROOT, LAB, OUT, EXPECTED, EXE, sha, dump, load, stamp

sys.path.insert(0,str(ROOT/'experiments/m0'))
sys.path.insert(0,str(ROOT/'experiments/m1/native_bootstrap'))
spec=importlib.util.spec_from_file_location('accepted57_runner',ROOT/'experiments/m1/native_bootstrap/runner.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
m.LAB=LAB;m.OUT=OUT;m.IMPLEMENTATION=OUT;m.prepare.OUT=OUT;m.prepare.PROFILE=LAB/'profile'
POSITION=[-7420.8037109375,-6848.013671875,1152.0]

def config(name,log):
    run,launch=m.owned_run(name)
    assert launch['binary_sha256']==EXPECTED and sha(EXE)==EXPECTED
    return dict(pid=launch['pid'],target=launch['target'],simulation_tid=launch['primary_thread_id'],binary_sha256=EXPECTED,workspace=str(ROOT),log=str(run/log),sites=load(LAB/'sites.json'),position=POSITION,initial_position=[-7420.0,-6775.0,1152.0])
def source(cfg,*names):
    return 'const cfg = '+json.dumps(cfg)+';\n'+'\n'.join((LAB/n).read_text(encoding='utf8') for n in names)
def bind(name):
    run,launch=m.owned_run(name)
    sys.argv=['generate_observer',name];__import__('generate_observer').main()
    sys.argv=['bind_probe',name,'isolation'];__import__('bind_probe').main()
    cfg=config(name,'scout.jsonl')
    (run/'scout_bound.js').write_text(source(cfg,'common.js','scout.js'),encoding='utf8')
    cfgfile=Path(launch['profile'])/'early_native_logs/kexengine.cfg';cfgfile.parent.mkdir(parents=True,exist_ok=True)
    cfgfile.write_text('seta cl_engineNoSleep "1"\nseta cl_engineFPS "180"\n',encoding='ascii')
    dump(run/'CONFIG.json',dict(config=cfg,test_cfg_sha256=sha(cfgfile),native_code_module=None))
    print(json.dumps(dict(pid=launch['pid'],run=str(run),scout=str(run/'scout_bound.js'))))
def all_payloads():
    result=[];errors=[];truncated=False
    for p in sorted((OUT/'rea_bundles').glob('*.json')):
        d=load(p)
        for rec in d.get('evidence',d.get('records',[])):
            n=rec.get('normalized_result',{})
            truncated|=n.get('messages_truncated',False)
            for msg in n.get('messages',[]):
                if msg.get('type')=='error':errors.append(msg)
                if isinstance(msg.get('payload'),dict):result.append(msg['payload'])
    return result,errors,truncated
def gate(name):
    run,launch=m.owned_run(name)
    # REA extraction retains the accepted envelope so the original payload reader works.
    records,errors,truncated=m.payloads(run)
    summaries=[p for p in records if p.get('kind')=='isolation_write_guard_summary']
    guard=max(summaries,key=lambda p:p.get('write_opens',0),default={})
    outside=[p for p in records if p.get('kind')=='file_write_open_observed' and p.get('succeeded') and not Path(p.get('path','')).resolve().is_relative_to(ROOT)]
    scouts=[json.loads(s) for s in (run/'scout.jsonl').read_text(encoding='utf8').splitlines()]
    pre=[p for p in scouts if p['event']=='scout_preflight' and p['tid']==launch['primary_thread_id']]
    passed=bool(pre and not errors and not truncated and not outside and guard.get('allowed_disk',0) and not guard.get('errors') and not guard.get('unique_overflow') and not any(p['event']=='blocked' for p in scouts))
    if pre:passed=passed and pre[0]['geometry']['inside'] and pre[0]['sector_list']['valid'] and pre[0]['primary']['pos']==[-7420,-6775,1152] and pre[0]['primary']['sector']==259
    result=dict(status='PASS' if passed else 'BLOCKED',pid=launch['pid'],binary_sha256=EXPECTED,guard=guard,preflight=pre,errors=errors,truncated=truncated,outside=outside)
    gatepath=run/'ISOLATION_GATE.json'
    if gatepath.exists():gatepath=run/('ISOLATION_GATE_FOLLOWUP.json' if passed else 'ISOLATION_PENDING_'+stamp()+'.json')
    dump(gatepath,result);assert passed,result
    cfg=config(name,'native.jsonl');cfg['source_sha256']=hashlib.sha256(((LAB/'common.js').read_text(encoding='utf8')+(LAB/'probe.js').read_text(encoding='utf8')).encode()).hexdigest()
    (run/'probe_bound.js').write_text(source(cfg,'common.js','probe.js'),encoding='utf8')
    dump(run/'PROBE_MANIFEST.json',dict(config=cfg,bound_sha256=sha(run/'probe_bound.js'),isolation_gate_path=str(gatepath),isolation_gate_sha256=sha(gatepath)))
    print('Isolation/native preflight PASS')
def command(name,value):
    run,launch=m.owned_run(name);assert value in [1,2]
    rows=[json.loads(s) for s in (run/'native.jsonl').read_text(encoding='utf8').splitlines()]
    assert not any(r['event']=='blocked' for r in rows)
    mailbox=next(r['control'] for r in rows if r['event']=='probe_loaded')
    cfg=dict(pid=launch['pid'],target=launch['target'],control=mailbox,command=value)
    text='const cfg = '+json.dumps(cfg)+';\n'+"if(Process.id!==cfg.pid||Process.mainModule.path.toLowerCase()!==cfg.target.toLowerCase())throw Error('mailbox identity');\nptr(cfg.control).writeU32(cfg.command);send({kind:'vp57_mailbox',command:cfg.command,control:cfg.control,game_writes:0,kex_calls:0});\n"
    dest=run/f'command_{value}.js'
    if not dest.exists():dest.write_text(text,encoding='utf8')
    assert dest.read_text(encoding='utf8')==text
    print(str(dest))
def watch(name):
    path=ROOT/'experiments/m1/background_host/watch.py'
    s=path.read_text(encoding='utf8').replace('from audit import ROOT, LAB, OUT','from workflow import ROOT, LAB, OUT')
    s=s.replace("run=ROOT/'docs/evidence/m1'/('N53_'+role.upper()+'_MAIN_01')",'run=OUT/'+repr(name))
    ns={'__name__':'accepted57_watch','__file__':str(path)};exec(compile(s,str(path),'exec'),ns);ns['watch']('host')
if __name__=='__main__':
    cmd,name=sys.argv[1:3];assert name.startswith('N57_')
    if cmd=='launch':m.launch(name)
    elif cmd=='bind':bind(name)
    elif cmd=='gate':gate(name)
    elif cmd=='command':command(name,int(sys.argv[3]))
    elif cmd=='watch':watch(name)
    elif cmd=='action':m.action(name,sys.argv[3],sys.argv[4:])
    else:raise ValueError(cmd)
