"""P4 candidate verification; no game or injector. Use a NEW --out path each time."""
from pathlib import Path
import argparse
import ast
import ctypes
import hashlib
import json
import shutil
import pre_native_p1 as profile

ROOT=Path(__file__).resolve().parents[3]
LAB=ROOT/'experiments/pse_together'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--offline',required=True);ap.add_argument('--out',required=True);ap.add_argument('--build-dir',default='b60');a=ap.parse_args()
    dest=(ROOT/a.out).resolve();assert dest.is_relative_to(ROOT) and not dest.exists()
    checks=[]
    def check(name,ok,**data): checks.append(dict(name=name,passed=bool(ok),**data))
    offline=json.loads((ROOT/a.offline).read_text(encoding='utf8'))
    expected=len(json.loads((LAB/'tools/expected_suites.json').read_text(encoding='utf8'))['suites'])
    check('offline suites',offline['status']=='PASS' and offline['passed']==offline['total_expected']==expected)
    build=(ROOT/a.build_dir).resolve();assert build.parent==ROOT and build.name.startswith('b')
    snapshot=dest.parent/(dest.stem+'_snapshot');snapshot.mkdir()
    artifacts={}
    sites=profile.load(ROOT/'docs/evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/GATE_N58A_C02/SITES.json')
    for role,game in [('host','experiments/m0/game'),('client','experiments/m1/native_second_instance/game')]:
        exe=ROOT/game/'anubis_Shipping_Steam_x64.exe';b=exe.read_bytes()
        check(role+' exact EXE',sha(exe)==profile.EXPECTED,sha256=sha(exe))
        check(role+' accepted native sites',all(b[profile.offset(b,s['rva']):profile.offset(b,s['rva'])+16].hex()==s['prefix'] for s in sites.values()))
    paths=[]
    for role,name in [('host','pse_m11_bootstrap'),('client','pse_native_client58b')]:
        built=build/f'dll/pse_dll_{role}_p4/{name}.dll'
        folder=snapshot/'artifacts'/role;folder.mkdir(parents=True)
        for ext in ['.dll','.pdb']:shutil.copy2(built.with_suffix(ext),folder/(name+ext))
        path=folder/(name+'.dll');paths += [path,path.with_suffix('.pdb')]
        check(role+' frozen DLL/PDB copy',all(sha(built.with_suffix(ext))==sha(path.with_suffix(ext)) for ext in ['.dll','.pdb']))
        artifacts[role]=path.relative_to(ROOT).as_posix()
        dll=ctypes.WinDLL(str(path));fn=dll.M11Prepare;fn.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32];fn.restype=ctypes.c_uint32
        log=dest.parent/f'MUST_NOT_EXIST_{dest.stem}_{role}.jsonl'
        rc=fn(str(log),ctypes.windll.kernel32.GetCurrentThreadId())
        check(role+' rejects foreign process before native work',rc==193 and not log.exists(),code=rc)
        if role=='client': check('atomic background export',hasattr(dll,'PSE_P4_EnableBackground'))
    for path in (LAB/'tools').glob('*.py'): ast.parse(path.read_text(encoding='utf8'))
    check('Python syntax',True)
    sources=[p for p in LAB.rglob('*') if p.is_file() and not any(x.startswith('runtime') or x=='__pycache__' for x in p.relative_to(LAB).parts)]
    copies=[]
    for p in sorted(sources):
        q=snapshot/'sources'/p.relative_to(ROOT);q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q)
        copies.append(dict(path=q.relative_to(ROOT).as_posix(),source=p.relative_to(ROOT).as_posix(),sha256=sha(q)))
    baseline=json.loads((LAB/'src/wire111/BASELINE.json').read_text(encoding='utf8'))
    check('v110 sources remain frozen',all(sha(ROOT/r['source'])==r['baseline_sha256'] for r in baseline))
    result=dict(status='PASS' if all(c['passed'] for c in checks) else 'FAIL',checks=checks,
        frozen=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in paths],
        sources=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in sorted(sources)],
        source_copies=copies,artifacts=artifacts,build_dir=a.build_dir,
        game_started=False,runtime_acceptance=False,offline=a.offline)
    dest.write_text(json.dumps(result,indent=2),encoding='utf8');print(result['status'],len(checks),'checks; NOT runtime acceptance')
    return 0 if result['status']=='PASS' else 1
if __name__=='__main__': raise SystemExit(main())
