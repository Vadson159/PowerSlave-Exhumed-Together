"""Review DOC58A interpolation follow-up run N58A_C02 from recorded evidence; never opens or mutates a KEX process."""
from workflow import *
from itertools import combinations
import math
run=OUT/'N58A_C02';SESSION='f7f43f03-b2e6-4b06-8eb8-f2863d0f4413'
rows=[json.loads(x) for x in (run/'native.jsonl').read_text('utf8').splitlines()]
def events(name):return [x for x in rows if x['event']==name]
checks=[]
def check(name,value):
    checks.append(dict(name=name,passed=bool(value)));assert value,name
launch=load(run/'launch.json');sim=launch['primary_thread_id']
check('exact EXE SHA-256',launch['binary_sha256']==EXPECTED and sha(EXE)==EXPECTED)
check('all events after load on attested Simulation TID',all(r['tid']==sim for r in rows if r['event']!='probe_loaded'))
pre=events('native_preflight')[0]
check('baseline map/state/P1 alone in 259',pre['world']['map']=='maps/TOMB.MAP' and (pre['world']['state'],pre['world']['next'],pre['world']['pending'])==(3,3,0) and pre['primary']['sector']==259 and pre['sector_list']['count']==1)
snaps=[x['snapshot'] for x in events('p1_state_snapshot')]
check('complete 12-field snapshots',[s['label'] for s in snaps]==['BEFORE','AFTER_SPAWN','AFTER_SMOKE','AFTER_INTERPOLATION_STEP','AFTER_RETURN','AFTER_REMOVE','POST_RETIREMENT'] and all(s['status']=='OK' and len(s['fields'])==12 for s in snaps))
comparisons=[]
for a,b in combinations(snaps,2):
    fields={k:dict(equal=a['fields'][k]['raw']==b['fields'][k]['raw'],before=a['fields'][k],after=b['fields'][k]) for k in a['fields']}
    check('P1 '+a['label']+' -> '+b['label'],a['identity']==b['identity'] and all(v['equal'] for v in fields.values()))
    comparisons.append(dict(before=a['label'],after=b['label'],fields=fields))
