"""DOC53 file-only integrity baseline; never starts a game."""
from pathlib import Path
import datetime, hashlib, importlib.util, json, shutil, sys

LAB = Path(__file__).resolve().parent
ROOT = LAB.parents[2]
OUT = ROOT / 'docs/evidence/m1/HOST_BACKGROUND_53'

def sha(p):
    h = hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda: f.read(1024 * 1024), b''): h.update(b)
    return h.hexdigest()

def dump(p, o):
    p.parent.mkdir(parents=True, exist_ok=True)
    with p.open('x', encoding='utf8') as f: json.dump(o, f, indent=2, ensure_ascii=False)

def game_check():
    spec = importlib.util.spec_from_file_location('integrity52', LAB.parent/'terminal_shutdown/audit.py')
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    return m.game_check()

def stamp(): return datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')

def baseline():
    assert not (OUT/'BASELINE.json').exists()
    files = set()
    for d in ['docs/evidence', 'experiments/m0', 'experiments/m1', 'tools/enet-prototype', 'tools/background-host-simulation-research']:
        for p in (ROOT/d).rglob('*'):
            if p.is_file() and not p.is_relative_to(OUT) and not p.is_relative_to(LAB) and not {'game', 'profile', '__pycache__'}.intersection(p.relative_to(ROOT/d).parts): files.add(p)
    files.update((ROOT/'docs').glob('*.md'))
    for n in ['PROGRESS.md', 'EVIDENCE_INDEX.md']: files.discard(ROOT/'docs'/n)
    g = game_check()
    assert not g['errors'] and not g['client_copy_errors'] and g['original_full_snapshot_equal']
    dump(OUT/'BASELINE.json', dict(utc=stamp(), files=[dict(path=p.relative_to(ROOT).as_posix(), sha256=sha(p)) for p in sorted(files)], game=g))
    for n in ['PROGRESS.md', 'EVIDENCE_INDEX.md']: shutil.copy2(ROOT/'docs'/n, OUT/(n+'.before53'))
    for p in [ROOT/'experiments/m1/native_bootstrap/src/bootstrap.cpp', ROOT/'experiments/m1/native_bootstrap/src/build_profile.hpp', ROOT/'experiments/m1/network_input/src/network_input.hpp']:
        dst=OUT/'before'/p.relative_to(ROOT); dst.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(p, dst)
    print('Baseline PASS:', len(files), 'files; exact game/copies/protected snapshots')

def integrity():
    b=json.loads((OUT/'BASELINE.json').read_text(encoding='utf8'))
    changed=[r['path'] for r in b['files'] if not (ROOT/r['path']).is_file() or sha(ROOT/r['path'])!=r['sha256']]
    allowed={'experiments/m1/native_bootstrap/src/bootstrap.cpp', 'experiments/m1/network_input/src/network_input.hpp'}
    unexpected=sorted(set(changed)-allowed)
    g=game_check()
    ok=not unexpected and not g['errors'] and not g['client_copy_errors'] and g['original_full_snapshot_equal'] and g['protected']==b['game']['protected']
    dump(OUT/('INTEGRITY_'+stamp()+'.json'),dict(status='PASS' if ok else 'FAIL', files_checked=len(b['files']),changed=changed,unexpected=unexpected,protected_equal=g['protected']==b['game']['protected'],game=g))
    print('Integrity:',ok,'changed',changed,'unexpected',unexpected); assert ok

if __name__=='__main__': {'baseline':baseline,'integrity':integrity}[sys.argv[1]]()
