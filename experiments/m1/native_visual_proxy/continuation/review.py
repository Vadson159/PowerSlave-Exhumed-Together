"""Offline acceptance review. Positive checks do not fill unmeasured native gates."""
from workflow import *
import math
RUN='N57_CONT_01';run=OUT/RUN
DEST=OUT
if len(sys.argv)>1:
    tag=sys.argv[1];assert tag.replace('_','').isalnum()
    DEST=inside(OUT/'reviews'/tag);assert not DEST.exists();DEST.mkdir(parents=True)
def rows(name):return [json.loads(s) for s in (run/name).read_text(encoding='utf8').splitlines()]
native=rows('native.jsonl');cleanup=rows('cleanup.jsonl');checks=[]
def one(rs,event):
    r=[r for r in rs if r['event']==event];assert len(r)==1,(event,len(r));return r[0]
def check(name,value):
    checks.append(dict(name=name,passed=bool(value)));assert value,name
def inv(p):return {k:p[k] for k in ['health','owned_weapons','ammo','weapon','pending_weapon']}
def graph(p):return {k:p[k] for k in ['player','node','actor','own_node','owner','vtable']}
def dist(a,b):return math.dist(a,b)
loaded=one(native,'probe_loaded');pre=one(native,'native_preflight');ret=one(native,'factory_return');spawn=one(native,'spawn_identity_verified')
fault=one(native,'experiment_fault');refused=one(native,'cleanup_refused')
known=one(cleanup,'cleanup_identity_revalidated');request=one(cleanup,'cleanup_remove_request');removed=one(cleanup,'cleanup_remove_return');retired=one(cleanup,'cleanup_deferred_retired')
posts=[r for r in cleanup if r['event']=='cleanup_post_retire']
record=load(run/'STATUS_FINAL.json')['structuredContent'];n=record['normalized_result'];messages=n['messages']
payloads=[m['payload'] for m in messages if isinstance(m.get('payload'),dict)]
errors=[m for m in messages if m['type']=='error']
guard=max((p for p in payloads if p.get('kind')=='isolation_write_guard_summary'),key=lambda x:x.get('write_opens',0))
outside=[p for p in payloads if p.get('kind')=='file_write_open_observed' and p.get('succeeded') and not Path(p['path']).resolve().is_relative_to(ROOT)]
detached=next(p for p in payloads if p.get('kind')=='vp57c_detached_readonly')
check('exact binary and current run',sha(EXE)==EXPECTED and loaded['pid']==29204 and pre['tid']==18952 and loaded['image_base']=='0x140000000')
check('fresh map ready plus camera/sector preflight',load(run/'ISOLATION_GATE.json')['status']=='PASS' and pre['world']['map']=='maps/TOMB.MAP' and pre['sector_box']['full_inside'] and pre['camera']['new_box']['clip_full_inside'] and pre['camera']['new_box']['frustum']['full_inside'])
check('old point is outside this actual native view',not pre['camera']['old_box']['frustum']['native_overlap'])
check('one direct factory return retained',ret['returned']==spawn['actor']['actor'] and ret['pointer_saved'] and spawn['spawn_calls']==1 and ret['tid']==18952)
check('independent ordinary Actor/node',spawn['actor']['vtable']=='0x140528608' and spawn['actor']['actor']!=spawn['p1']['actor'] and spawn['actor']['node']!=spawn['p1']['node'] and spawn['actor']['flags']==64 and spawn['actor']['refs']==0)
check('real ordinary sprite submission and vertices',len([r for r in native if r['event']=='sprite_submission' and r['frame']==spawn['actor']['frame'] and r['origin']==[-7424,-6880,1240]])==2 and all(r['vertices_emitted']==20 for r in native if r['event']=='sprite_submission_return'))
check('at least120 measured simulation steps, separate Actor callbacks',fault['simulation_ticks']>=120 and fault['actor_tick_callbacks']>0 and request['additional_observed_world_steps']==12)
check('main run stopped without attempting Remove',refused['remove_calls']==0 and not any(r['event']=='remove_request' for r in native))
check('direct known pointer revalidated; P1 sector count explains false guard',known['actor']['actor']==ret['returned'] and known['actor']['node']==spawn['actor']['node'] and known['primary']['sector']==123 and known['sector_list']['count']==1 and known['sector_list']['found']==1 and known['actor']['flags']==64)
check('native input P1 motion with same ownership',dist(pre['primary']['pos'],known['primary']['pos'])>1 and all(graph(r['primary'])==graph(pre['primary']) for r in [known,request,removed]+posts))
check('verified health/weapon inventory preserved',all(inv(r['p1'])==inv(pre['primary']) for r in native if r['event']=='stable') and all(inv(r['primary'])==inv(pre['primary']) for r in [known,request,removed]+posts))
check('one native Remove with hidden/removed/ref1',request['remove_calls']==1 and removed['removed']==1 and removed['flags']==0x2044 and removed['refs']==1 and request['tid']==18952)
drains=[r for r in cleanup if r['event']=='cleanup_drain_return']
check('native deferred sector unlink and release',drains[0]['sector_list']['count']==0 and drains[0]['refs']==0 and drains[0]['payload']==ret['returned'])
pool=one(cleanup,'cleanup_pool_return_entry')
check('OnRemove and pool retirement in sweep',one(cleanup,'cleanup_on_remove_return')['own_node_cleared'] and pool['valid_scope'] and pool['refs']==0 and pool['actor_identity_invalidated'] and pool['return_address']=='0x1401b6417')
check('ObjectNode retired, all weak identities cleared',retired['payload_null'] and retired['in_use']==0 and retired['refs']==0 and retired['sector_occurrences']==0 and retired['all_weak_pointers_null'])
check('no post-retire actor reads/submissions or cleanup fault',all(p['actor_reads'] is False and p['all_weak_pointers_null'] and p['sprite_submissions_after_remove']==0 for p in posts) and not any(r['event'] in ['cleanup_blocked','cleanup_sprite_after_remove'] for r in cleanup))
check('P1 input still moves after retirement',dist(posts[0]['primary']['pos'],posts[-1]['primary']['pos'])>1 and posts[-1]['post_retire_world_steps']>=120)
check('all18 original KEX prefixes restored',len(detached['restored_anchors'])==18 and all(a['match'] for a in detached['restored_anchors']))
check('no Frida script errors/truncation or successful external writes',not errors and not n['messages_truncated'] and not outside and not guard['errors'] and not guard['unique_overflow'])
os_exit=load(run/'OS_SIGNALED.json');ob=load(run/'OBSERVER_CLOSED.json')
check('same retained and duplicate handles signal exit0',load(run/'OS_HANDLE_IDENTITY.json')['compare_same_kernel_object'] and os_exit['exit_code']==0 and os_exit['wait']==0 and os_exit['duplicate_wait']==0 and os_exit['no_emergency_cleanup'] and ob['close_duplicate'] and ob['close_primary'] and load(run/'exit.json')['exit_code']==0)
check('original protected profiles unchanged',load(run/'protected_before.json')==load(run/'protected_after.json'))
check('all task processes gone and sessions closed',not load(OUT/'SESSIONS_CLOSED.json')['post_close_processes'])
manifest=load(run/'PROBE_MANIFEST.json');plan=load(run/'CLEANUP_PLAN.json')
check('executed frozen sources match REA hashes',sha(run/'probe_bound.js')==manifest['bound_sha256']==load(run/'LOAD_PROBE_BOUND.json')['structuredContent']['normalized_result']['source_sha256'] and sha(run/'cleanup_bound.js')==plan['bound_sha256']==load(run/'LOAD_CLEANUP_BOUND.json')['structuredContent']['normalized_result']['source_sha256'])
membership=command(['C:/Program Files/nodejs/node.exe',LAB/'membership_test.js',run],'MEMBERSHIP_REGRESSION')
frames={}
for label in ['live','moved','after','post_move']:
    d=load(run/f'FRAME_{label}.json');p=Path(d['record']['path']);assert sha(p)==d['image_sha256'];frames[label]=d
