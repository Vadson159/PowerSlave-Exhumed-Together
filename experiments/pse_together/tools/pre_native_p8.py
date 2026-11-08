"""P8 pre-native gate. No game execution. Freeze exact built DLLs and sources."""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import shutil
import dll_compare
import build_player_sprites as assets
import build_expedition_sprites as expedition

ROOT=Path(__file__).resolve().parents[3]
LAB=ROOT/'experiments/pse_together'
EXPECTED='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--offline',required=True);ap.add_argument('--out',required=True);a=ap.parse_args()
    out=(ROOT/a.out).resolve();assert out.is_relative_to(ROOT) and not out.exists()
    checks=[];frozen=[]
    def check(n,ok,**details): checks.append(dict(name=n,passed=bool(ok),**details))
    result=json.loads((ROOT/a.offline).read_text())
    check('complete offline suites',result['status']=='PASS' and result['passed']==result['total_expected']==27)
    package=expedition.DEST
    manifest=json.loads(package.with_suffix('.manifest.json').read_text())
    with assets.zipfile.ZipFile(package) as z:
        check('new atlas bytes match source PNGs',all(assets.sha(z.read('sprites/player/'+p['name']+'.png'))==p['sha256'] for p in manifest['profiles']))
    check('two original archetypes, 80 rectangles',len(manifest['profiles'])==2 and all(len(p['rectangles'])==40 for p in manifest['profiles']))
    paths=[package,package.with_suffix('.manifest.json')]
    for role,game,name in [('host','experiments/m0/game','pse_m11_bootstrap'),('client','experiments/m1/native_second_instance/game','pse_native_client58b')]:
        exe=ROOT/game/'anubis_Shipping_Steam_x64.exe';check(role+' exact EXE',sha(exe)==EXPECTED)
        path=ROOT/f'b68/dll/pse_dll_{role}_p8/{name}.dll';paths += [path,path.with_suffix('.pdb')]
        dll=ctypes.WinDLL(str(path));fn=dll.M11Prepare;fn.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32];fn.restype=ctypes.c_uint32
        log=out.parent/f'MUST_NOT_EXIST_{role}.jsonl';rc=fn(str(log),ctypes.windll.kernel32.GetCurrentThreadId())
        check(role+' rejects foreign process',rc==193 and not log.exists(),code=rc)
        if role=='client':
            preview=dll.PSE_P8_Preview;preview.argtypes=[ctypes.c_uint32,ctypes.c_uint32];preview.restype=ctypes.c_uint32
            check('preview only publishes validated atomic',preview(9,0)==87 and preview(0,4)==87 and preview(8,0)==0)
    # Compare current legacy targets before/after P8, not against accepted P1
    # (DOC-67 F1 already established those as different).
    for target,name in [('pse_dll_host','pse_m11_bootstrap'),('pse_dll_client_proxy','pse_native_client58b')]:
        cmp=dll_compare.compare(ROOT/f'b60/dll/{target}/{name}.dll',ROOT/f'b68/dll/{target}/{name}.dll')
        check(target+' unchanged by P8',cmp['all_sections_equal_masked'],comparison=cmp)
    src=out.parent/(out.stem+'_sources');assert not src.exists();src.mkdir()
    sources=[]
    for p in sorted(LAB.rglob('*')):
        if not p.is_file() or {'runtime','runtime_p4','runtime_p8','__pycache__'}.intersection(p.relative_to(LAB).parts):continue
        rel=p.relative_to(LAB);dest=src/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest)
        sources.append(dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)))
    for p in paths:
        frozen.append(dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)))
    status='PASS' if all(c['passed'] for c in checks) else 'FAIL'
    out.write_text(json.dumps(dict(status=status,checks=checks,frozen=frozen,sources=sources,game_started=False),indent=2),encoding='utf8')
    print(status,len(checks),'checks; NOT runtime acceptance')
    return status!='PASS'
if __name__=='__main__':raise SystemExit(main())
