"""P23 fresh native run: accepted DOC51/REA lane, no new injector."""
from pathlib import Path
import runtime_p4 as r
r.OUT=r.ROOT/'docs/evidence/m1/P23_NATIVE'
r.LAB=r.ROOT/'experiments/pse_together/runtime_p23'
r.RUNS={'host':'P23_HOST_05','client':'P23_CLIENT_05'}
r.GATE_PATH=r.OUT/'PRE_NATIVE05.json'
r.DLLS={role:r.OUT/f'PRE_NATIVE05_snapshot/artifacts/{role}/{name}.dll' for role,name in [('host','pse_m11_bootstrap'),('client','pse_native_client58b')]}
original_verify=r.verify_artifacts
def verify():
    gate=original_verify()
    for row in gate['source_copies']:
        p=r.inside(r.ROOT/row['path'])
        if r.sha(p)!=row['sha256']:raise ValueError('Frozen source changed: '+str(p))
    return gate
r.verify_artifacts=verify
original_bind=r.bind
def bind(m,role,name):
    original_bind(m,role,name)
    run=r.OUT/name
    from rhi_p23 import stage
    stage(run)
    cfg=r.load(run/'NATIVE_ARTIFACT.json')['config']
    launch=r.load(run/'launch.json')
    config=r.inside(Path(launch['profile'])/'early_native_logs/kexengine.cfg')
    previous=config.read_text('ascii')
    config.write_text(previous+'seta r_rhirenderfamily \"d3d11\"\n','ascii')
    r.dump(run/'P23_RENDER_CONFIG.json',dict(path=str(config),sha256=r.sha(config),text=config.read_text('ascii'),renderer='d3d11',test_profile_only=True,supersedes='TEST_CONFIG.json',original_config_changed=False))

    import json
    head='const cfg='+json.dumps(cfg)+';\n'+"if(Process.id!==cfg.pid||Process.mainModule.path.toLowerCase()!==cfg.target.toLowerCase())throw Error('target');const d=Process.getModuleByName(cfg.dll_name);if(d.path.toLowerCase()!==cfg.dll.toLowerCase())throw Error('DLL path');\n"
    (run/'permit_interface.js').write_text(head+"const r=new NativeFunction(d.getExportByName('PSE_P23_PermitInterface'),'uint32',[],'win64')();send({kind:'p23_interface_permit',result:r,gameplay_permit:false,kex_calls:0});\n",'utf8')
    (run/'read_ui.js').write_text(head+"const p=Memory.alloc(96);const r=new NativeFunction(d.getExportByName('PSE_P23_ReadObservation'),'uint32',['pointer','uint32'],'win64')(p,96);const names=['presents','overlays','arrows','markers','menus','submitted','chats','restores','errors'];const o={kind:'p23_observation',result:r};if(r===0){for(let i=0;i<names.length;i++)o[names[i]]=p.add(8+i*8).readU64().toString();o.render_tid=p.add(80).readU32();o.sim_tid=p.add(84).readU32();o.ready=p.add(88).readU32();o.requested=p.add(92).readU32();}send(o);\n",'utf8')
r.bind=bind
if __name__=='__main__':r.main()
