"""Bind cleanup of retained known return identity; never scans memory or spawns."""
from workflow import *
from runner import config,RUN
run=OUT/RUN
rows=[json.loads(s) for s in (run/'native.jsonl').read_text(encoding='utf8').splitlines()]
pre=next(r for r in rows if r['event']=='native_preflight')
spawn=next(r for r in rows if r['event']=='spawn_identity_verified')
returned=next(r for r in rows if r['event']=='factory_return')
assert returned['returned']==spawn['actor']['actor'] and returned['pointer_saved']
assert sum(r['event']=='factory_return' for r in rows)==1
assert not any(r['event']=='remove_request' for r in rows)
bundle=load(OUT/'REA_C_GUARD_STOP.json')
w=next(r for r in bundle['records'] if r['evidence_id']=='ev_505aaf9a7f0513d30d41675a3ec8c44bba2e45c19752ce4afc9f3998e5cbbabd')
p=w['normalized_result']['messages'][0]['payload'];assert p['actor']==returned['returned'] and p['node']==spawn['actor']['node']
assert p['p1']['sector']==123 and p['tail_valid'] and len(p['sector259'])==1 and p['refs']==0 and p['in_use']==1 and p['removed']==0 and p['flags']==64
cfg=config(RUN);cfg.update(log=str(run/'cleanup.jsonl'),existing_actor=p['actor'],existing_node=p['node'],existing_frame=spawn['actor']['frame'],original_graph={k:pre['primary'][k] for k in ['player','node','actor','own_node','owner','vtable']},original_inventory={k:pre['primary'][k] for k in ['health','owned_weapons','ammo','weapon','pending_weapon']})
body='const cfg='+json.dumps(cfg)+';\n'+'\n'.join((LAB/n).read_text(encoding='utf8') for n in ['common.js','cleanup_only.js'])
bound=run/'cleanup_bound.js'
with bound.open('x',encoding='utf8') as f:f.write(body)
command(['C:/Program Files/nodejs/node.exe','--check',bound],'CLEANUP_SYNTAX')
dump(run/'CLEANUP_PLAN.json',dict(status='READY',source=cfg,known_identity_witness=w,main_stop_reason='Incorrect constant sector-list count after ordinary P1 sector259 ->123. Direct actor/node/list remain valid. Experiment stopped; cleanup only.',sources=[dict(path=(LAB/n).relative_to(ROOT).as_posix(),sha256=sha(LAB/n)) for n in ['common.js','cleanup_only.js']],bound_sha256=sha(bound),factory_calls=0,main_probe_must_unload_before_loading=True))
print('CLEANUP ONLY bound; known identity, no search, no Spawn')
