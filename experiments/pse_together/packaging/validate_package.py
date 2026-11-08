"""Accept the actual P25 ZIP in a fresh workspace directory. Never starts KEX.

--out must be NEW; every attempt keeps raw stdout/stderr and partial receipts.
"""
from pathlib import Path
import argparse
import contextlib
import hashlib
import importlib.util
import io
import json
import os
import socket
import struct
import subprocess
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent


def sha(p):
    with Path(p).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def load(p):
    return json.loads(Path(p).read_text('utf-8-sig'))


def write(p, value):
    Path(p).write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', 'utf8')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dist', required=True)
    ap.add_argument('--out', required=True)
    # Build dirs the dist was packaged from (defaults = the accepted P25 dirs).
    ap.add_argument('--transport', default='b85t')
    ap.add_argument('--diagnostics', default='b85p')
    args = ap.parse_args()
    dist, out = (ROOT / args.dist).resolve(), (ROOT / args.out).resolve()
    if not out.is_relative_to(ROOT) or out == ROOT:
        raise ValueError('Output must stay inside workspace')
    out.mkdir(parents=True, exist_ok=False)
    checks = []
    children = []

    def check(name, value, **details):
        checks.append(dict(name=name, passed=bool(value), **details))
        write(out / 'CHECKS.json', checks)
        if not value:
            raise AssertionError(name)

    def run(label, argv, **kw):
        r = subprocess.run([str(v) for v in argv], capture_output=True, timeout=30, **kw)
        (out / (label + '.stdout')).write_bytes(r.stdout)
        (out / (label + '.stderr')).write_bytes(r.stderr)
        return r

    try:
        kit = out / 'Чистая папка друга'
        with zipfile.ZipFile(dist / 'PSE-P25-DIAGNOSTIC-1.zip') as z:
            names = z.namelist()
            check('archive has no traversal or game resources', all(
                not Path(n).is_absolute() and '..' not in Path(n).parts and ':' not in n and '\\' not in n
                and not n.lower().endswith(('.kpf', '.sav', '.pack')) and 'anubis' not in n.lower() for n in names))
            z.extractall(kit)
        manifest = load(kit / 'manifest.json')
        check('all extracted hashes and sizes', all(sha(kit / r['path']) == r['sha256'] and
              (kit / r['path']).stat().st_size == r['size'] for r in manifest['files']))
        check('editable config excluded from hashes', 'friends.json' not in [r['path'] for r in manifest['files']])
        check('crash fixture absent from release', not any('fixture' in n for n in names))
        # Parse PE imports: runtime portability must not depend on this workstation's VC install.
        spec = importlib.util.spec_from_file_location('p25_pe', ROOT / 'docs/research/inventory.py')
        pe_module = importlib.util.module_from_spec(spec); spec.loader.exec_module(pe_module)
        imports = {}
        for p in (kit / 'bin').glob('*.exe'):
            pe = pe_module.PE(p)
            imports[p.name] = [i['dll'] for i in pe.imports()]
            check('x64 static runtime ' + p.name, pe.machine == 0x8664 and not any(
                v.lower().startswith(('vcruntime', 'msvcp', 'ucrtbase')) for v in imports[p.name]), imports=imports[p.name])
        script = kit / 'friends.ps1'
        # Explicit Windows PowerShell 5.1; working directory is unrelated to the kit.
        ps = [str(Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'),
              '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(script)]
        env = dict(os.environ)
        env['PATH'] = str(Path(os.environ['SystemRoot']) / 'System32')
        env.pop('PYTHONPATH', None)
        cwd = out
        def command(label, *cli, **kw):
            return run(label, ps + list(cli), cwd=cwd, env=env, **kw)

        r = command('verify', 'verify')
        check('Windows PowerShell clean-directory verification', r.returncode == 0 and b'Config valid' in r.stdout)
        r = run('bat_verify', ['cmd.exe', '/d', '/c', str(kit / 'verify.bat')], input=b'\n', cwd=cwd, env=env)
        check('double-click batch verification entry point', r.returncode == 0 and b'Config valid' in r.stdout)
        config = load(kit / 'friends.json')
        invalid = [dict(port=0), dict(port=65536), dict(port='27015'), dict(schema=2),
                   dict(host='8.8.8.8'), dict(bind='localhost'), dict(host='127.0.0.999'),
                   dict(token='bad token'), dict(name='x";quit'), dict(max_clients=8),
                   dict(crash_dumps='true'), dict(extra=1)]
        for i, delta in enumerate(invalid):
            write(kit / 'invalid.json', dict(config, **delta))
            r = command(f'invalid_{i:02}', 'host', '-Config', 'invalid.json')
            check('reject invalid config ' + str(i), r.returncode == 2 and not (kit / 'logs').exists(), config_delta=delta)
        (kit / 'invalid.json').write_text('{broken', 'utf8')
        check('reject malformed JSON', command('malformed', 'host', '-Config', 'invalid.json').returncode == 2)
        check('reject config traversal', command('traversal', 'host', '-Config', '../friends.json').returncode == 2)
        exe = kit / 'bin/pse_host.exe'
        original_bytes = exe.read_bytes()
        exe.write_bytes(original_bytes[:-1] + bytes([original_bytes[-1] ^ 1]))
        r = command('tampered', 'host')
        check('tampered executable refused before launch', r.returncode == 2 and b'Package hash mismatch' in r.stderr)
        exe.write_bytes(original_bytes)
        # New network test is of the shipped wrapper/receipt, not a repeated KEX run.
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.bind(('127.0.0.1', 0)); port = s.getsockname()[1]
        config.update(port=port, token='p25-private-token')
        write(kit / 'friends.json', config)
        for role in ['host', 'client']:
            stdout = (out / (role + '.stdout')).open('wb')
            stderr = (out / (role + '.stderr')).open('wb')
            p = subprocess.Popen(ps + [role], cwd=cwd, env=env, stdin=subprocess.PIPE, stdout=stdout, stderr=stderr)
            children.append((p, stdout, stderr))
            # A freshly unpacked unsigned EXE can be held by real-time antivirus/SmartScreen on its first start; 20 s was
            # too tight on a loaded machine (one reported failure, not reproducible: the same ZIP passed 46/46 next run).
            started = time.monotonic()
            deadline = started + 60
            marker = 'Type' if role == 'host' else 'Authenticated!'
            found = False
            while time.monotonic() < deadline and p.poll() is None:
                logs = list((kit / 'logs').glob(role + '_*/console.log'))
                if logs and marker in logs[-1].read_text('utf8', errors='replace'):
                    found = True; break
                time.sleep(.05)
            logs_now = list((kit / 'logs').glob(role + '_*/console.log'))
            check('shipped ' + role + ' started/authenticated', found, waited_s=round(time.monotonic() - started, 1), wrapper_alive=p.poll() is None,
                  console_log_bytes=[x.stat().st_size for x in logs_now])
        for p, _, _ in reversed(children):
            p.stdin.write(b'status\nstat\nquit\n'); p.stdin.flush(); p.stdin.close()
            check('shipped wrapper clean exit ' + str(p.pid), p.wait(timeout=15) == 0)
        for p, stdout, stderr in children:
            stdout.close(); stderr.close()
        sessions = list((kit / 'logs').glob('*/session.json'))
        check('two complete per-session receipts without token', len(sessions) == 2 and all(
            load(p)['exit_code'] == 0 and config['token'] not in p.read_text('utf-8-sig') for p in sessions))
        # A separate real native crash fixture tests second-chance exception context.
        fixture = out / 'fixture'
        fixture.mkdir()
        import shutil
        shutil.copyfile(kit / 'bin/pse-diagnostics.exe', fixture / 'pse-diagnostics.exe')
        shutil.copyfile(ROOT / 'b85p/Release/p25-crash-fixture.exe', fixture / 'pse_host.exe')
        dump_dir = fixture / 'crashed'
        r = run('native_crash', [fixture / 'pse-diagnostics.exe', dump_dir, 'host'], input=b'')
        check('real unhandled native crash', (r.returncode & 0xffffffff) == 0xE0250001)
        dump = (dump_dir / 'crash.dmp').read_bytes()
        signature, version, streams, directory = struct.unpack_from('<4I', dump)
        exception = None
        for i in range(streams):
            kind, size, offset = struct.unpack_from('<3I', dump, directory + i * 12)
            if kind == 6:
                exception = dict(thread=struct.unpack_from('<I', dump, offset)[0],
                                 code=struct.unpack_from('<I', dump, offset + 8)[0], size=size)
        check('actual MDMP with exception stream and correct code', signature == 0x504d444d and exception and
              exception['code'] == 0xE0250001 and exception['thread'] != 0, exception=exception, dump_size=len(dump))
        check('dump success separately recorded', 'crash_dump_written code=0' in (dump_dir / 'diagnostics.log').read_text())
        keep = kit / 'logs/crash-fixture'
        shutil.copytree(dump_dir, keep)
        private = keep / 'session.json'
        write(private, dict(path=str(kit), profile=os.environ.get('USERPROFILE', ''), mac='AA-BB-CC-DD-EE-FF'))
        check('default collection succeeds', command('collect_default', 'collect').returncode == 0)
        archives = set(kit.glob('results_*.zip'))
        with zipfile.ZipFile(next(iter(archives))) as z:
            check('collection excludes dumps and config', not any(n.endswith(('.dmp', 'friends.json')) for n in z.namelist()))
            all_text = '\n'.join(z.read(n).decode('utf-8-sig') for n in z.namelist())
            check('collection redacts known paths/MAC and excludes token', str(kit) not in all_text and
                  'AA-BB-CC-DD-EE-FF' not in all_text and config['token'] not in all_text and '<MAC>' in all_text)
        check('dump opt-in collection succeeds', command('collect_dumps', 'collect', '-IncludeDumps').returncode == 0)
        with zipfile.ZipFile(next(iter(set(kit.glob('results_*.zip')) - archives))) as z:
            check('opt-in includes exact real dump', z.read('crash-fixture/crash.dmp') == dump)
        # Disabled capture still creates a successful text-only session.
        config['crash_dumps'] = False
        write(kit / 'friends.json', config)
        check('capture can be disabled', command('no_dump', 'host', input=b'quit\n').returncode == 0)
        check('config survives launch unchanged', load(kit / 'friends.json') == config)
        config['crash_dumps'] = True
        write(kit / 'friends.json', config)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as occupied:
            occupied.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            occupied.bind(('127.0.0.1', port))
            check('ordinary initialization failure propagated', command('occupied_port', 'host', input=b'quit\n').returncode == 1)
        receipts = [load(p) for p in (kit / 'logs').glob('host_*/session.json')]
        check('nonzero exit receipt without false crash claim', any(r['exit_code'] == 1 for r in receipts) and
              not any((p.parent / 'crash.dmp').exists() for p in (kit / 'logs').glob('host_*/session.json')))
        # Exact EXE checker is read-only and carries the frozen P24 hash.
        real_exe = ROOT / 'experiments/m0/game/anubis_Shipping_Steam_x64.exe'
        r = run('exe_check', [kit / 'native-reference/pe-check.exe', real_exe])
        check('frozen checker accepts exact EXE', r.returncode == 0 and manifest['exe_sha256'] in r.stdout.decode('utf-16-le'))
        r = run('exe_missing', [kit / 'native-reference/pe-check.exe', out / 'missing.exe'])
        check('frozen checker refuses missing EXE', r.returncode == 2)
        check('package manifest unchanged after all runs', all(sha(kit / r['path']) == r['sha256'] for r in manifest['files']))
        # Deterministic rebuild of the same inputs, including separate symbol archive.
        from build_package import build
        rebuild = out / 'rebuild'
        with contextlib.redirect_stdout(io.StringIO()):
            build(rebuild, ROOT / args.transport, ROOT / args.diagnostics)
        for name in ['PSE-P25-DIAGNOSTIC-1.zip', 'symbols.zip']:
            check('reproducible archive ' + name, sha(rebuild / name) == sha(dist / name))
        status = 'PASS'
    except Exception as exc:
        import traceback
        (out / 'ERROR.txt').write_text(traceback.format_exc(), 'utf8')
        status = 'FAIL'
        print(str(exc), file=sys.stderr)
    finally:
        for p, stdout, stderr in children:
            if p.poll() is None:
                subprocess.run(['taskkill', '/PID', str(p.pid), '/T', '/F'], capture_output=True)
                p.wait(timeout=15)
            stdout.close(); stderr.close()
        result = dict(status=status, passed=sum(c['passed'] for c in checks), total=len(checks), checks=checks,
                      scope='Portable diagnostic ZIP, fresh directory on existing workstation; not a clean OS or KEX acceptance.')
        write(out / 'RESULT.json', result)
        print(status, result['passed'], '/', result['total'])
    return 0 if status == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
