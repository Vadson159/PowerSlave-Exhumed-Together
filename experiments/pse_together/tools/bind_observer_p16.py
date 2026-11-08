"""Bind a passive Scene/arena observer to the actual isolated native run."""
import argparse,json
from runtime_p16 import r
def main():
    p=argparse.ArgumentParser();p.add_argument('role',choices=['host','client']);a=p.parse_args()
    run=r.OUT/r.RUNS[a.role];launch=r.load(run/'launch.json');artifact=r.load(run/'NATIVE_ARTIFACT.json')['config'];iso=r.load(run/'ISOLATION_GATE.json')
    if iso['status']!='PASS' or iso['pid']!=launch['pid'] or r.sha(artifact['dll'])!=artifact['dll_sha256']:raise ValueError('isolated frozen run required')
    secondary='0x0'
    if a.role=='host':
        raw=(run/'native.jsonl').read_bytes();raw=raw[:raw.rfind(b'\n')+1]
        rows=[json.loads(line) for line in raw.splitlines()]
        spawned=next((x for x in rows if x['event']=='independent_spawn'),None)
        if spawned:secondary=hex(int(spawned['p2_player'],16))
    cfg=dict(pid=launch['pid'],tid=launch['primary_thread_id'],role=a.role,target=launch['target'],secondary=secondary,scene=r.load(r.SITES_PATH)['scene'])
    path=run/('observer_p16_'+r.stamp()+'.js')
    path.write_text('const cfg='+json.dumps(cfg)+';\n'+(r.TOOLS/'observer_lifetime_p16.js').read_text(encoding='utf8'),encoding='utf8')
    print(path)
if __name__=='__main__':main()
