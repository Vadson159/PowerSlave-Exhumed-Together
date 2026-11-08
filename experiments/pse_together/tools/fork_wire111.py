"""Create the separately compiled P4 transport fork once; never edits frozen v1.10.

The generated sources are retained under src/wire111 and reviewed as normal code.
This script only records/copies the baseline; it must not be rerun over the fork.
"""
from pathlib import Path
import hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[3]
src=ROOT/'tools/enet-prototype'
dst=ROOT/'experiments/pse_together/src/wire111'
assert not dst.exists()
records=[]
for folder,pattern in [('include','*.h'),('src','*.cpp')]:
    for p in sorted((src/folder).glob(pattern)):
        if folder=='src' and p.stem.endswith('_main'):continue
        q=dst/p.relative_to(src);q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q)
        records.append(dict(source=p.relative_to(ROOT).as_posix(),fork=q.relative_to(ROOT).as_posix(),baseline_sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
(dst/'BASELINE.json').write_text(json.dumps(records,indent=2),encoding='utf8')
print('Copied',len(records),'source files; no v1.10 edits')
