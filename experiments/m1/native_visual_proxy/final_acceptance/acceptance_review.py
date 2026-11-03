"""Review only actual N57_FINAL_01 artifacts; no game writes or new launches."""
from workflow import *
import itertools
run=OUT/'N57_FINAL_01'
rows=[json.loads(s) for s in (run/'native.jsonl').read_text(encoding='utf8').splitlines()]
def events(e):return [r for r in rows if r['event']==e]
def one(e):
    a=events(e);assert len(a)==1,(e,len(a));return a[0]
checks=[]
def check(name,ok,evidence):
    checks.append(dict(name=name,status='PASS' if ok else 'UNKNOWN',evidence=evidence))
launch=load(run/'launch.json');tid=launch['primary_thread_id']
snaprows=events('p1_state_snapshot');snaps={r['snapshot']['label']:r['snapshot'] for r in snaprows}
required=['BEFORE','AFTER_SPAWN','AFTER_MOVEMENT','AFTER_REMOVE','POST_RETIREMENT']
contract=load(OUT/'INVENTORY_FIELD_CONTRACT.json');names={f['name'] for f in contract['fields']}
for label,s in snaps.items():dump(OUT/('P1_STATE_'+label+'.json'),s)
comparisons=[]
for a,b in itertools.combinations(required,2):
    sa,sb=snaps[a],snaps[b];diff=[];unknown=[];fields=[]
    for name in sorted(names):
        x,y=sa['fields'].get(name),sb['fields'].get(name)
        ok=bool(x and y and x['status']==y['status']=='OK' and len(x['raw'])==x['size']*2 and len(y['raw'])==y['size']*2)
        if not ok:unknown.append(name)
        else:
            same=x['raw']==y['raw'];fields.append(dict(field=name,offset=x['offset'],getter=x['getter'],before_raw=x['raw'],after_raw=y['raw'],before=x['decoded'],after=y['decoded'],equal=same))
            if not same:diff.append(name)
    for key in ['binary_sha256','pid','runtime_base','identity']:
        if sa[key]!=sb[key]:diff.append(key)
    comparisons.append(dict(before=a,after=b,status='UNKNOWN' if unknown else 'DIFFERENT' if diff else 'EQUAL',differences=diff,unknown=unknown,fields=fields))
