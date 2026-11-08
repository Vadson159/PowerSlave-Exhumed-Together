"""P25 packaging only: no game launch, game assets, native source or protocol edits.

python -X utf8 -B experiments/pse_together/packaging/build_package.py --out <NEW workspace directory>
Requires standalone transport build (default b85t) and diagnostics build (b85p).
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
EXE_SHA = 'b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'


def sha(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def write(path, data):
    Path(path).write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf8')


def archive(source, output):
    # Stable timestamps and order make rebuilds from identical inputs byte-reproducible.
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED) as z:
        for path in sorted(source.rglob('*')):
            if path.is_file():
                info = zipfile.ZipInfo(path.relative_to(source).as_posix(), (2026, 11, 5, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                z.writestr(info, path.read_bytes())


def build(out, transport, diagnostics):
    out = Path(out).resolve()
    if not out.is_relative_to(ROOT) or out == ROOT:
        raise ValueError('Output must be a new directory inside the mod workspace')
    out.mkdir(parents=True, exist_ok=False)
    kit = out / 'PSE-P25-DIAGNOSTIC-1'
    kit.mkdir()
    provenance = []

    def copy(source, relative, expected=None):
        source = Path(source)
        actual = sha(source)
        if expected and actual != expected:
            raise ValueError(f'Frozen SHA mismatch: {source}')
        dest = kit / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
        provenance.append(dict(path=relative, source=source.relative_to(ROOT).as_posix(), sha256=actual))

    for name in ['friends.ps1', 'friends.json', 'README_RU.md', 'README_EN.md']:
        copy(HERE / name, name)
    for name in ['pse_host.exe', 'pse_client.exe']:
        copy(transport / 'Release' / name, 'bin/' + name)
    copy(diagnostics / 'Release/pse-diagnostics.exe', 'bin/pse-diagnostics.exe')
    frozen = json.loads((ROOT / 'docs/evidence/m1/PSE_TOGETHER_P24/ARTIFACT_MANIFEST.json').read_text('utf8'))
    for rec in frozen['artifacts']:
        if rec['path'].endswith('.pdb'):
            continue
        relative = rec['path'].split('/artifacts/', 1)[1]
        copy(ROOT / rec['path'], 'native-reference/' + relative, rec['sha256'])
    copy(ROOT / 'tools/enet-prototype/third_party/enet/LICENSE', 'licenses/ENet.txt')
    copy(ROOT / 'experiments/pse_together/third_party/minhook/LICENSE.txt', 'licenses/MinHook.txt')
    for action in ['verify', 'host', 'client', 'collect']:
        (kit / (action + '.bat')).write_bytes((
            '@echo off\r\n'
            '"%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0friends.ps1" ' + action + '\r\n'
            'set "pse_result=%ERRORLEVEL%"\r\n'
            'if not "%pse_result%"=="0" echo Failed with code %pse_result%\r\n'
            'pause\r\nexit /b %pse_result%\r\n').encode('ascii'))
    write(kit / 'provenance.json', dict(package_id='PSE-P25-DIAGNOSTIC-1', exe_sha256=EXE_SHA,
          scope='Standalone ENet friends diagnostics plus frozen native developer reference; no portable KEX launch.',
          files=provenance))
    files = [dict(path=p.relative_to(kit).as_posix(), size=p.stat().st_size, sha256=sha(p))
             for p in sorted(kit.rglob('*')) if p.is_file() and p.name != 'friends.json']
    write(kit / 'manifest.json', dict(package_id='PSE-P25-DIAGNOSTIC-1', exe_sha256=EXE_SHA,
                                    editable=['friends.json'], files=files))
    archive(kit, out / 'PSE-P25-DIAGNOSTIC-1.zip')
    symbols = out / 'symbols'
    symbols.mkdir()
    symbol_rows = []
    for rec in frozen['artifacts']:
        if rec['path'].endswith('.pdb'):
            source = ROOT / rec['path']
            if sha(source) != rec['sha256']:
                raise ValueError('Symbol hash mismatch')
            shutil.copyfile(source, symbols / source.name)
    for source in [transport / 'Release/pse_host.pdb', transport / 'Release/pse_client.pdb',
                   diagnostics / 'Release/pse-diagnostics.pdb']:
        shutil.copyfile(source, symbols / source.name)
    for role, name in [('HOST', 'pse_m11_bootstrap.pdb'), ('CLIENT', 'pse_native_client58b.pdb')]:
        source = ROOT / f'docs/evidence/m1/PSE_TOGETHER_P1/P1_{role}_03/native_artifact' / name
        shutil.copyfile(source, symbols / name)
    for p in sorted(symbols.glob('*')):
        symbol_rows.append(dict(path=p.name, size=p.stat().st_size, sha256=sha(p)))
    write(symbols / 'manifest.json', dict(files=symbol_rows))
    archive(symbols, out / 'symbols.zip')
    result = dict(status='BUILT', scope='Packaging, not gameplay acceptance', files=len(files),
                  artifacts=[dict(path=p.name, size=p.stat().st_size, sha256=sha(p))
                             for p in [out / 'PSE-P25-DIAGNOSTIC-1.zip', out / 'symbols.zip']])
    write(out / 'BUILD.json', result)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--transport', default='b85t')
    ap.add_argument('--diagnostics', default='b85p')
    args = ap.parse_args()
    build(ROOT / args.out, ROOT / args.transport, ROOT / args.diagnostics)
