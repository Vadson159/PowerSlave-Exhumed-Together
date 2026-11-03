"""Retain every M1.2a build and exact source/EXE manifest, including failures."""
from pathlib import Path
import datetime, hashlib, json, subprocess, sys
LAB=Path(__file__).resolve().parent
ROOT=LAB.parents[2]
OUT=ROOT/'docs/evidence/m1/M12A_IMPLEMENTATION_20261102'
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    OUT.mkdir(exist_ok=True,parents=True)
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
    cmake='C:/Program Files/CMake/bin/cmake.exe'
    exe=ROOT/'experiments/m0/game/anubis_Shipping_Steam_x64.exe'
    assert digest(exe)=='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
    targets=sys.argv[1:] or ['pse_m12a','m12a_generation_test','m12a_adapter_test','m12a_native_client']
    builddir=LAB/'build'
    if all(t in ['pse_test_suite','pse_stress_test','pse_fault_injection','pse_bridge_acceptance'] for t in targets): builddir/='transport'
    commands=[[cmake,'-S',str(LAB),'-B',str(LAB/'build'),'-G','Visual Studio 17 2022','-A','x64','-DCMAKE_SYSTEM_VERSION=10.0.26100.0'],
              [cmake,'--build',str(builddir),'--config','Release','--parallel','2','--target']+targets]
    records=[]
    for n,c in enumerate(commands):
        r=subprocess.run(c,cwd=ROOT,capture_output=True)
        log=OUT/f'BUILD_{stamp}_{n}.log'; log.write_bytes(r.stdout+b'\nSTDERR:\n'+r.stderr)
        records.append(dict(argv=c,exit_code=r.returncode,log=log.name))
        print(r.stdout.decode(errors='replace')[-4500:])
        if r.returncode: break
    sources=[]
    for root in [LAB,ROOT/'experiments/m1/native_bootstrap/src',ROOT/'tools/enet-prototype']:
        for p in sorted(root.rglob('*')):
            if p.is_file() and not {'build','profile','field-test','__pycache__'}.intersection(p.parts) and p.suffix in ['.cpp','.c','.h','.hpp','.py','.txt']:
                sources.append(dict(path=str(p.relative_to(ROOT)),sha256=digest(p)))
    artifacts=[]
    for p in (LAB/'build/Release').glob('*'):
        if p.suffix in ['.dll','.exe','.pdb']: artifacts.append(dict(path=str(p.relative_to(ROOT)),sha256=digest(p),size=p.stat().st_size))
    report=dict(commands=records,status='PASS' if len(records)==2 and not records[-1]['exit_code'] else 'FAIL',exe_sha256=digest(exe),sources=sources,artifacts=artifacts)
    (OUT/f'BUILD_{stamp}.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    print(report['status']); raise SystemExit(report['status']!='PASS')
if __name__=='__main__': main()
