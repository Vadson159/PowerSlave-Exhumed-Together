"""DOC52 file-only audit. Never launches a game or modifies historical evidence."""
from pathlib import Path
import datetime, hashlib, importlib.util, json, sys

LAB = Path(__file__).resolve().parent
ROOT = LAB.parents[2]
OUT = ROOT / 'docs/evidence/m1/HOST_TERMINAL_52'
E = ROOT / 'docs/evidence/m1'

def sha(p):
    h = hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda: f.read(1024*1024), b''): h.update(b)
    return h.hexdigest()

def load(p): return json.loads(p.read_text(encoding='utf-8-sig'))
def dump(name, o):
    OUT.mkdir(parents=True, exist_ok=True)
    with (OUT/name).open('x', encoding='utf8') as f: json.dump(o, f, indent=2, ensure_ascii=False)

def normalized(p):
    o = load(p)
    return o.get('structuredContent', {}).get('normalized_result', o)

def game_check():
    spec = importlib.util.spec_from_file_location('window_close52', ROOT/'experiments/m1/window_close/workflow.py')
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    g = m.game_check()
    prep = load(ROOT/'docs/evidence/m0/M0_001_PREPARATION.json')
    client = ROOT/'experiments/m1/native_second_instance/game'
    g['client_copy_errors'] = [r['relative_path'] for r in prep['independent_copy_files'] if sha(client/r['relative_path']) != r['sha256']]
    return g

def baseline():
    assert not (OUT/'BASELINE.json').exists()
    paths = set()
    for folder in ['docs/evidence', 'experiments/m1', 'tools/enet-prototype']:
        for p in (ROOT/folder).rglob('*'):
            if p.is_file() and not p.is_relative_to(OUT) and not p.is_relative_to(LAB) and not {'__pycache__','game','profile'}.intersection(p.relative_to(ROOT/folder).parts): paths.add(p)
    for p in (ROOT/'docs').glob('*.md'): paths.add(p)
    paths.discard(ROOT/'docs/PROGRESS.md'); paths.discard(ROOT/'docs/EVIDENCE_INDEX.md')
    records = [dict(path=p.relative_to(ROOT).as_posix(), sha256=sha(p)) for p in sorted(paths)]
    g = game_check()
    assert not g['errors'] and not g['client_copy_errors'] and g['original_full_snapshot_equal']
    dump('BASELINE.json',dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), files=records, game=g))
    for n in ['PROGRESS.md','EVIDENCE_INDEX.md']:
        (OUT/(n+'.before52')).write_bytes((ROOT/'docs'/n).read_bytes())
    print('Baseline:',len(records),'historical/source/build files; game integrity PASS')

def inspect():
    native = [json.loads(s) for s in (E/'N51_HOST_01/native.jsonl').read_text().splitlines()]
    status = normalized(E/'N51_HOST_01/STATUS_EXIT.json')
    payloads = [m['payload'] for m in status['messages'] if isinstance(m.get('payload'), dict)]
    selected = [r for r in native if r['seq'] >= 21026]
    os_selected = [p for p in payloads if p.get('kind','').startswith('n51_')]
    dump('DOC51_TERMINAL_EXTRACT.json',dict(native=selected, os=os_selected, status={k:v for k,v in status.items() if k!='messages'}, sources=['N51_HOST_01/native.jsonl','N51_HOST_01/STATUS_EXIT.json']))
    for r in selected: print(r['utc'],r['tid'],r['seq'],r['event'])
    for r in os_selected: print(json.dumps(r))
    a,b = [load(E/n/'BUILD_MANIFEST.json') for n in ['M12B_WINDOW_CLOSE_48','NATIVE_SECOND_INSTANCE_51']]
    def entries(o): return {r['path'].replace('\\','/'):r['sha256'] for r in o['sources']}
    aa,bb = entries(a),entries(b)
    common = sorted(set(aa)&set(bb))
    differences = [dict(path=p, doc48=aa[p], doc51=bb[p]) for p in common if aa[p]!=bb[p]]
    equal = [dict(path=p,sha256=aa[p]) for p in common if aa[p]==bb[p]]
    current_bad = [p for p in bb if (ROOT/p).is_file() and sha(ROOT/p)!=bb[p]]
    dump('SOURCE_DIFFERENTIAL.json',dict(changed=differences,equal=equal,doc51_current_drift=current_bad, artifacts48=a['artifacts'],artifacts51=b['artifacts'],only48=sorted(set(aa)-set(bb)),only51=sorted(set(bb)-set(aa))))
    print('Compiled-source differences:',json.dumps([r for r in differences if Path(r['path']).suffix in ['.cpp','.h','.hpp','.c','.txt']]))
    print('DOC51 current drift',current_bad)

def integrity():
    b=load(OUT/'BASELINE.json')
    changed=[r['path'] for r in b['files'] if not (ROOT/r['path']).is_file() or sha(ROOT/r['path'])!=r['sha256']]
    g=game_check()
    ok=not changed and not g['errors'] and not g['client_copy_errors'] and g['original_full_snapshot_equal'] and g['protected']==b['game']['protected']
    dump('INTEGRITY_'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S')+'.json',dict(status='PASS' if ok else 'FAIL',historical_files=len(b['files']),changed=changed,game=g,protected_equal=g['protected']==b['game']['protected']))
    print('Integrity',ok,'files',len(b['files']),'changed',changed)
    assert ok

if __name__=='__main__': {'baseline':baseline,'inspect':inspect,'integrity':integrity}[sys.argv[1]]()
