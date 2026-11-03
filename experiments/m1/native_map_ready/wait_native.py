"""Read-only bounded journal predicate; fails immediately on recorded native fault."""
from pathlib import Path
import json,sys,time
root=Path(__file__).resolve().parents[3]
name,stage=sys.argv[1:3];timeout=float(sys.argv[3]) if len(sys.argv)>3 else 20;assert name.startswith('N56_')
path=root/'docs/evidence/m1'/name/'native.jsonl';end=time.monotonic()+timeout
predicates={
    'startup':lambda r:r['event']=='tick_status',
    'gameplay':lambda r:r['event']=='tick_status' and r['state']==3 and r['local_updates']>0 and r['local_this_tick']==1,
    'map_bound':lambda r:r['event']=='map_identity_bound',
    'listener':lambda r:r['event']=='network_started',
    'client_welcome':lambda r:r['event']=='client_welcome',
    'client_ready':lambda r:r['event']=='client_map_ready_queued',
    'host_ready':lambda r:r['event']=='network_ready',
    'snapshots':lambda r:r['event']=='native_snapshot_received',
}
while time.monotonic()<end:
    rows=[]
    if path.exists():
        for line in path.read_text(encoding='utf8').splitlines():
            try:rows.append(json.loads(line))
            except ValueError:pass
    bad=[r for r in rows if r['event'] in ['blocked','native_fault','hook_error','hook_create_error']]
    if bad and stage!='blocked':print(json.dumps(bad,ensure_ascii=False));raise SystemExit(2)
    selected=[r for r in rows if r['event']=='blocked'] if stage=='blocked' else [r for r in rows if predicates[stage](r)]
    if stage=='snapshots' and {r['slot'] for r in selected}!={0,1}:selected=[]
    if selected:print(json.dumps(selected[:2],ensure_ascii=False));break
    time.sleep(.1)
else:raise TimeoutError('Native predicate missing: '+name+'/'+stage)
