"""pse_together: run ALL offline suites with one command (S0.3).

    python -X utf8 -B experiments/pse_together/tools/run_offline.py [--out DIR] [--no-build] [--keep-going]

What it does (nothing here starts the game, Frida/REA, a DLL loader or a second process of KEX):
  1. exact-EXE precondition (SHA-256 of both game copies; read-only);
  2. cmake configure + build of target `pse_all` (offline suites + all four DLL variants) into the short tree b60/;
  3. `ctest` over every registered suite (C++ contracts, transport/bridge suites, loopback ENet suites, JS controllers);
  4. verification of each suite's own summary line against tools/expected_suites.json (exit code 0 alone is not accepted);
  5. RESULT.json + per-suite logs in --out (default: docs/evidence/m1/PSE_TOGETHER_S03/RUN_<utc stamp>).
Exit code 0 only when every expected suite is registered, exits 0 and prints its expected summary.
These are offline contract/mock tests; they are NOT runtime (KEX) acceptance."""
from pathlib import Path
import argparse, datetime, hashlib, json, os, re, shutil, subprocess, sys, xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
LAB = ROOT / 'experiments/pse_together'
BUILD = ROOT / 'b60'
EXPECTED_EXE = 'b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
EXES = [ROOT / 'experiments/m0/game/anubis_Shipping_Steam_x64.exe', ROOT / 'experiments/m1/native_second_instance/game/anubis_Shipping_Steam_x64.exe']
CMAKE_DIRS = ['C:/Program Files/CMake/bin', 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin']
ANSI = re.compile(r'\x1b\[[0-9;]*m')

def tool(name):
    p = shutil.which(name)
    if p: return p
    for d in CMAKE_DIRS:
        c = Path(d) / (name + '.exe')
        if c.is_file(): return str(c)
    raise SystemExit(f'{name} not found')
def sha(p):
    h = hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''): h.update(b)
    return h.hexdigest()
def run(argv, log, cwd=None, timeout=1800):
    with open(log, 'wb') as f:
        r = subprocess.run([str(a) for a in argv], cwd=cwd, stdout=f, stderr=subprocess.STDOUT, timeout=timeout)
    return r.returncode

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out'); ap.add_argument('--no-build', action='store_true'); ap.add_argument('--keep-going', action='store_true')
    ap.add_argument('--build-dir',default='b60',help='Short private build tree inside the mod; default b60')
    a = ap.parse_args()
    global BUILD
    BUILD=(ROOT/a.build_dir).resolve()
    if BUILD.parent!=ROOT or not re.fullmatch(r'b[0-9][0-9a-z_]*',BUILD.name):ap.error('build-dir must be a short b<number> tree in the mod root')
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S')
    out = (Path(a.out) if a.out else ROOT / 'docs/evidence/m1/PSE_TOGETHER_S03' / f'RUN_{stamp}').resolve()
    out.mkdir(parents=True, exist_ok=False)
    expected = json.loads((LAB / 'tools/expected_suites.json').read_text(encoding='utf8'))['suites']
    result = dict(utc=stamp, out=str(out), exe_expected=EXPECTED_EXE, steps={}, suites=[])
    ok = True
    # 1. exact EXE precondition
    exe = {str(p.relative_to(ROOT)): sha(p) for p in EXES}
    result['exe_sha256'] = exe
    if any(v != EXPECTED_EXE for v in exe.values()):
        result['error'] = 'EXE hash mismatch'; (out / 'RESULT.json').write_text(json.dumps(result, indent=2), encoding='utf8'); print('FAIL: EXE hash'); return 2
    cmake, ctest = tool('cmake'), tool('ctest')
    # 2. configure + build
    if not a.no_build:
        c = run([cmake, '-S', LAB, '-B', BUILD, '-G', 'Visual Studio 17 2022', '-A', 'x64', '-DCMAKE_SYSTEM_VERSION=10.0.26100.0'], out / 'configure.log')
        b = run([cmake, '--build', BUILD, '--config', 'Release', '--parallel', '4', '--target', 'pse_all'], out / 'build.log') if c == 0 else -1
        result['steps'] = dict(configure=c, build=b)
        text = (out / 'build.log').read_text(encoding='utf8', errors='replace') if b != -1 else ''
        own = sorted({m.group(0).strip() for m in re.finditer(r'[^\r\n]*experiments\\pse_together\\[^\r\n]*(warning|error) [A-Z]+\d+[^\r\n]*', text)})
        result['own_code_diagnostics'] = own
        if c != 0 or b != 0:
            ok = False; print('FAIL: configure/build', result['steps'])
    if ok:
        # 3. ctest, junit carries each suite's stdout
        junit = out / 'ctest_junit.xml'
        env = dict(os.environ)
        rc = run([ctest, '-C', 'Release', '--output-junit', junit, '--timeout', '600', '--test-output-size-passed', '8388608', '--test-output-size-failed', '8388608'], out / 'ctest.log', cwd=BUILD)
        result['steps']['ctest_exit'] = rc
        got = {}
        if junit.is_file():
            for tc in ET.parse(junit).getroot().iter('testcase'):
                so = tc.find('system-out')
                got[tc.get('name')] = dict(status=tc.get('status'), time=float(tc.get('time') or 0), out=(so.text or '') if so is not None else '',
                                           failed=tc.find('failure') is not None)
        (out / 'logs').mkdir()
        # 4. verify
        for name, spec in expected.items():
            g = got.get(name)
            rec = dict(name=name, registered=g is not None, doc=spec['doc'], expect=spec['patterns'])
            if g is not None:
                txt = ANSI.sub('', g['out'])
                (out / 'logs' / f'{name}.log').write_text(txt, encoding='utf8')
                rec.update(status=g['status'], seconds=g['time'], ctest_failed=g['failed'],
                           matched=[bool(re.search(p, txt, re.M)) for p in spec['patterns']], log=f'logs/{name}.log')
                rec['verdict'] = 'PASS' if g['status'] == 'run' and not g['failed'] and all(rec['matched']) else 'FAIL'
            else:
                rec['verdict'] = 'MISSING'
            ok &= rec['verdict'] == 'PASS'
            result['suites'].append(rec)
        result['unexpected_registered'] = sorted(set(got) - set(expected))
        ok &= not result['unexpected_registered']
    result['passed'] = sum(s['verdict'] == 'PASS' for s in result['suites'])
    result['total_expected'] = len(expected)
    result['status'] = 'PASS' if ok else 'FAIL'
    (out / 'RESULT.json').write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf8')
    for s in result['suites']: print(f"{s['verdict']:7} {s['name']}")
    print(f"{result['status']}: {result['passed']}/{result['total_expected']} suites; evidence {out}")
    return 0 if ok else 1

if __name__ == '__main__':
    sys.exit(main())
