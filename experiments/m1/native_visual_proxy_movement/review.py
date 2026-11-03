"""Review recorded current-run evidence; never opens or mutates a KEX process."""
from workflow import *
from itertools import combinations
run=OUT/'N58A_01'
rows=[json.loads(x) for x in (run/'native_v2.jsonl').read_text('utf8').splitlines()]
def events(name):return [x for x in rows if x['event']==name]
checks=[]
def check(name,value):
    checks.append(dict(name=name,passed=bool(value)))
    assert value,name
snaps=[x['snapshot'] for x in events('p1_state_snapshot')]
check('nine complete snapshots',len(snaps)==9 and all(x['status']=='OK' and len(x['fields'])==12 for x in snaps))
comparisons=[]
for a,b in combinations(snaps,2):
    fields={k:dict(equal=a['fields'][k]['raw']==b['fields'][k]['raw'],before=a['fields'][k],after=b['fields'][k]) for k in a['fields']}
    check('P1 '+a['label']+' -> '+b['label'],a['identity']==b['identity'] and all(v['equal'] for v in fields.values()))
    comparisons.append(dict(before=a['label'],after=b['label'],fields=fields))
for s in snaps:dump(OUT/('P1_STATE_'+s['label']+'.json'),s)
dump(OUT/'PER_FIELD_COMPARISON.json',comparisons)
check('single native spawn',len(events('native_factory_call'))==len(events('factory_return'))==len(events('spawn_identity_verified'))==1)
check('no native experiment faults',not events('experiment_fault'))
moves=events('movement_applied');pos=[x for x in moves if x['operation']=='position'];yaw=[x for x in moves if x['operation']=='yaw'];links=[x for x in moves if x['operation']=='sector']
check('four unique transforms',len(pos)==4 and len({tuple(x['value']) for x in pos})==4)
check('four yaw updates',len(yaw)==4 and [x['value'] for x in yaw]==[.25,.5,.75,1])
check('four native same sector LinkSector',len(links)==4 and all(x['value']['sector']==259 for x in links))
for m in moves:
    a=m['after']['actor'];f=a['actor'];s=m['after']['interpolants']
    check('movement invariants '+str(m['revision']),a['sector_list']['valid'] and a['sector_list']['found']==1 and m['before']['sector_list']['valid'] and m['after']['p1_exact_unchanged'] and f['sector']==259 and f['flags']==64 and f['target']=='0x0' and f['source_owner']=='0x0' and f['bounds_min']==[-12,-12,-24] and f['bounds_max']==[12,12,24])
    if m['operation']=='position' and m['mode']=='teleport':check('teleport reset '+str(m['revision']),s['previous']==s['output']==m['value'])
sprites=events('sprite_submission')
for p,y in zip(pos,yaw):
    check('genuine sprite transform '+str(p['revision']),any(s['revision']==p['revision']+2 and s['origin']==p['value'] and s['angles'][2]==y['value'] for s in sprites))
lerp=events('native_interpolation_sample');check('three natural interpolant outputs',len(lerp)==3 and all(0<x['t']<1 and x['formula_error']<=.001 for x in lerp) and lerp[0]['t']<lerp[1]['t']<lerp[2]['t'])
check('P1 dynamic sector traversal with live proxy',bool(events('sector_transition_verified')))
stable=events('movement_stability')[0];check('post movement >=120 steps',stable['steps_since_last_movement']>=120)
ret=events('deferred_retired')[0]
check('exactly one Remove and retirement',len(events('native_remove_postcondition'))==len(events('native_pool_return_entry'))==1 and ret['spawn_calls']==ret['remove_calls']==1 and ret['payload_null'] and ret['refs']==0 and ret['in_use']==0 and ret['sector_occurrences']==0)
check('steps and actor callbacks',ret['simulation_ticks']==ret['actor_tick_callbacks']==5235)
check('P1 post retirement movement',bool(events('post_retirement_movement')))
check('no stale sprite after remove',not events('sprite_after_remove'))
osproof=load(run/'OS_SIGNALED.json');check('clean retained handle exit0',osproof['wait']==osproof['duplicate_wait']==osproof['exit_code']==0 and osproof['no_emergency_cleanup'] and load(run/'exit.json')['exit_code']==0)
check('protected profiles equal',load(run/'protected_before.json')==load(run/'protected_after.json'))
bundle=load(OUT/'REA_FINAL.json');messages=[m for r in bundle['records'] if r['operation'] in ['frida_session_status','load_frida_script'] for m in r.get('normalized_result',{}).get('messages',[])]
payloads=[m.get('payload',{}) for m in messages]
camera=next(p for p in payloads if p.get('kind')=='post_retirement_native_camera');check('local native camera',camera['camera_matches'])
restore=next(p for p in payloads if p.get('kind')=='58A_hooks_restored');check('all 20 site prefixes restored',restore['passed'] and len(restore['results'])==20)
guards=[p for p in payloads if p.get('kind')=='isolation_write_guard_summary'];guard=max(guards,key=lambda p:p.get('write_opens',0));check('isolation guard no errors or overflow',guard.get('allowed_disk',0)>0 and guard.get('errors',0)==guard.get('unique_overflow',0)==0)
check('no truncated MCP messages',all(not r.get('normalized_result',{}).get('messages_truncated') for r in bundle['records']))
errors=[m for m in messages if m.get('type')=='error'];check('only documented rejected pre-spawn rebind error',all(m.get('description')=='Error: prefix mismatch tick' for m in errors))
dump(OUT/'NATIVE_ACCEPTANCE_REVIEW.json',dict(verdict='PARTIAL',checks=checks,simulation_steps=ret['simulation_ticks'],post_movement_steps=stable['steps_since_last_movement'],same_sector='PASS',yaw='PASS',render_transform='PASS',interpolation='PARTIAL: teleport reset and natural native output verified; intermediate sprite submissions not recorded',cross_sector='BLOCKED: only same-sector LinkSector invoked; destination and portal route not attested',lifecycle='PASS',camera=camera,isolation_guard=guard,documented_pre_spawn_errors=errors))
dump(OUT/'MOVEMENT_OBSERVATIONS.json',dict(plan=events('movement_plan')[0],moves=moves,sprites=sprites,interpolation=lerp,stability=stable))
dump(OUT/'LIFECYCLE_JOURNAL.json',[x for x in rows if x['event'] in ['native_factory_call','factory_return','spawn_identity_verified','remove_request','native_remove_postcondition','deferred_drain_entry','deferred_drain_return','native_on_remove_entry','native_on_remove_return','actor_identity_retired','native_pool_return_entry','native_pool_return_complete','deferred_retired']])
dump(OUT/'VISUAL_EVIDENCE.json',[dict(metadata=load(p),metadata_sha256=sha(p)) for p in sorted(run.glob('FRAME_*.json'))])
print('REVIEW PASS (consistent PARTIAL evidence)',len(checks),'checks; no whole-DOC58A PASS')
