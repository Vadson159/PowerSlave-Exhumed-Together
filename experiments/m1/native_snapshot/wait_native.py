"""Read-only bounded journal predicate; fails immediately on recorded native fault."""
from pathlib import Path
import json,sys,time
root=Path(__file__).resolve().parents[3]
name,stage=sys.argv[1:3];assert name.startswith('N55_')
path=root/'docs/evidence/m1'/name/'native.jsonl';end=time.monotonic()+20
while time.monotonic()<end:
    rows=[]
    if path.exists():
        for line in path.read_text(encoding='utf8').splitlines():
            try:rows.append(json.loads(line))
            except ValueError:pass
    bad=[r for r in rows if r['event'] in ['blocked','native_fault','hook_error','hook_create_error']]
    assert not bad,bad
    if stage=='startup':selected=[r for r in rows if r['event']=='tick_status']
    elif stage=='gameplay':selected=[r for r in rows if r['event']=='tick_status' and r['state']==3 and r['local_updates']>0 and r['local_this_tick']==1]
    elif stage=='listener':selected=[r for r in rows if r['event']=='network_started']
    elif stage=='snapshots':
        selected=[r for r in rows if r['event']=='native_snapshot_received']
        if {r['slot'] for r in selected}!={0,1}:selected=[]
    else:raise ValueError(stage)
    if selected:print(json.dumps(selected[:2],ensure_ascii=False));break
    time.sleep(.1)
else:raise TimeoutError('Native predicate missing: '+name+'/'+stage)
