"""Read-only exact-profile and artifact gate. No game/Frida/injection."""
from pathlib import Path
import ctypes
import argparse
import hashlib
import json
import pre_native_p1 as profile
import integrity

ROOT=profile.ROOT
OUT=ROOT/'docs/evidence/m1/PSE_TOGETHER_P14'

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--offline',default='OFFLINE_02');ap.add_argument('--out',default='PRE_NATIVE_02.json');a=ap.parse_args()
    checks=[]
    def check(name,ok,**details): checks.append(dict(name=name,passed=bool(ok),**details))
    offline=json.loads((OUT/a.offline/'RESULT.json').read_text('utf-8'))
    check('offline',offline['status']=='PASS' and offline['passed']==offline['total_expected']==26)
    exact=json.loads((OUT/'EXACT_PROFILE.json').read_text('utf-8'))
    for game in ['experiments/m0/game','experiments/m1/native_second_instance/game']:
        exe=ROOT/game/'anubis_Shipping_Steam_x64.exe';b=exe.read_bytes()
        check(game,sha(exe)==profile.EXPECTED and all(b[profile.offset(b,s['rva']):profile.offset(b,s['rva'])+16].hex()==s['prefix'] for s in exact['sites']))
    game=integrity.games()
    check('original and protected profiles',not game['errors'] and not game['client_copy_errors'] and game['original_full_snapshot_equal'],game=game)
    frozen=[]
    from runtime_p14 import runtime
    for role,path in runtime.DLLS.items():
        dll=ctypes.WinDLL(str(path));fn=dll.M11Prepare
        fn.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32];fn.restype=ctypes.c_uint32
        log=OUT/f'MUST_NOT_EXIST_{role}.jsonl'
        rc=fn(str(log),ctypes.windll.kernel32.GetCurrentThreadId())
        check(role+' foreign process refusal',rc==193 and not log.exists(),code=rc)
        frozen += [dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in [path,path.with_suffix('.pdb')]]
    result=dict(status='PASS' if all(c['passed'] for c in checks) else 'FAIL',checks=checks,
                frozen=frozen,game_started=False,runtime_acceptance=False)
    dest=OUT/a.out
    with dest.open('x',encoding='utf-8') as f: json.dump(result,f,indent=2)
    print(result['status'],len(checks),'checks; NOT runtime acceptance')
    return 0 if result['status']=='PASS' else 1
if __name__=='__main__': raise SystemExit(main())