C2=OUT/'C02';C2.mkdir(exist_ok=True)
for s in snaps:dump(C2/('P1_STATE_'+s['label']+'.json'),s)
dump(C2/'PER_FIELD_COMPARISON.json',comparisons)
check('single spawn, no faults',len(events('factory_return'))==1 and not events('experiment_fault'))
spawn=events('spawn_identity_verified')[0]
check('stock passive Actor1007',spawn['actor']['type']==1007 and spawn['actor']['flags']==0x40 and spawn['actor']['vtable']=='0x140528608')
smoke=events('exception_status_smoke')[0];check('same-sector smoke and exception-to-status',smoke['passed'])
step=events('interpolation_step_started')[0]
check('native_step between approved points >=64',math.dist(step['from'],step['to'])>=64)
lerp=events('native_interpolation_sample');inter=events('sprite_intermediate_submission')
check('native lerp outputs strictly inside (0,1), formula error <=1e-3',lerp and all(0<x['t']<1 and x['formula_error']<=1e-3 for x in lerp) and [x['t'] for x in lerp]==sorted(x['t'] for x in lerp))
check('lerp previous/current are step from/to',all(x['previous']==step['from'] and x['current']==step['to'] for x in lerp))
check('rotation constant during step (position-only interpolation)',all(x['rotation']['previous']==x['rotation']['current']==x['rotation']['output']==[0,0,0.25] for x in lerp))
check('intermediate sprite submissions recorded',bool(inter))
check('every intermediate sprite: identity OURS, not settled, sector259, origin == last native lerp output',all(s['match']['status']=='OURS' and s['match']['intermediate'] and s['entry']['sector']==259 and s['correlated_with_last_lerp'] and s['entry']['origin']==s['last_lerp']['output'] for s in inter))
check('every lerp output reached a sprite submission',all(any(s['last_lerp']['sample']==x['sample'] and s['entry']['origin']==x['output'] for s in inter) for x in lerp))
check('sprite follows its lerp in the next render pass',all(s['render_frame']==s['last_lerp']['render_frame']+1 for s in inter))
check('sprite angles equal settled yaw',all(s['entry']['angles']==[0,0,0.25] for s in inter))
check('no ambiguous sprite identity',not events('sprite_identity_ambiguous'))
ret=events('return_teleport')[0];after=[s for s in events('sprite_submission') if s['revision']==ret['revision']]
check('teleport reset: previous=output=target, first sprite settled OURS',ret['reset'] and after and after[0]['origin']==ret['target'] and after[0]['identity']=='OURS' and not after[0]['intermediate'])
check('no lerp sample after reset',all(x['revision']<ret['revision'] for x in lerp))
fin=events('deferred_retired')[0]
check('one Remove -> Drain -> OnRemove -> PoolReturn -> retirement',fin['payload_null'] and fin['refs']==0 and fin['in_use']==0 and fin['sector_occurrences']==0 and fin['spawn_calls']==fin['remove_calls']==1 and events('native_on_remove_return')[0]['own_node_cleared'] and events('native_pool_return_entry')[0]['valid_scope'])
check('steps == Actor callbacks',fin['simulation_ticks']==fin['actor_tick_callbacks'])
check('no sprite after Remove',not events('sprite_after_remove'))
osproof=load(run/'OS_SIGNALED.json');check('WM_CLOSE exit0 retained+duplicate wait0',osproof['wait']==osproof['duplicate_wait']==osproof['exit_code']==0 and osproof['no_emergency_cleanup'] and load(run/'exit.json')['exit_code']==0)
check('protected profiles equal',load(run/'protected_before.json')==load(run/'protected_after.json'))
bundle=load(OUT/'REA_C02_FINAL.json');mine=[r for r in bundle['records'] if r.get('parameters',{}).get('session_id')==SESSION or r.get('source_parameters',{}).get('session_id')==SESSION]
messages=[m for r in mine for m in r.get('normalized_result',{}).get('messages',[])];payloads=[m['payload'] for m in messages if isinstance(m.get('payload'),dict)]
camera=next(p for p in payloads if p.get('kind')=='post_retirement_native_camera');check('camera follows original P1',camera['camera_matches'] and camera['puppet']==pre['primary']['actor'])
restore=[p for p in payloads if p.get('kind')=='58A_hooks_restored'];check('23 prefixes restored',restore and all(p['passed'] and len(p['results'])==23 for p in restore))
guards=[p for p in payloads if p.get('kind')=='isolation_write_guard_summary'];guard=max(guards,key=lambda p:p.get('write_opens',0))
check('isolation guard clean, no outside writes',guard.get('allowed_disk',0)>0 and guard.get('errors',0)==guard.get('unique_overflow',0)==0 and not [p for p in payloads if p.get('kind')=='file_write_open_observed' and p.get('succeeded') and not Path(p.get('path','')).resolve().is_relative_to(ROOT)])
check('no truncated/error MCP messages',not any(r.get('normalized_result',{}).get('messages_truncated') for r in mine) and not [m for m in messages if m.get('type')=='error'])
dump(C2/'NATIVE_ACCEPTANCE_REVIEW.json',dict(verdict=dict(INTERPOLATION='PASS',SAME_SECTOR='PASS',LIFECYCLE='PASS'),checks=checks,step=step,lerp=lerp,intermediate_sprites=inter,return_teleport=ret,simulation_steps=fin['simulation_ticks'],camera=camera,isolation_guard=guard))
dump(C2/'LIFECYCLE_JOURNAL.json',[x for x in rows if x['event'] in ['native_factory_call','factory_return','spawn_identity_verified','remove_request','native_remove_postcondition','deferred_drain_entry','deferred_drain_return','native_on_remove_entry','native_on_remove_return','actor_identity_retired','native_pool_return_entry','native_pool_return_complete','deferred_retired']])
dump(C2/'VISUAL_EVIDENCE.json',[dict(metadata=load(p),metadata_sha256=sha(p)) for p in sorted(run.glob('FRAME_*.json'))])
print('REVIEW C02 INTERPOLATION PASS',len(checks),'checks')
