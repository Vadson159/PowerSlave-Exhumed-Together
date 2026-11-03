"""Read-only checks for final DOC57 links and declared file hashes; not native QA."""
from pathlib import Path
import json,re
from workflow import ROOT,OUT,sha,load

manifest=load(OUT/'FILES_FINAL.json')
bad=[r['path'] for r in manifest['files'] if not (ROOT/r['path']).is_file() or sha(ROOT/r['path'])!=r['sha256']]
missing=[]
for name in ['57_NATIVE_VISUAL_PROXY_STEP0.md','PROGRESS.md','EVIDENCE_INDEX.md']:
    p=ROOT/'docs'/name;text=p.read_text(encoding='utf-8-sig')
    if name!='57_NATIVE_VISUAL_PROXY_STEP0.md':text=text.split('## DOC-56',1)[0]
    for target in re.findall(r'\]\(([^)]+)\)',text):
        if '://' in target or target.startswith('#'):continue
        if not (p.parent/target.split('#',1)[0]).exists():missing.append(dict(document=name,target=target))
summary=load(OUT/'RESULT_SUMMARY.json');integrity=load(OUT/'INTEGRITY_FINAL.json')
assert summary['verdict']=='BLOCKED' and not summary['doc58_ready']
assert summary['verified_actor_tick_samples']==0 and summary['actual_actor_lifetime_ticks'] is None
assert summary['remove_calls_requested']==0 and not summary['visual_pass']
assert not bad and not missing and not integrity['unexpected'] and integrity['game_equals_baseline'],(bad,missing)
print(json.dumps(dict(status='PASS',scope='Artifact/link/integrity consistency only; native verdict remains BLOCKED',manifest_files=len(manifest['files']),bad_hashes=bad,missing_links=missing,historical_files=integrity['baseline_files'],changed_existing=integrity['changed']),ensure_ascii=False))
