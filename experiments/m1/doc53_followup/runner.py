"""Fresh DOC53 follow-up runs. Accepted launcher, isolation and DLL-only loader."""
from pathlib import Path
import importlib.util,json,shutil,sys
from workflow import LAB,ROOT,OUT,sha,dump
sys.path.insert(0,str(LAB.parent/'native_bootstrap'))
spec=importlib.util.spec_from_file_location('runner53f_base',LAB.parent/'native_bootstrap/runner.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
m.LAB=LAB;m.IMPLEMENTATION=OUT;m.prepare.PROFILE=LAB/'profile'
def configure(role):
    assert role in ['host','client']
    m.prepare.COPY=ROOT/'experiments/m0/game' if role=='host' else LAB.parent/'native_second_instance/game'
def bind(name,role):
    run,launch=m.owned_run(name)
    sys.argv=['generate_observer',name];__import__('generate_observer').main()
    sys.argv=['bind_probe',name,'isolation'];__import__('bind_probe').main()
    manifest=json.loads((OUT/'BUILD_MANIFEST.json').read_text(encoding='utf8'))
    for row in manifest['sources']:assert sha(ROOT/row['path'])==row['sha256'],row['path']
    artifact=run/'native_artifact';artifact.mkdir()
    dll='pse_m11_bootstrap.dll' if role=='host' else 'pse_native_client.dll'
    for ext in ['.dll','.pdb']:shutil.copy2((OUT/'artifacts'/dll).with_suffix(ext),(artifact/dll).with_suffix(ext))
    cfg=dict(pid=launch['pid'],target=launch['target'],primary_tid=launch['primary_thread_id'],dll=str(artifact/dll),dll_name=dll,dll_sha256=sha(artifact/dll),native_log=str(run/'native.jsonl'),role=role)
    dump(run/'NATIVE_ARTIFACT.json',dict(config=cfg,build_manifest_sha256=sha(OUT/'BUILD_MANIFEST.json'),no_frida_kex_hooks=True))
    loader=(LAB.parent/'native_second_instance/runner.py').read_text(encoding='utf8');start=loader.index('if(Process.id!==cfg.pid');end=loader.index("'''",start)
    (run/'load_native.js').write_text('const cfg = '+json.dumps(cfg)+';\n'+loader[start:end],encoding='utf8')
    (run/'os_probe.js').write_bytes((LAB.parent/'native_second_instance/os_probe.js').read_bytes())
    cfgfile=Path(launch['profile'])/'early_native_logs/kexengine.cfg'
    # This disposable profile alone adds a keyboard attack binding using stock cfg grammar.
    cfgfile.write_text('seta cl_engineNoSleep "1"\nseta cl_engineFPS "180"\n'+('bind k "attack"\n' if role=='host' else ''),encoding='ascii')
    dump(run/'TEST_CONFIG.json',dict(path=str(cfgfile),sha256=sha(cfgfile),text=cfgfile.read_text(),test_profile_only=True))
    print(json.dumps(cfg))
def gate(name,role):
    try:m.gate(name)
    except SystemExit as e:
        if e.code:raise
    if role=='client':
        p=m.OUT/name/'permit_native.js';p.write_text(p.read_text(encoding='utf8').replace('pse_m11_bootstrap.dll','pse_native_client.dll'),encoding='utf8')
if __name__=='__main__':
    cmd,role,name=sys.argv[1:4];assert name.startswith('N53F_');configure(role)
    if cmd=='launch':
        assert (OUT/'AUTONOMOUS_GATE.json').is_file()
        assert not (m.OUT/name).exists();m.launch(name)
    elif cmd=='bind':bind(name,role)
    elif cmd=='gate':gate(name,role)
    elif cmd=='action':
        assert 'activate' not in sys.argv[4:]
        m.action(name,sys.argv[4],sys.argv[5:])
    else:raise ValueError(cmd)
