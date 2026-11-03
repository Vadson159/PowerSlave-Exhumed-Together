"""Build with the installed MSVC/SDK; preserve every build attempt and its output."""
from pathlib import Path
import datetime
import hashlib
import json
import subprocess
import sys

LAB = Path(__file__).resolve().parent
ROOT = LAB.parents[2]
EVIDENCE = ROOT / 'docs/evidence/m1/M11_IMPLEMENTATION_20261101'


def main():
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
    commands = [
        [sys.executable, '-B', str(LAB / 'generate_profile.py')],
        ['C:/Program Files/CMake/bin/cmake.exe', '-S', str(LAB), '-B', str(LAB / 'build'),
         '-G', 'Visual Studio 17 2022', '-A', 'x64', '-DCMAKE_SYSTEM_VERSION=10.0.26100.0'],
        ['C:/Program Files/CMake/bin/cmake.exe', '--build', str(LAB / 'build'), '--config', 'Release', '--parallel', '2']]
    records = []
    for n, cmd in enumerate(commands):
        result = subprocess.run(cmd, cwd=ROOT, capture_output=True)
        path = EVIDENCE / f'BUILD_{stamp}_{n}.log'
        path.write_bytes(result.stdout + b'\nSTDERR:\n' + result.stderr)
        records.append(dict(argv=cmd, exit_code=result.returncode, log=path.name))
        print(result.stdout.decode(errors='replace')[-5000:])
        print(result.stderr.decode(errors='replace')[-2000:])
        if result.returncode:
            break
    dll = LAB / 'build/Release/pse_m11_bootstrap.dll'
    report = dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  status='PASS' if len(records) == 3 and not records[-1]['exit_code'] else 'FAIL', commands=records,
                  sources=[dict(path=str(p.relative_to(ROOT)), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                           for p in sorted(LAB.rglob('*')) if p.is_file() and ('src' in p.parts or p.suffix == '.py' or p.name == 'CMakeLists.txt') and 'build' not in p.parts])
    if report['status'] == 'PASS':
        report['dll'] = dict(path=str(dll.relative_to(ROOT)), sha256=hashlib.sha256(dll.read_bytes()).hexdigest(), size=dll.stat().st_size)
    (EVIDENCE / f'BUILD_{stamp}.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    print(json.dumps({k: v for k, v in report.items() if k != 'sources'}))
    raise SystemExit(report['status'] != 'PASS')


if __name__ == '__main__':
    main()
