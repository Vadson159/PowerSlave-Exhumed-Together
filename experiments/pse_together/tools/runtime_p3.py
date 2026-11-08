"""P3 binding of the accepted P1 launcher, isolation guard, DOC51 loader, DOC53 watcher and input fixture.

No new injection mechanism; use through REA MCP's Python spawn lane exactly as RUNTIME_P1.md describes.
Host = frozen accepted P1 host artifact (code-equal to the DOC-56 host, DOC-64); client = pse_dll_client_p3 (b71).
Extra client control scripts publish atomics only: request_reconnect.js (PSE_P3_RequestReconnect) and status_p3.js.
Client world transitions use the accepted owned-HWND UI helper (`action client <run> ui key --key ESC|DOWN|...`).
"""
import json
import runtime_p1 as runtime

runtime.LAB=runtime.ROOT/'experiments/pse_together/runtime_p3'
runtime.OUT=runtime.ROOT/'docs/evidence/m1/PSE_TOGETHER_P3'
# Run 01 (P3_HOST_01/P3_CLIENT_01, client DLL 15a4bf03...) stopped after world 1: enterWorld deadlock fixed for run 02.
# Run 02 (client DLL d3d7443d...) passed 18/18 before the DOC-64 N1 foreign-thread fault fix; run 03 (3ab7982b...) 19/19 with it.
# DOC-78 returned P3 (F1-F5, F7): run 04 binds the DLL with those fixes.
# Run 04 (DLL af29298b..., gate 04) was aborted by the inherited N3 invariant during an optional 11-minute life-cap wait (DOC-78 fixes);
# run 05 repeats the run 03 scenario on the same DLL under gate 05 (the gate freezes this file, so it is edited before the gate).
runtime.RUNS={'host':'P3_HOST_05','client':'P3_CLIENT_05'}
runtime.GATE_PATH=runtime.OUT/'PRE_NATIVE_GATE_P3_05.json'
runtime.DLLS={'host':runtime.ROOT/'docs/evidence/m1/PSE_TOGETHER_P1/P1_HOST_03/native_artifact/pse_m11_bootstrap.dll',
              'client':runtime.ROOT/'b71/dll/pse_dll_client_p3/pse_native_client58b.dll'}
accepted_gate=runtime.gate


def gate(m,role,name):
    accepted_gate(m,role,name)
    if role!='client':
        return
    run,launch=m.owned_run(name)
    cfg=json.loads((run/'NATIVE_ARTIFACT.json').read_text(encoding='utf8'))['config']
    head='const c='+json.dumps(cfg)+';\n'+(
        "if(Process.id!==c.pid||Process.mainModule.path.toLowerCase()!==c.target.toLowerCase())throw Error('target');"
        "const d=Process.getModuleByName(c.dll_name);if(d.path.toLowerCase()!==c.dll.toLowerCase())throw Error('DLL path');\n")
    status=("const st=new NativeFunction(d.getExportByName('PSE58B_Status'),'uint32',[],'win64')();"
            "const p3=new NativeFunction(d.getExportByName('PSE_P3_Status'),'uint32',[],'win64')();"
            "send({kind:'p3_status',status:st,seam:st&255,quiescent:(st>>8)&1,proxy_state_plus1:(st>>16)&255,"
            "incarnation:p3&255,closure:(p3>>8)&255,world:(p3>>16)&65535,utc:new Date().toISOString()});\n")
    (run/'status_p3.js').write_text(head+status,encoding='utf8')
    body=("const r=new NativeFunction(d.getExportByName('PSE_P3_RequestReconnect'),'uint32',[],'win64')();"
          "send({kind:'p3_reconnect_request',result:r,kex_calls:0,atomic_control_only:true,utc:new Date().toISOString()});\n")
    (run/'request_reconnect.js').write_text(head+body+status,encoding='utf8')


runtime.gate=gate

if __name__=='__main__':
    runtime.main()
