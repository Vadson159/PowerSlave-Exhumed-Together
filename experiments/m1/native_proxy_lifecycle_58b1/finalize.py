from workflow import *
import re
def integrity(name):
    b=load(OUT/'BASELINE.json');allowed={'docs/PROGRESS.md','docs/EVIDENCE_INDEX.md'}
    changed=[r['path'] for r in b['files'] if not (ROOT/r['path']).is_file() or sha(ROOT/r['path'])!=r['sha256']]
    ext=[dict(path=x['path'],equal=snapshot(x['path'])==x) for x in b['external']]
    d=dict(status='PASS' if not (set(changed)-allowed) and all(x['equal'] for x in ext) else 'FAIL',historical_files=len(b['files']),changed=changed,unexpected=sorted(set(changed)-allowed),external=ext,allowed=sorted(allowed),scope='All protected baseline files excluding build/cache/profile/game; complete original77 and protected save roots separately hashed',utc=stamp())
    dump(OUT/name,d);print(json.dumps(d,ensure_ascii=False));assert d['status']=='PASS'
def manifest():
    files=[p for p in sorted(LAB.rglob('*')) if p.is_file() and 'scratch' not in p.parts and 'profile' not in p.parts]
    dump(OUT/'SOURCE_MANIFEST.json',dict(new_files=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p),size=p.stat().st_size) for p in files],accepted_sources_changed=[],new_gameplay_DLLs=[],wire_changes=False,transport_changes=False,build='b581',note='launch_bound.py is copied accepted launcher with proc.terminate removed. restore_observation.js retains the inline REA source; UTF8 final newline differs from inline source and is declared.'))
    doc=ROOT/'docs/58B1_NATIVE_LEVEL_UNLOAD_RECONNECT.md'
    with doc.open('a',encoding='utf8') as f:
        f.write('\n## Фактические новые исходники и SHA-256\n\nПринятые исходники DOC-58B и production не изменялись; новая gameplay DLL не создавалась.\n\n| Файл в лаборатории | SHA-256 |\n|---|---|\n')
        for p in files:f.write('| `'+p.relative_to(LAB).as_posix()+'` | `'+sha(p)+'` |\n')
        f.write('\nМанифест: [SOURCE_MANIFEST.json](evidence/m1/NATIVE_PROXY_LIFECYCLE_58B1/SOURCE_MANIFEST.json). Build outputs и command logs находятся в `b581/` и новом Evidence.\n')
if __name__=='__main__':
    if sys.argv[1]=='manifest':manifest()
    else:integrity(sys.argv[1])
