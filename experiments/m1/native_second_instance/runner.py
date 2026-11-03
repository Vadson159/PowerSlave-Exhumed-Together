"""One bounded two-process DOC51 run, reusing accepted M0 isolation unchanged."""
from pathlib import Path
import importlib, importlib.util,json,shutil,sys
LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2];NATIVE=LAB.parent/'native_bootstrap'
sys.path.insert(0,str(NATIVE));spec=importlib.util.spec_from_file_location('m11_runner51',NATIVE/'runner.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
m.LAB=LAB;m.IMPLEMENTATION=m.OUT/'NATIVE_SECOND_INSTANCE_51';m.prepare.PROFILE=LAB/'profile'
def configure(role):
    if role not in ['host','client']:raise ValueError(role)
    m.prepare.COPY=ROOT/'experiments/m0/game' if role=='host' else LAB/'game'
def bind(name,role):
    run,launch=m.owned_run(name)
    sys.argv=['generate_observer',name];importlib.import_module('generate_observer').main()
    sys.argv=['bind_probe',name,'isolation'];importlib.import_module('bind_probe').main()
    artifact=run/'native_artifact';artifact.mkdir()
    dll='pse_m11_bootstrap.dll' if role=='host' else 'pse_native_client.dll'
    for ext in ['.dll','.pdb']:
        source=m.IMPLEMENTATION/'artifacts'/Path(dll).with_suffix(ext);shutil.copy2(source,artifact/source.name)
    suffix='_MAIN' if name=='N51_CLIENT_MAIN_01' else ''
    manifest_path=m.IMPLEMENTATION/('BUILD_MANIFEST'+suffix+'.json')
    if suffix:
        for ext in ['.dll','.pdb']:
            source=m.IMPLEMENTATION/('artifacts'+suffix)/Path(dll).with_suffix(ext);shutil.copy2(source,artifact/source.name)
    manifest=json.loads(manifest_path.read_text(encoding='utf8'))
    for row in manifest['sources']:
        assert m.digest(ROOT/row['path'])==row['sha256'],'Source drift after freeze: '+row['path']
    cfg=dict(pid=launch['pid'],target=launch['target'],primary_tid=launch['primary_thread_id'],dll=str(artifact/dll),dll_name=dll,dll_sha256=m.digest(artifact/dll),native_log=str(run/'native.jsonl'),role=role)
    m.dump(run/'NATIVE_ARTIFACT.json',dict(config=cfg,build_manifest_sha256=m.digest(manifest_path),sources=manifest['sources'],no_frida_kex_hooks=True))
    source='const cfg = '+json.dumps(cfg)+';\n'+r'''
if(Process.id!==cfg.pid || Process.mainModule.path.toLowerCase()!==cfg.target.toLowerCase())throw Error('loader target mismatch');
const load=new NativeFunction(Module.getGlobalExportByName('LoadLibraryW'),'pointer',['pointer'],'win64');
if(load(Memory.allocUtf16String(cfg.dll)).isNull())throw Error('LoadLibraryW failed');
const dll=Process.getModuleByName(cfg.dll_name);
if(dll.path.toLowerCase()!==cfg.dll.toLowerCase())throw Error('wrong DLL');
const prepare=new NativeFunction(dll.getExportByName('M11Prepare'),'uint32',['pointer','uint32'],'win64');
const result=prepare(Memory.allocUtf16String(cfg.native_log),cfg.primary_tid);
send({kind:'m11_dll_loader',pid:Process.id,dll_path:dll.path,dll_sha256_host_verified:cfg.dll_sha256,prepare_result:result,kex_calls:0,role:cfg.role});
if(result!==0)throw Error('Prepare rejected '+result);
'''
    (run/'load_native.js').write_text(source,encoding='utf8')
    # Independent OS send/socket/wait evidence, same accepted APIs as DOC44/48.
    (run/'os_probe.js').write_text((LAB/'os_probe.js').read_text(encoding='utf8'),encoding='utf8')
    print(json.dumps(cfg))
def gate(name,role):
    # Reuse mandatory isolation loader/gameplay checks. Client module name differs.
    try:m.gate(name)
    except SystemExit as e:
        if e.code:raise
    if role=='client':
        p=m.OUT/name/'permit_native.js';p.write_text(p.read_text(encoding='utf8').replace("pse_m11_bootstrap.dll","pse_native_client.dll"),encoding='utf8')
if __name__=='__main__':
    cmd,role,name=sys.argv[1:4];configure(role)
    if cmd=='launch':
        assert name==('N51_HOST_01' if role=='host' else 'N51_CLIENT_MAIN_01')
        assert not (m.OUT/name).exists();m.launch(name)
    elif cmd=='bind':bind(name,role)
    elif cmd=='gate':gate(name,role)
    elif cmd=='action':m.action(name,sys.argv[4],sys.argv[5:])
    else:raise ValueError(cmd)