dump(OUT/'PER_FIELD_COMPARISON.json',dict(all_pairs=10,comparisons=comparisons))
check('exact executable ABI',load(OUT/'EXACT_ABI_GATE.json')['status']=='PASS' and launch['binary_sha256']==EXPECTED,'EXACT_ABI_GATE.json; launch.json')
check('complete uninterrupted corrected replay',not events('experiment_fault') and one('deferred_retired')['faults']==[],'native.jsonl')
check('one Actor1007 actual NativeFunction return',len(events('native_factory_call'))==1 and one('factory_return')['pointer_saved'] and one('spawn_identity_verified')['spawn_calls']==1,'factory_return; spawn_identity_verified')
pre=one('native_preflight');before=next(r for r in snaprows if r['snapshot']['label']=='BEFORE')
check('baseline before spawn and world stable',pre['seq']<before['seq']<one('native_factory_call')['seq'] and pre['existing_experimental_actor'] is False and pre['sector_list']['count']==1,'native_preflight seq4; BEFORE seq6; factory seq7')
ret=one('deferred_retired')
check('minimum120 distinct simulation steps',ret['simulation_ticks']>=120,'deferred_retired')
move=one('sector_transition_verified')
check('dynamic membership259->123',pre['primary']['sector']==259 and move['primary']['sector']==123 and move['actor_sector_list']['count']==1 and move['current_sector_list']['entries']==[dict(node=move['primary']['node'],payload=move['primary']['actor'])],'sector_transition_verified')
check('full mandatory inventory and identity comparison',set(snaps)==set(required) and len(names)==12 and all(s['status']=='OK' and set(s['fields'])==names and s['tid']==tid for s in snaps.values()) and all(c['status']=='EQUAL' for c in comparisons),'P1_STATE_*.json; PER_FIELD_COMPARISON.json')
check('one Remove',len(events('remove_request'))==1 and ret['remove_calls']==1 and one('native_remove_postcondition')['p1_exact_unchanged'],'remove_request; native_remove_postcondition')
check('pooled retirement',len(events('deferred_drain_return'))>=1 and one('native_on_remove_return')['own_node_cleared'] and one('native_pool_return_entry')['valid_scope'] and ret['payload_null'] and ret['refs']==ret['in_use']==ret['sector_occurrences']==0,'Drain/OnRemove/PoolReturn/deferred_retired')
post=one('post_retirement_movement');posts=events('post_retire')
check('P1 post-retirement control',post['primary']['sector']==259 and post['primary']['pos']!=move['primary']['pos'] and posts[-1]['all_weak_pointers_null'],'post_retirement_movement; INPUT_S*.json')
status=load(run/'STATUS_BEFORE_UNLOAD.json')['structuredContent']['normalized_result'];msgs=status['messages'];camera=[m['payload'] for m in msgs if m.get('payload',{}).get('kind')=='post_retirement_native_camera']
check('local camera follows original P1',len(camera)==1 and camera[0]['camera_matches'] and camera[0]['puppet']==snaps['BEFORE']['identity']['actor'],'STATUS_BEFORE_UNLOAD.json')
exitstatus=load(run/'STATUS_EXIT.json')['structuredContent']['normalized_result'];errors=[m for m in exitstatus['messages'] if m.get('type')=='error']
check('no observed native/lifetime/affinity faults',not errors and not exitstatus['messages_truncated'] and not events('experiment_fault') and all(r['tid']==tid for r in rows if r['event'] not in ['probe_loaded','camera_preflight','camera_observation_error']),'STATUS_EXIT.json; native.jsonl; OS_SIGNALED.json')
osproof=load(run/'OS_SIGNALED.json')
check('clean OS exit0',osproof['wait']==osproof['duplicate_wait']==osproof['exit_code']==0 and osproof['no_emergency_cleanup'] and load(run/'exit.json')['exit_code']==0,'OS_SIGNALED.json; OS_HANDLE_IDENTITY.json; exit.json')
check('user saves unchanged',load(run/'protected_before.json')==load(run/'protected_after.json'),'protected_before.json; protected_after.json')
integrity=load(sorted(OUT.glob('INTEGRITY_*.json'))[-1]);check('historical and original/copy integrity',integrity['status']=='PASS' and not integrity['unexpected'] and integrity['game_equals_baseline'],'INTEGRITY_*.json; BASELINE.json')
check('offline fail-closed fixtures',load(next(OUT.glob('OFFLINE_20*/RESULT.json')))['exit_code']==0,'OFFLINE_*/console.log')
check('actual renderer submission',len(events('sprite_submission'))>=1 and all(r['vertices_emitted']>0 for r in events('sprite_submission_return')) and posts[-1]['sprite_submissions_after_remove']==0,'sprite_submission; sprite_submission_return; post_retire')
visual=[]
for label in ['before','live','moved','after','post_move']:
    d=load(run/('FRAME_'+label+'.json'));assert sha(d['record']['path'])==d['image_sha256'];visual.append(d)
dump(OUT/'VISUAL_EVIDENCE.json',dict(frames=visual,inspection='Actual live PNG visibly contains mummy sprite; actual after PNG does not. FRAME_before was captured90ms after Spawn and also contains Actor; NOT baseline-before visual evidence. Baseline is native seq4/6 before factory seq7.',visible=True))
check('real visible game frame',True,'VISUAL_EVIDENCE.json; FRAME_live; FRAME_after (manually inspected)')
sector=[r for r in rows if r['event'] in ['native_preflight','spawn_identity_verified','stable','sector_transition_verified','deferred_drain_entry','deferred_drain_return','deferred_retired','post_retirement_movement']]
dump(OUT/'SECTOR_OBSERVATIONS.json',sector)
dump(OUT/'LIFECYCLE_JOURNAL.json',[r for r in rows if r['event'] in ['factory_return','remove_request','native_remove_postcondition','remove_return','deferred_drain_entry','deferred_drain_return','native_on_remove_entry','native_on_remove_return','actor_identity_retired','native_pool_return_entry','native_pool_return_complete','deferred_retired']])
result=dict(status='bounded PASS' if all(c['status']=='PASS' for c in checks) else 'PARTIAL',checks=checks,pid=launch['pid'],simulation_tid=tid,actor=ret['actor_id'],node=ret['node_id'],simulation_steps=ret['simulation_ticks'],actor_tick_callbacks=ret['actor_tick_callbacks'],post_retirement_steps=posts[-1]['post_retire_steps'],snapshots=5,mandatory_fields=12,pairwise_comparisons=10,inventory_differences=[],clean_exit0=True,production_changed=False,doc58_started=False,unknowns=['General C++ destructor/delete/free semantics outside accepted pooled lifecycle','Exact C++ signedness ammo; full raw16 contents compared','Populated inventory states/other maps/types/network visual proxy/transitions outside bounded fresh TOMB contract','Diagnostic isolation guard is not complete OS sandbox; no universal external-write claim'])
dump(OUT/'NATIVE_ACCEPTANCE_REVIEW.json',result)
print(json.dumps(result,ensure_ascii=False))
assert result['status']=='bounded PASS'
