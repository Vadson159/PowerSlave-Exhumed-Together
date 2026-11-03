"""DOC58B pre-native gate: exact build, frozen artifacts, offline regressions, wrong-process DLL preflight,
exact-file site prefixes. Writes OUT/PRE_NATIVE_GATE.json (exclusive). No game is started."""
from workflow import ROOT,LAB,OUT,EXPECTED,CM,NODE,HOST_EXE,CLIENT_EXE,sha,load,dump,stamp,command
from build58b import BUILD
import ctypes,json,py_compile,shutil,struct,sys
from pathlib import Path
def file_offset(exe,rva):
    b=exe.read_bytes();pe=struct.unpack_from('<I',b,0x3c)[0];n=struct.unpack_from('<H',b,pe+6)[0];opt=struct.unpack_from('<H',b,pe+20)[0];sec=pe+24+opt
    for i in range(n):
        vsize,va,rsize,rptr=struct.unpack_from('<IIII',b,sec+40*i+8)
        if va<=rva<va+max(vsize,rsize):return rptr+rva-va,b
    raise ValueError(hex(rva))
def main():
    checks=[];ok=lambda name,v,**kw:checks.append(dict(check=name,status='PASS' if v else 'FAIL',**kw))
    ok('host/client EXE exact SHA-256',sha(HOST_EXE)==EXPECTED and sha(CLIENT_EXE)==EXPECTED,host=sha(HOST_EXE),client=sha(CLIENT_EXE))
    command([sys.executable,'-X','utf8','-B',LAB/'build58b.py','pse_native_client58b','proxy58b_contract','snapshot_contract','map_ready_contract','native_client_contract'],'PRECHECK_BUILD',timeout=1500)
    art=OUT/'artifacts';art.mkdir(exist_ok=True)
    for ext in ['.dll','.pdb']:
        src=(BUILD/'Release/pse_native_client58b').with_suffix(ext);dst=(art/'pse_native_client58b').with_suffix(ext)
        assert not dst.exists();shutil.copy2(src,dst)
    exe=lambda n:next(p for p in BUILD.rglob(n+'.exe') if 'Release' in p.parts)
    res=OUT/'offline';res.mkdir(exist_ok=True)
    r=command([exe('proxy58b_contract'),res/'PROXY58B_CONTRACT.json'],'TEST_proxy58b_contract',require=False);ok('proxy58b_contract (patched state machine/router) 32 cases',r['exit_code']==0 and load(res/'PROXY58B_CONTRACT.json')['failed']==0,result=load(res/'PROXY58B_CONTRACT.json')['passed'])
    r=command([NODE,LAB/'offline58b.js',res/'OFFLINE58B_JS.json'],'TEST_offline58b_js',require=False,cwd=LAB);ok('offline58b.js adapter decisions + unchanged DOC58A controller',r['exit_code']==0)
    r=command([NODE,ROOT/'experiments/m1/native_visual_proxy_movement/offline_test.js'],'TEST_doc58a_offline_test',require=False,cwd=ROOT/'experiments/m1/native_visual_proxy_movement');ok('DOC58A offline_test.js unchanged (39 cases)',r['exit_code']==0,log=r['log'])
    for t in ['snapshot_contract','map_ready_contract','native_client_contract']:
        r=command([exe(t)],'TEST_'+t,require=False);ok(t+' (frozen DOC55/56 contract, rebuilt in b58)',r['exit_code']==0,log=r['log'])
    g=sorted(OUT.glob('GEMINI_REPRO_*.json'));gr=load(g[-1]) if g else {};tr=load(OUT/'gemini_repro/TEST_RESULTS.json')['metadata'] if g else {}
    ok('Gemini DOC58D.1 suite reproduced unchanged (original header)',bool(g) and gr['exit_code']==0 and gr['gemini_files_unchanged'] and tr.get('passed')==73 and tr.get('failed')==0,metadata=tr)
    for p in sorted(LAB.glob('*.js')):r=command([NODE,'--check',p],'SYNTAX_'+p.stem,require=False);ok('node --check '+p.name,r['exit_code']==0)
    for p in sorted(LAB.glob('*.py')):py_compile.compile(str(p),cfile=str(LAB/'scratch'/(p.stem+'.pyc')),doraise=True)
    ok('python syntax',True,files=[p.name for p in sorted(LAB.glob('*.py'))])
    # Wrong-process preflight: the frozen client DLL must refuse python.exe before any hook/log/APC.
    dll=ctypes.WinDLL(str(art/'pse_native_client58b.dll'));logfile=OUT/'offline/MUST_NOT_BE_CREATED.jsonl'
    fn=dll.M11Prepare;fn.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32];fn.restype=ctypes.c_uint32
    rr=dll.PSE58B_RegisterSeam;rr.argtypes=[ctypes.c_void_p];rr.restype=ctypes.c_uint32
    st=dll.PSE58B_Status;st.restype=ctypes.c_uint32
    reg=rr(None);prep=fn(str(logfile),ctypes.windll.kernel32.GetCurrentThreadId());reg2=rr(None)
    ok('client DLL refuses a non-target process (193) and seam registration before Prepare (5023)',prep==193 and reg==5023 and reg2==5023 and st()==0 and not logfile.exists(),prepare=prep,register_before_prepare=reg,seam_status=st())
    exports=[n for n in ['M11Prepare','M11PermitTest','PSE58B_RegisterSeam','PSE58B_RequestProxyShutdown','PSE58B_RequestDetach','PSE58B_Status'] if not hasattr(dll,n)]
    ok('client DLL exports',not exports,missing=exports)
    # Exact-file prefixes for every DOC58A site (Frida-owned and DLL-owned) in BOTH game copies.
    sites=load(ROOT/'docs/evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/GATE_N58A_C02/SITES.json');bad=[]
    for e in [HOST_EXE,CLIENT_EXE]:
        for k,v in sites.items():
            off,b=file_offset(e,v['rva'])
            if b[off:off+16].hex()!=v['prefix']:bad.append([str(e),k])
    ok('23 DOC58A site prefixes in exact file bytes (host+client copies)',not bad and len(sites)==23,bad=bad)
    passed=all(c['status']=='PASS' for c in checks)
    frozen=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in [art/'pse_native_client58b.dll',art/'pse_native_client58b.pdb',ROOT/'docs/evidence/m1/NATIVE_MAP_READY_56/artifacts/pse_m11_bootstrap.dll']]
    sources=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in sorted(list(LAB.glob('*.js'))+list(LAB.glob('*.py'))+list(LAB.glob('*.json'))+list((LAB/'src').glob('*'))+list((LAB/'tests').glob('*'))+[LAB/'CMakeLists.txt'])]
    dump(OUT/'PRE_NATIVE_GATE.json',dict(status='PASS' if passed else 'FAIL',utc=stamp(),checks=checks,frozen=frozen,sources=sources,build_dir=str(BUILD),game_started=False,kex_calls=0))
    print('PRE_NATIVE_GATE',passed,sum(c['status']=='PASS' for c in checks),'/',len(checks));assert passed
if __name__=='__main__':main()
