"""DOC52 bounded scenario; retained DOC51 DLLs, unchanged accepted launcher/guard."""
from pathlib import Path
import importlib.util, json, shutil, sys
LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2]
sys.path.insert(0,str(LAB.parent/'native_bootstrap'))
spec=importlib.util.spec_from_file_location('m11runner52',LAB.parent/'native_bootstrap/runner.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
m.LAB=LAB;m.prepare.PROFILE=LAB/'profile'
m.IMPLEMENTATION=m.OUT/'HOST_TERMINAL_52'
PREVIOUS=m.OUT/'NATIVE_SECOND_INSTANCE_51'
NAMES={'host':'N52_HOST_DIAG_01','client':'N52_CLIENT_DIAG_01'}

def configure(role):
    m.prepare.COPY=ROOT/'experiments/m0/game' if role=='host' else LAB.parent/'native_second_instance/game'

def bind(name,role):
    run,launch=m.owned_run(name)
    sys.argv=['generate_observer',name];__import__('generate_observer').main()
    sys.argv=['bind_probe',name,'isolation'];__import__('bind_probe').main()
    artifact=run/'native_artifact';artifact.mkdir()
    dll='pse_m11_bootstrap.dll' if role=='host' else 'pse_native_client.dll'
    prior=PREVIOUS/('artifacts' if role=='host' else 'artifacts_MAIN')
    expected='90412198bb08f52b0099426fc07b558aa3fadd2c0a88bfab901d03cc2e8f795f' if role=='host' else '4887fddaa4071321ab9a89fefb5bc8e41426fb58440c7137fd19759a917fdef3'
    assert m.digest(prior/dll)==expected
    for ext in ['.dll','.pdb']:shutil.copy2((prior/dll).with_suffix(ext),(artifact/dll).with_suffix(ext))
    manifest=PREVIOUS/('BUILD_MANIFEST.json' if role=='host' else 'BUILD_MANIFEST_MAIN.json')
    cfg=dict(pid=launch['pid'],target=launch['target'],primary_tid=launch['primary_thread_id'],dll=str(artifact/dll),dll_name=dll,dll_sha256=expected,native_log=str(run/'native.jsonl'),role=role)
    m.dump(run/'NATIVE_ARTIFACT.json',dict(config=cfg,build_manifest_path=str(manifest),build_manifest_sha256=m.digest(manifest),byte_identical_doc51=True,no_frida_kex_hooks=True))
    loader=(LAB.parent/'native_second_instance/runner.py').read_text(encoding='utf8')
    # Reuse the exact saved loader body, without executing historical run names.
    start=loader.index("if(Process.id!==cfg.pid")
    end=loader.index("'''",start)
    (run/'load_native.js').write_text('const cfg = '+json.dumps(cfg)+';\n'+loader[start:end],encoding='utf8')
    (run/'os_probe.js').write_bytes((LAB.parent/'native_second_instance/os_probe.js').read_bytes())
    if role=='host':(run/'terminal_probe.js').write_text('const cfg = '+json.dumps(cfg)+';\n'+(LAB/'terminal_probe.js').read_text(encoding='utf8'),encoding='utf8')
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
        assert not (m.OUT/name).exists(),'No hidden repeat'
        assert (m.IMPLEMENTATION/'STATIC_PREFLIGHT.json').is_file(),'Static preflight required'
        m.launch(name)
    elif cmd=='bind':bind(name,role)
    elif cmd=='gate':gate(name,role)
    elif cmd=='action':m.action(name,sys.argv[3],sys.argv[4:])
    else:raise ValueError(cmd)
