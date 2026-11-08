"""Fresh P16 REA/DOC51 lane. No launch or instrumentation on import."""
import os
import sys
import runtime_p4 as r
revision=os.environ.get('PSE_P16_RUN','01')
if not revision.isdigit():raise ValueError('numeric fresh revision required')
r.OUT=r.ROOT/'docs/evidence/m1/P16_RESPAWN'
r.LAB=r.ROOT/'experiments/pse_together/runtime_p16'
r.RUNS={role:f'P16_{role.upper()}_{revision}' for role in ['host','client']}
r.GATE_PATH=r.OUT/f'PRE_NATIVE{revision}.json'
r.DLLS={role:r.OUT/f'PRE_NATIVE{revision}_snapshot/artifacts/{role}/{name}.dll' for role,name in [('host','pse_m11_bootstrap'),('client','pse_native_client58b')]}
original_verify=r.verify_artifacts
def verify():
    gate=original_verify()
    for row in gate['source_copies']:
        if r.sha(r.ROOT/row['source'])!=row['sha256']:raise ValueError('Frozen source drift: '+row['source'])
    return gate
r.verify_artifacts=verify
original_bind=r.bind
def bind(m,role,name):
    original_bind(m,role,name)
    launch=r.load(r.OUT/name/'launch.json')
    config=r.inside(r.ROOT/'experiments/pse_together/runtime_p16/profile'/name/'early_native_logs/kexengine.cfg')
    text='seta cl_engineNoSleep "1"\nseta cl_engineFPS "60"\nbind w "forward"\nbind s "backward"\nbind a "strafeleft"\nbind d "straferight"\nbind e "+use"\nbind mouse_left "attack"\n'
    config.write_text(text,encoding='ascii')
    r.dump(r.OUT/name/'P16_INPUT_CONFIG.json',dict(pid=launch['pid'],path=str(config),sha256=r.sha(config),text=text,test_profile_only=True))
r.bind=bind
def physical(role,key,seconds):
    if role not in r.RUNS or key not in ['W','S','A','D','E'] or not 0<float(seconds)<=.4:raise ValueError('bounded owned input required')
    source=(r.ROOT/'experiments/m1/native_network_visual_proxy_58b/input58b.py').read_text(encoding='utf8')
    source=source.replace('from workflow import ROOT, OUT, dump, stamp','from runtime_p4 import ROOT,OUT,dump,stamp').replace('from workflow import ROOT,OUT,dump,stamp','from runtime_p4 import ROOT,OUT,dump,stamp')
    source=source.replace("RUNS={'host':'N58B_HOST_02','client':'N58B_CLIENT_02'}",'RUNS='+repr(r.RUNS))
    source=source.replace("d,h=focus('host',records)","d,h=focus("+repr(role)+",records)")
    source=source.replace("['W','S','A','D']","['W','S','A','D','E']")
    source=source.replace('fg=ui.foreground();records.append',"fg=ui.foreground();\n    if fg['pid']!=d['pid']:\n        from focus_p1 import titlebar_focus\n        titlebar_focus(d,h,records);fg=ui.foreground()\n    records.append")
    source=source.replace('def now():',"def physical_key(vk,scan,down):\n    ui.u.keybd_event.argtypes=[ui.W.BYTE,ui.W.BYTE,ui.W.DWORD,ui.C.c_size_t]\n    ui.u.keybd_event(vk,scan,0 if down else 2,0)\n    return True\n\ndef now():")
    source=source.replace('ui.u.PostMessageW(h,0x100,vk,1|(scan<<16))','physical_key(vk,scan,True)')
    source=source.replace('ui.u.PostMessageW(h,0x100,vk,1|(scan<<16)|(1<<30))','True')
    source=source.replace('ui.u.PostMessageW(h,0x101,vk,1|(scan<<16)|(1<<30)|(1<<31))','physical_key(vk,scan,False)')
    source=source.replace("result=dict(status='PASS',action=sys.argv[1:]","result=dict(status='PASS',physical_os_keyboard=True,role="+repr(role)+",action=sys.argv[1:]")
    scope={'__name__':'p16_accepted_input_fixture'};exec(compile(source,'p16_accepted_input_fixture','exec'),scope)
    sys.argv=['p16_input','key',key,str(seconds)];scope['main']()
if __name__=='__main__':
    if sys.argv[1]=='physical':physical(*sys.argv[2:5])
    else:r.main()
