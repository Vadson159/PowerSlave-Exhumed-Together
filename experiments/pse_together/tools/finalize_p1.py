"""Read-only final source/links audit; writes only new P1 evidence, never launches the game."""
import datetime
import hashlib
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[3]
LAB=ROOT/'experiments/pse_together'
OUT=ROOT/'docs/evidence/m1/PSE_TOGETHER_P1'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def load(p):return json.loads(p.read_text(encoding='utf8'))
def record(p):return dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p),bytes=p.stat().st_size)
docs=[ROOT/'docs'/n for n in ['61_P1_NATIVE_PROXY_DLL.md','60_S0_3_PSE_TOGETHER_UNIFIED_BASE.md',
    'CURRENT_STATE.md','ROADMAP.md','PROGRESS.md','EVIDENCE_INDEX.md']]
files=[p for p in LAB.rglob('*') if p.is_file() and not {'runtime','__pycache__'}.intersection(p.relative_to(LAB).parts)]
files+=docs
freeze=load(OUT/'PRE_NATIVE_GATE_RUN03.json')
compiled=[r for r in freeze['sources'] if Path(r['path']).suffix in {'.c','.cpp','.h','.hpp'} or Path(r['path']).name=='CMakeLists.txt']
mismatch=[r['path'] for r in compiled if sha(ROOT/r['path'])!=r['sha256']]
links=[]
for p in [docs[0],docs[2],docs[3],LAB/'README.md',LAB/'tools/RUNTIME_P1.md',docs[4],docs[5]]:
    content=p.read_text(encoding='utf8')
    if p.name=='PROGRESS.md':content=content.split('\n## S0.5 / П2',1)[0]
    if p.name=='EVIDENCE_INDEX.md':content=content.split('\n## ROADMAP:',1)[0]
    if p.name=='ROADMAP.md':content='\n'.join(line for line in content.splitlines() if 'DOC-61' in line or '61_P1' in line)
    for target in re.findall(r'\[[^\]]+\]\(([^)]+)\)',content):
        if target.startswith(('http:','https:','#')):continue
        target=target.split('#',1)[0].strip('<>')
        links.append(dict(source=p.relative_to(ROOT).as_posix(),target=target,exists=(p.parent/target).exists()))
missing=[r for r in links if not r['exists']]
runtime=load(OUT/'P1_RUNTIME_ACCEPTANCE_CANDIDATE.json')
integrity=load(OUT/'P1_SCOPED_INTEGRITY.json')
sessions=load(OUT/'RUN03_SESSIONS_CLOSED.json')
roadmap=(ROOT/'docs/ROADMAP.md').read_text(encoding='utf8')
status_ok=all('REVIEW' in next(line for line in roadmap.splitlines() if line.startswith(prefix)) for prefix in ['| П1 |','| S0.4 |'])
status_ok&='PASS' in next(line for line in roadmap.splitlines() if line.startswith('| S0.3 |'))
manifest=dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),package='П1',package_status='REVIEW',
    sources=[record(p) for p in sorted(files)],compiled_source_count=len(compiled),compiled_source_mismatches=mismatch,
    frozen_dlls=freeze['frozen'],runtime_candidate='P1_RUNTIME_ACCEPTANCE_CANDIDATE.json',
    runtime_status=runtime['status'],runtime_checks=runtime['check_counts'],integrity_status=integrity['status'],
    session_count=sessions['closed'],sessions_closed=sessions['closed']==sessions['attempted'] and not sessions['errors'])
for filename in ['FINAL_SOURCE_MANIFEST.json','DELIVERY_CHECKS.json']:
    active=OUT/filename
    if active.exists():
        with (OUT/(active.stem+'_BEFORE_QA_ORDER_FIX.json')).open('xb') as archive:archive.write(active.read_bytes())
with (OUT/'FINAL_SOURCE_MANIFEST.json').open('w',encoding='utf8') as f:json.dump(manifest,f,indent=2,ensure_ascii=False)
# The manifest is itself a deliverable link; validate it after publishing it.
for item in links:item['exists']=(ROOT/item['source']).parent.joinpath(item['target']).exists()
missing=[r for r in links if not r['exists']]
ok=not mismatch and not missing and status_ok and runtime['check_counts']=={'PASS':34} and integrity['status']=='PASS' and manifest['sessions_closed']
result=dict(status='PASS' if ok else 'FAIL',scope='P1 delivery files/statuses/source freeze and already completed evidence; no new runtime',
    sources=len(manifest['sources']),compiled_sources=len(compiled),source_mismatches=mismatch,
    local_links=len(links),missing_links=missing,roadmap_statuses_correct=status_ok,
    source_manifest_sha256=sha(OUT/'FINAL_SOURCE_MANIFEST.json'))
with (OUT/'DELIVERY_CHECKS.json').open('w',encoding='utf8') as f:json.dump(result,f,indent=2,ensure_ascii=False)
print(json.dumps(result,ensure_ascii=False))
raise SystemExit(0 if ok else 1)
