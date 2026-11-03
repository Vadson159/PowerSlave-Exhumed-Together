from pathlib import Path
import datetime, json, subprocess, hashlib
LAB=Path(__file__).resolve().parent
ROOT=LAB.parents[2]
OUT=ROOT/'docs/evidence/m1/M12A_IMPLEMENTATION_20261102'
stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%H%M%S_%f')
results=[]
for name in ['pse_test_suite','pse_stress_test','pse_fault_injection','pse_bridge_acceptance']:
    exe=LAB/'build/transport/Release'/f'{name}.exe'
    log=OUT/f'{name}_{stamp}.log'
    with log.open('xb') as f:
        try:
            r=subprocess.run([str(exe)],cwd=OUT,stdout=f,stderr=subprocess.STDOUT,timeout=240)
            code=r.returncode
        except subprocess.TimeoutExpired: code='TIMEOUT'
    results.append(dict(name=name,exit_code=code,log=log.name,sha256=hashlib.sha256(exe.read_bytes()).hexdigest()))
    print(json.dumps(results[-1]),flush=True)
(OUT/f'REGRESSION_{stamp}.json').write_text(json.dumps(results,indent=2),encoding='utf8')
raise SystemExit(any(r['exit_code']!=0 for r in results))
