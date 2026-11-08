"""P1 pre-native gate. Reads saved tests; verifies current bytes/API, starts no game."""
import ast
import ctypes
import hashlib
import json
import struct
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'docs/evidence/m1/PSE_TOGETHER_P1'
LAB=ROOT/'experiments/pse_together'
EXPECTED='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def load(p): return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def offset(b,rva):
    pe=struct.unpack_from('<I',b,0x3c)[0];n=struct.unpack_from('<H',b,pe+6)[0];opt=struct.unpack_from('<H',b,pe+20)[0];sec=pe+24+opt
    for i in range(n):
        vs,va,rs,rp=struct.unpack_from('<IIII',b,sec+40*i+8)
        if va<=rva<va+max(vs,rs): return rp+rva-va
    raise ValueError(hex(rva))
def main():
    offline=ROOT/sys.argv[1];checks=[]
    def check(name,ok,**data): checks.append(dict(check=name,status='PASS' if ok else 'FAIL',**data))
    tests=load(offline)
    check('all unified offline suites',tests['status']=='PASS' and tests['passed']==tests['total_expected']==24,result=str(offline.relative_to(ROOT)))
    entry=load(OUT/'S03_ENTRY_CHECKS.json')
    check('S0.3 entry review',entry['manifest_matches']==entry['manifest_total']==55 and entry['suite_log_passes']==23,review='S03_ENTRY_REVIEW.md')
    sites=load(ROOT/'docs/evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/GATE_N58A_C02/SITES.json')
    for role,path in [('host','experiments/m0/game'),('client','experiments/m1/native_second_instance/game')]:
        exe=ROOT/path/'anubis_Shipping_Steam_x64.exe';b=exe.read_bytes()
        check(role+' exact EXE',sha(exe)==EXPECTED,sha256=sha(exe))
        bad=[name for name,s in sites.items() if b[offset(b,s['rva']):offset(b,s['rva'])+16].hex()!=s['prefix']]
        check(role+' all 23 accepted sites',len(sites)==23 and not bad,bad=bad)
    dllpath=ROOT/'b60/dll/pse_dll_client_proxy/pse_native_client58b.dll'
    dll=ctypes.WinDLL(str(dllpath));badlog=OUT/'MUST_NOT_BE_CREATED.jsonl'
    prep=dll.M11Prepare;prep.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32];prep.restype=ctypes.c_uint32
    rr=dll.PSE58B_RegisterSeam;rr.argtypes=[ctypes.c_void_p];rr.restype=ctypes.c_uint32
    status=dll.PSE58B_Status;status.restype=ctypes.c_uint32
    rc=prep(str(badlog),ctypes.windll.kernel32.GetCurrentThreadId());reg=rr(None)
    check('wrong process refuses before native work; external seam disabled',rc==193 and reg==50 and status()==0 and not badlog.exists(),prepare=rc,register=reg,seam_status=status())
    exports=['M11Prepare','M11PermitTest','PSE58B_RegisterSeam','PSE58B_RequestProxyShutdown','PSE58B_RequestDetach','PSE58B_Status','PSE_P1_PermitProxySpawn','PSE_P1_ReadObservation','PSE_P1_IsPositionInterpolant']
    check('client exports',all(hasattr(dll,n) for n in exports),exports=exports)
    ob=dll.PSE_P1_ReadObservation;ob.argtypes=[ctypes.c_void_p,ctypes.c_uint32];ob.restype=ctypes.c_uint32
    buf=ctypes.create_string_buffer(128);null_rc=ob(None,128);size_rc=ob(buf,127);rc=ob(buf,128)
    size,abi,phase,valid=struct.unpack_from('<4I',buf.raw)
    check('DTO128 ABI + wrong-thread/uninstalled refusal + no raw pointer',null_rc==87 and size_rc==87 and rc==5023 and (size,abi,phase,valid)==(128,0x50310001,0,0),code=rc,header=[size,abi,phase,valid],null=null_rc,wrong_size=size_rc)
    for p in (LAB/'tools').glob('*.py'): ast.parse(p.read_text(encoding='utf8'),str(p))
    check('Python syntax',True)
    observer=(LAB/'tools/observer_p1.js').read_text(encoding='utf8')
    check('observer cannot register control callbacks',not any(x in observer for x in ['new NativeCallback','PSE58B_RegisterSeam','PSE_P1_PermitProxySpawn','writeFloat(', 'writePointer(']))
    files=[]
    for p in sorted(LAB.rglob('*')):
        if p.is_file() and not {'runtime','__pycache__'}.intersection(p.relative_to(LAB).parts): files.append(p)
    artifacts=[ROOT/'b60/dll/pse_dll_host/pse_m11_bootstrap.dll',ROOT/'b60/dll/pse_dll_host/pse_m11_bootstrap.pdb',dllpath,dllpath.with_suffix('.pdb')]
    result=dict(status='PASS' if all(c['status']=='PASS' for c in checks) else 'FAIL',checks=checks,game_started=False,kex_calls=0,
        frozen=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in artifacts],
        sources=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in files])
    destination=OUT/(sys.argv[2] if len(sys.argv)>2 else 'PRE_NATIVE_GATE.json')
    with destination.open('x',encoding='utf8') as f: json.dump(result,f,indent=2,ensure_ascii=False)
    print(result['status'],sum(c['status']=='PASS' for c in checks),'/',len(checks))
    for c in checks:
        if c['status']!='PASS': print(c)
    return 0 if result['status']=='PASS' else 1
if __name__=='__main__': sys.exit(main())
