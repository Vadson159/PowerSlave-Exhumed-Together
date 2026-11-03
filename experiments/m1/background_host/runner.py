"""DOC53 one Host/Client scenario, accepted isolated launcher/loader, no fake activation."""
from pathlib import Path
import importlib.util, json, shutil, sys
LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2]
sys.path.insert(0,str(LAB.parent/'native_bootstrap'))
spec=importlib.util.spec_from_file_location('runner53_base',LAB.parent/'native_bootstrap/runner.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
m.LAB=LAB;m.IMPLEMENTATION=m.OUT/'HOST_BACKGROUND_53';m.prepare.PROFILE=LAB/'profile'
NAMES={'host':'N53_HOST_MAIN_01','client':'N53_CLIENT_MAIN_01'}
def configure(role): m.prepare.COPY=ROOT/'experiments/m0/game' if role=='host' else LAB.parent/'native_second_instance/game'
def bind(name,role):
    run,launch=m.owned_run(name)
    sys.argv=['generate_observer',name];__import__('generate_observer').main()
    sys.argv=['bind_probe',name,'isolation'];__import__('bind_probe').main()
    artifact=run/'native_artifact';artifact.mkdir()
    dll='pse_m11_bootstrap.dll' if role=='host' else 'pse_native_client.dll'
    prior=m.IMPLEMENTATION/'artifacts' if role=='host' else m.OUT/'NATIVE_SECOND_INSTANCE_51/artifacts_MAIN'
    expected=json.loads((m.IMPLEMENTATION/'BUILD_MANIFEST.json').read_text(encoding='utf8'))
    if role=='client': assert m.digest(prior/dll)=='4887fddaa4071321ab9a89fefb5bc8e41426fb58440c7137fd19759a917fdef3'
    else:
        for r in expected['sources']:assert m.digest(ROOT/r['path'])==r['sha256'],'Source drift: '+r['path']
    for ext in ['.dll','.pdb']:shutil.copy2((prior/dll).with_suffix(ext),(artifact/dll).with_suffix(ext))
    cfg=dict(pid=launch['pid'],target=launch['target'],primary_tid=launch['primary_thread_id'],dll=str(artifact/dll),dll_name=dll,dll_sha256=m.digest(artifact/dll),native_log=str(run/'native.jsonl'),role=role)
    m.dump(run/'NATIVE_ARTIFACT.json',dict(config=cfg,host_build_manifest_sha256=m.digest(m.IMPLEMENTATION/'BUILD_MANIFEST.json'),client_retained_doc51=role=='client',no_frida_kex_hooks=True))
    loader=(LAB.parent/'native_second_instance/runner.py').read_text(encoding='utf8');start=loader.index('if(Process.id!==cfg.pid');end=loader.index("'''",start)
    (run/'load_native.js').write_text('const cfg = '+json.dumps(cfg)+';\n'+loader[start:end],encoding='utf8')
    (run/'os_probe.js').write_bytes((LAB.parent/'native_second_instance/os_probe.js').read_bytes())
    (run/'terminal_probe.js').write_text('const cfg = '+json.dumps(cfg)+';\n'+(LAB.parent/'terminal_shutdown/terminal_probe.js').read_text(encoding='utf8'),encoding='utf8')
    # Fresh task-owned minimal settings, never a copied personal profile.
    (Path(launch['profile'])/'early_native_logs/kexengine.cfg').write_text('seta cl_engineNoSleep "1"\nseta cl_engineFPS "180"\n',encoding='ascii')
    print(json.dumps(cfg))
def gate(name,role):
    try:m.gate(name)
    except SystemExit as e:
        if e.code:raise
    if role=='client':
        p=m.OUT/name/'permit_native.js';p.write_text(p.read_text(encoding='utf8').replace('pse_m11_bootstrap.dll','pse_native_client.dll'),encoding='utf8')
if __name__=='__main__':
    cmd,role=sys.argv[1:3];name=NAMES[role];configure(role)
    if cmd=='launch':
        assert not (m.OUT/name).exists(),'No hidden native repeat'
        assert (m.IMPLEMENTATION/'AUTONOMOUS_GATE.json').is_file(),'Build/regression gate required'
        m.launch(name)
    elif cmd=='bind':bind(name,role)
    elif cmd=='gate':gate(name,role)
    elif cmd=='action':
        assert not any(x=='activate' for x in sys.argv[3:]),'Synthetic focus messages prohibited'
        m.action(name,sys.argv[3],sys.argv[4:])
    else:raise ValueError(cmd)
