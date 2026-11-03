"""Read-only source/evidence viewer. All scratch configuration comes from S workflow."""
from workflow import ROOT
import argparse,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('path');p.add_argument('--key');p.add_argument('--start',type=int,default=1);p.add_argument('--end',type=int,default=10000000);p.add_argument('--tail',type=int)
a=p.parse_args();f=Path(a.path);f=f if f.is_absolute() else ROOT/f
d=f.read_text(encoding='utf-8-sig')
if a.key:
    d=json.loads(d)
    for k in a.key.split('.'):d=d[int(k)] if isinstance(d,list) else d[k]
if isinstance(d,str):
    lines=d.splitlines();start=max(1,len(lines)-a.tail+1) if a.tail else a.start
    for i,line in enumerate(lines,1):
        if start<=i<=a.end:print(f'{i}: {line}')
else:print(json.dumps(d,ensure_ascii=False,indent=2))