before=run/'window_20261103_151218_644928.png';frames['gameplay_before']=dict(path=str(before),sha256=sha(before))
dump(DEST/'VISUAL_EVIDENCE.json',dict(status='VERIFIED NATIVE',frames=frames,visual_inspection='Real owned KEX-window frames inspected: no mummy before spawn, large opaque horizontal mummy live/moved, absent after Remove. Before marker FRAME_before is the earlier level-select screen and is NOT used as gameplay-before proof.',renderer= [r for r in native if r['event'].startswith('sprite_submission')],image_generation=False))
coordinates=[]
for name,s in load(OUT/'SITES.json').items():coordinates.append(dict(name=name,rva=hex(s['rva']),runtime_va=hex(0x140000000+s['rva']),prefix=s['prefix']))
dump(DEST/'RUNTIME_COORDINATES.json',dict(pid=29204,simulation_tid=18952,image_base='0x140000000',exe_sha256=EXPECTED,coordinates=coordinates,prepared_view='stack object; numeric copies only',actor=spawn['actor']['actor'],node=spawn['actor']['node'],p1=graph(pre['primary'])))
dump(DEST/'NATIVE_REVIEW.json',dict(checks=checks,positive_checks=len(checks),fixture_gate=load(OUT/'A_HARNESS_GATE.json')['status'],membership_regression=membership,observed_simulation_steps_before_guard=fault['simulation_ticks'],separate_actor_callbacks_before_guard=fault['actor_tick_callbacks'],additional_cleanup_world_steps=request['additional_observed_world_steps'],additional_cleanup_actor_callbacks=request['additional_actor_callbacks'],total_verified_lower_bound=fault['simulation_ticks']+request['additional_observed_world_steps'],exact_total_simulation_lifetime='UNKNOWN: observer stopped on sector-count false positive; no count assigned to gap',post_retire_distinct_world_steps=posts[-1]['post_retire_world_steps'],engine_stamp_spawn=ret['engine_stamp'],engine_stamp_remove=request['engine_stamp'],engine_stamp_retire=retired['engine_stamp'],health_and_weapon_inventory=inv(pre['primary']),extended_inventory_native='UNKNOWN: artifacts/abilities/keys/questItems/teamDolls/ankh fields were not sampled',main_harness_stop=[fault,refused],cleanup_retirement=retired,os_exit=os_exit,isolation_guard=guard,production_regressions_rerun=False,reason='Only new standalone Python/JS harness changed; production C++/DLL/network contracts remain byte-identical. Historical15 commands are not fresh evidence.',complete_acceptance_decision_separate=True))
print('NATIVE REVIEW',len(checks),'positive evidence checks; extended inventory and continuous total remain UNKNOWN')
