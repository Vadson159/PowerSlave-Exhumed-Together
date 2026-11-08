"""P23 source/artifact receipts, without launching KEX/REA or changing old evidence."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil

ROOT = Path(__file__).resolve().parents[3]
LAB = ROOT / 'experiments/pse_together'
E = ROOT / 'docs/evidence/m1/P23_UI'


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    out = (ROOT / args.out).resolve()
    if not out.is_relative_to(ROOT) or out.exists():
        raise ValueError('New workspace output directory required')
    out.mkdir(parents=True)
    checks = []

    def check(name, truth, **detail):
        checks.append(dict(name=name, passed=bool(truth), **detail))

    # Compare to the source snapshot actually frozen before accepted P7 RUN03,
    # not to a newly created baseline which would hide preexisting differences.
    accepted = ROOT / 'docs/evidence/m1/PSE_TOGETHER_P7/PRE_NATIVE_RUN03_snapshot/sources/experiments/pse_together/src'
    for version in ['wire111', 'wire112']:
        records = []
        for old in sorted((accepted / version).rglob('*')):
            if not old.is_file():
                continue
            current = LAB / 'src' / version / old.relative_to(accepted / version)
            records.append(dict(path=current.relative_to(ROOT).as_posix(), baseline=str(old.relative_to(ROOT)),
                                sha256=sha(current), baseline_sha256=sha(old), passed=sha(current) == sha(old)))
        check(version + ' equals accepted source snapshot', records and all(r['passed'] for r in records), files=records)
    legacy = ROOT / 'tools/enet-prototype/include/net_protocol.h'
    check('frozen v1.10 header', sha(legacy) == '8272aa3ef7bcd9e555e8a7b6bc12a1fe320b378dc34c8edbe6b38891e3bbac0f', sha256=sha(legacy))
    independent = {
        'p23_ui_contract.exe': 'c5be860e40b4fb7a838270142784cd726768dbd767d1f64979f8790afe3d1c23',
        'p23_lobby_live.exe': 'ff8a35e44902cc4ee70c5b3979396054af350805d5bd516a0f14928ad7d80661',
    }
    artifacts = []
    for name, expected in independent.items():
        p = ROOT / 'b84/Release' / name
        artifacts.append(dict(path=p.relative_to(ROOT).as_posix(), sha256=sha(p), independent_sha256=expected))
        check('same independent reviewed artifact ' + name, sha(p) == expected)
    sources = []
    files = set()
    for folder in ['src/ui', 'src/wire113', 'tests/ui']:
        files.update(p for p in (LAB / folder).rglob('*') if p.is_file())
    files.update(LAB / name for name in ['CMakeLists.txt', 'cmake/p23_ui.cmake', 'tools/expected_suites.json', 'tools/verify_p23.py'])
    for p in sorted(files):
        rel = p.relative_to(ROOT)
        dest = out / 'source_snapshot' / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(p, dest)
        sources.append(dict(path=rel.as_posix(), snapshot=dest.relative_to(out).as_posix(), sha256=sha(p)))
    final = json.loads((E / 'OFFLINE_FINAL/RESULT.json').read_text('utf8'))
    check('fresh complete CTest34/34', final['status'] == 'PASS' and final['passed'] == final['total_expected'] == 34)
    for phase in ['BEFORE', 'AFTER']:
        root = E / ('INTEGRITY_' + phase)
        receipt = json.loads((root / 'RESULT.json').read_text('utf8'))
        check('current integrity + saved exact restores ' + phase, receipt['status'] == 'PASS')
    result = dict(machine_checks='PASS' if all(c['passed'] for c in checks) else 'FAIL',
                  passed=sum(c['passed'] for c in checks), total=len(checks), checks=checks,
                  artifacts=artifacts, sources=sources, package_status='REVIEW / PARTIAL',
                  native_ui_runtime_observed=False, native_hooks_added=False,
                  note='Backend source/artifact proof only; does not close S8.1/S8.2/S8.3.')
    (out / 'SOURCE_CHECKS.json').write_text(json.dumps(result, indent=2), 'utf8')
    print(result['machine_checks'], result['passed'], '/', result['total'], 'package REVIEW / PARTIAL')
    return 0 if result['machine_checks'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
