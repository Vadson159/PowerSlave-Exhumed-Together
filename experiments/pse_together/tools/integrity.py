"""S0.3 integrity gate: baseline | check <TAG>. Everything stays inside the mod root; read-only on the game.
Usage: python -X utf8 -B integrity.py baseline|check <TAG>
Baseline = SHA-256 of every historical file (docs, experiments, tools) outside the S0.3 additions, plus the
accepted game_check() (original install, EXE copies, protected saves) from DOC-52 audit.py."""
from pathlib import Path
import datetime, hashlib, importlib.util, json, sys
ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'docs/evidence/m1/PSE_TOGETHER_S03'
NEW = ROOT / 'experiments/pse_together'
OWN_DOC = 'docs/60_S0_3_PSE_TOGETHER_UNIFIED_BASE.md'
ALLOWED = {'docs/PROGRESS.md', 'docs/EVIDENCE_INDEX.md', 'docs/ROADMAP.md', 'docs/CURRENT_STATE.md', '.gitignore'}
SKIP = {'game', 'profile', '__pycache__', '.git', 'build', 'scratch', 'build_gemini'}
EXPECTED = 'b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
HOST_EXE = ROOT / 'experiments/m0/game/anubis_Shipping_Steam_x64.exe'
CLIENT_EXE = ROOT / 'experiments/m1/native_second_instance/game/anubis_Shipping_Steam_x64.exe'

def sha(p):
    h = hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''): h.update(b)
    return h.hexdigest()
def stamp(): return datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
def games():
    s = importlib.util.spec_from_file_location('audit52', ROOT / 'experiments/m1/terminal_shutdown/audit.py')
    m = importlib.util.module_from_spec(s); s.loader.exec_module(m); return m.game_check()
def paths():
    out = []
    for part in ['docs', 'experiments', 'tools']:
        for p in (ROOT / part).rglob('*'):
            if not p.is_file() or p.is_relative_to(OUT) or p.is_relative_to(NEW): continue
            rel = p.relative_to(ROOT)
            if SKIP.intersection(rel.parts) or rel.as_posix() == OWN_DOC: continue
            out.append(p)
    return sorted(out)
def dump(p, d, overwrite=False):
    with Path(p).open('w' if overwrite else 'x', encoding='utf8') as f: json.dump(d, f, indent=2, ensure_ascii=False)
def baseline():
    OUT.mkdir(parents=True, exist_ok=True)
    assert not (OUT / 'BASELINE.json').exists()
    g = games(); assert not g['errors'] and not g['client_copy_errors'] and g['original_full_snapshot_equal'], g
    ps = paths()
    dump(OUT / 'BASELINE.json', dict(utc=stamp(), files=[dict(path=p.relative_to(ROOT).as_posix(), sha256=sha(p)) for p in ps], game=g,
        host_exe_sha256=sha(HOST_EXE), client_exe_sha256=sha(CLIENT_EXE), exe_expected=EXPECTED))
    for n in ['PROGRESS.md', 'EVIDENCE_INDEX.md', 'ROADMAP.md', 'CURRENT_STATE.md']: (OUT / (n + '.before')).write_bytes((ROOT / 'docs' / n).read_bytes())
    print('BASELINE PASS', len(ps), 'files; exe', sha(HOST_EXE) == EXPECTED and sha(CLIENT_EXE) == EXPECTED)
def check(tag):
    b = json.loads((OUT / 'BASELINE.json').read_text(encoding='utf8'))
    changed = [r['path'] for r in b['files'] if not (ROOT / r['path']).is_file() or sha(ROOT / r['path']) != r['sha256']]
    unexpected = sorted(set(changed) - ALLOWED)
    known = {r['path'] for r in b['files']}
    added = [p.relative_to(ROOT).as_posix() for p in paths() if p.relative_to(ROOT).as_posix() not in known]
    g = games()
    ok = not unexpected and not added and g == b['game'] and sha(HOST_EXE) == EXPECTED and sha(CLIENT_EXE) == EXPECTED
    r = dict(status='PASS' if ok else 'FAIL', tag=tag, changed=changed, allowed_changes=sorted(ALLOWED), unexpected=unexpected, added_outside_s03=added,
        game_equals_baseline=g == b['game'], files=len(b['files']), host_exe_sha256=sha(HOST_EXE), client_exe_sha256=sha(CLIENT_EXE), utc=stamp())
    dump(OUT / (tag + '.json'), r, overwrite=True); print('INTEGRITY', tag, r['status'], 'changed', changed, 'added', added); return ok
if __name__ == '__main__':
    if sys.argv[1] == 'baseline': baseline()
    elif sys.argv[1] == 'check': sys.exit(0 if check(sys.argv[2]) else 1)
