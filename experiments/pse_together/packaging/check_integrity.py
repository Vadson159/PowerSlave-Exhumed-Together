"""P25 final read-only integrity + reuse of accepted P24 raw-evidence audits.

Redirect audit output only; never overwrite earlier evidence or start KEX/REA.
"""
from pathlib import Path
import argparse
import ast
import contextlib
import hashlib
import io
import json
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def sha(p):
    with Path(p).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    out = (ROOT / args.out).resolve()
    if not out.is_relative_to(ROOT) or out == ROOT:
        raise ValueError('Output must stay in workspace')
    out.mkdir(parents=True, exist_ok=False)
    checks, reused = [], []
    for name in ['audit83.py', 'supplement83.py']:
        source = ROOT / 'docs/evidence/m1/P24_ACCEPTANCE_83' / name
        tree = ast.parse(source.read_text('utf8'), filename=str(source))
        # Preserve every predicate; redirect just the two path constants.
        tree.body = [node for node in tree.body if not (
            isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id in {'ROOT', 'OUT'} for t in node.targets))]
        namespace = dict(ROOT=ROOT, OUT=out, __file__=str(source), __name__='__main__')
        output = io.StringIO()
        code = 0
        with contextlib.redirect_stdout(output):
            try:
                exec(compile(tree, str(source), 'exec'), namespace)
            except SystemExit as exc:
                code = exc.code
        (out / (name + '.stdout')).write_text(output.getvalue(), 'utf8')
        reused.append(dict(source=source.relative_to(ROOT).as_posix(), sha256=sha(source),
                           transformation='Only ROOT/OUT assignments replaced; all predicates unmodified', exit_code=code))
        checks.append(dict(name='retained raw evidence ' + name, passed=code == 0))
    # Hash/size enumeration receipts from supplement include all actual protected roots.
    script = "@(@(Get-CimInstance Win32_Process | Where-Object { $_.Name -in @('anubis_Shipping_Steam_x64.exe','pse_host.exe','pse_client.exe','pse-diagnostics.exe','p25-crash-fixture.exe') } | Select-Object ProcessId,Name,ExecutablePath)) | ConvertTo-Json -Compress"
    p = subprocess.run(['powershell', '-NoProfile', '-Command', script], capture_output=True, timeout=30)
    (out / 'processes.stdout').write_bytes(p.stdout)
    (out / 'processes.stderr').write_bytes(p.stderr)
    processes = json.loads(p.stdout) if p.stdout.strip() else []
    checks.append(dict(name='no game or diagnostic processes remain', passed=p.returncode == 0 and not processes))
    receipt = dict(status='PASS' if all(c['passed'] for c in checks) else 'FAIL', checks=checks, reused=reused,
                   game_launched=False, rea_sessions_created=0,
                   scope='Current full original77/copies/actual5 protected hashes and retained exact restores; not a new runtime run.')
    (out / 'RESULT.json').write_text(json.dumps(receipt, indent=2), 'utf8')
    print(receipt['status'])
    return 0 if receipt['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
