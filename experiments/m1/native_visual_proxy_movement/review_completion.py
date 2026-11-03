"""Review DOC58A completion run N58A_C01 from recorded evidence; never opens or mutates a KEX process."""
from workflow import *
from itertools import combinations
run=OUT/'N58A_C01'
rows=[json.loads(x) for x in (run/'native.jsonl').read_text('utf8').splitlines()]
def events(name):return [x for x in rows if x['event']==name]
checks=[]
def check(name,value,required=True):
    checks.append(dict(name=name,passed=bool(value),required=required))
    if required:assert value,name
    return bool(value)
launch=load(run/'launch.json');sim=launch['primary_thread_id']
check('exact EXE SHA-256',launch['binary_sha256']==EXPECTED and sha(EXE)==EXPECTED)
check('all native journal events after load on attested Simulation TID',all(r['tid']==sim for r in rows if r['event']!='probe_loaded'))
pre=events('native_preflight')[0]
check('D1 map identity and stable state3/3/0',pre['world']['map']=='maps/TOMB.MAP' and (pre['world']['state'],pre['world']['next'],pre['world']['pending'])==(3,3,0))
check('D1 P1 alone in source 259, destination 123 empty, no experimental Actor',pre['primary']['sector']==259 and pre['sector_list']['count']==1 and pre['sector_list']['entries'][0]['payload']==pre['primary']['actor'] and pre['destination_sector_list']['count']==0 and pre['existing_experimental_actor'] is False)
snaps=[x['snapshot'] for x in events('p1_state_snapshot')]
labels=[s['label'] for s in snaps]
check('complete 12-field P1 snapshots',all(s['status']=='OK' and len(s['fields'])==12 for s in snaps) and labels==['BEFORE','AFTER_SPAWN','AFTER_SMOKE','AFTER_RETURN','AFTER_CROSS_SECTOR','AFTER_CROSS_STABILITY','AFTER_REMOVE','POST_RETIREMENT'])
comparisons=[]
for a,b in combinations(snaps,2):
    fields={k:dict(equal=a['fields'][k]['raw']==b['fields'][k]['raw'],before=a['fields'][k],after=b['fields'][k]) for k in a['fields']}
    check('P1 '+a['label']+' -> '+b['label'],a['identity']==b['identity'] and all(v['equal'] for v in fields.values()))
    comparisons.append(dict(before=a['label'],after=b['label'],fields=fields))
for s in snaps:dump(OUT/('P1_STATE_'+s['label']+'.json'),s)
dump(OUT/'PER_FIELD_COMPARISON.json',comparisons)
check('single native spawn',len(events('native_factory_call'))==len(events('factory_return'))==len(events('spawn_identity_verified'))==1)
check('no experiment faults',not events('experiment_fault'))
spawn=events('spawn_identity_verified')[0]
check('stock Actor1007 passive identity',spawn['actor']['type']==1007 and spawn['actor']['flags']==0x40 and spawn['actor']['vtable']=='0x140528608' and spawn['actor']['sector']==259)
moves=events('movement_applied');status=events('controller_status')
check('D2 same-sector smoke: teleport+yaw+same-sector LinkSector applied',[m['operation'] for m in moves[:3]]==['position','yaw','sector'] and moves[2]['value']['cross'] is False and all(s['ok'] for s in status))
smoke=events('exception_status_smoke')[0]
check('D2 exception-to-status in KEX: VALIDATION_UNKNOWN then STOPPED, zero writes',smoke['passed'] and smoke['first']['reason']=='VALIDATION_UNKNOWN' and smoke['second']['reason']=='STOPPED' and smoke['adapter_writes']==0 and smoke['actor_unchanged'])
sprites=events('sprite_submission')
for m in [x for x in moves if x['operation']=='position']:
    check('settled live-identity sprite after position revision '+str(m['revision']),any(s['identity']=='OURS' and s['revision']>=m['revision'] and s['origin']==m['value'] for s in sprites))
ret=events('return_teleport')[0];after_ret=[s for s in sprites if s['revision']==ret['revision']]
check('return teleport reset previous=output=target and first sprite settled',ret['reset'] and after_ret and after_ret[0]['origin']==ret['target'] and not after_ret[0]['intermediate'])
plan=events('cross_sector_plan')[0]
check('pre-spawn geometry plan: every geometric check passed, only cache deferred',plan['reason']=='FLOOR_CEILING_CONTRACT_UNKNOWN' and all(c['passed'] for c in plan['checks'][:-1]))
att=events('cross_sector_attestation')[0]
check('D4 attestation ATTESTED (all checks)',att['status']=='ATTESTED' and all(c['passed'] for c in att['checks']))
check('D4 unique destination over complete sector scan',att['input']['scan']['hits']==[123] and att['input']['scan']['skipped']==[] and att['input']['scan']['count']>0)
check('D4 native point-in-sector1cf540 witness and FindSector mirror',att['input']['native_point_in_sector']['destination'] is True and att['input']['native_point_in_sector']['source'] is False and att['input']['find_sector']['sector']==123)
c=att['input']['cache']
check('D4 native floor1a3eb0/ceiling1a4060 at target equal Actor caches, water flags equal, physics gate clear',c['actor_floor']==c['destination_floor'] and c['actor_ceiling']==c['destination_ceiling'] and c['source_flags14']==c['destination_flags14'] and c['physics_gate_bit7'] is False)
link=events('cross_sector_link')[0]
b,a=link['before_lists'],link['after_lists'];node,actor=spawn['node_id'],spawn['actor_id']
check('D4 exact native sequence origin->LinkSector(123)->reset',link['sequence']==['origin_store_xyz','LinkSector1d3d70(actor,123)','interpolant_reset1d3e60(actor+50)'])
check('D4 Actor node absent from 259, exactly one in 123, P1 node kept in 259',[e['node'] for e in a['source']['entries']]==[pre['primary']['node']] and a['destination']['entries']==[dict(node=node,payload=actor)] and b['source']['found']==1 and b['destination']['count']==0)
check('D4 reciprocal walkers valid both lists before/after',all(x['valid'] for x in [b['source'],b['destination'],a['source'],a['destination']]) and link['membership']['ok'])
check('D4 sectorIndex 123, own node unchanged, caches unchanged',link['sector_index']==123 and link['own_node']==node and link['caches_before']==link['caches_after'])
check('D4 interpolants reset to target (no stale render values)',link['teleport_reset'] and link['interpolants']['previous']==link['interpolants']['output']==[-7424,-7008,1240])
cross_sprites=[s for s in sprites if s['revision']==link['cross']['revision']]
check('D4 renderer: identity-matched settled sprite with sector123 at target',cross_sprites and cross_sprites[0]['identity']=='OURS' and cross_sprites[0]['sector']==123 and cross_sprites[0]['origin']==[-7424,-7008,1240] and not cross_sprites[0]['intermediate'])
stab=events('cross_sector_stability')[0]
check('D5 >=120 distinct steps after transfer with membership 259:0 / 123:1',stab['steps_since_transfer']>=120 and stab['source_list']['found']==0 and stab['destination_list']['found']==1 and stab['destination_list']['count']==1)
check('D5 sprites in destination and no ambiguous identity',stab['sprites_in_destination']>0 and stab['ambiguous_sprites']==0)
check('no ambiguous sprite identity events',not events('sprite_identity_ambiguous'))
ms=events('movement_stability')[0]
check('D6 Remove issued from actual sector 123 after stability',ms['actor_sector']==123 and ms['steps_since_transfer']>=120)
rem=events('native_remove_postcondition')[0]
check('D6 Remove postcondition flags0x2000 removed1 in 123 list',rem['removed']==1 and rem['flags']&0x2000 and rem['sector_list']['found']==1 and rem['p1_exact_unchanged'])
dr=events('deferred_drain_return')[0]
check('D6 Drain unlinked node from 123',dr['sector_list']['count']==0 and dr['refs']==0)
check('D6 OnRemove cleared own node',events('native_on_remove_return')[0]['own_node_cleared'])
pr=events('native_pool_return_entry')[0]
check('D6 PoolReturn inside controlled Sweep, no Actor reads after',pr['valid_scope'] and pr['actor_reads_after_entry'] is False)
fin=events('deferred_retired')[0]
check('D6 node retirement payload null refs0 in_use0, occurrences 123:0 259:0',fin['payload_null'] and fin['refs']==0 and fin['in_use']==0 and fin['sector_occurrences']==0 and fin['source_sector_occurrences']==0)
check('exactly one Remove and retirement',fin['spawn_calls']==fin['remove_calls']==1 and len(events('native_remove_postcondition'))==len(events('native_pool_return_entry'))==1)
check('simulation steps equal Actor Tick callbacks',fin['simulation_ticks']==fin['actor_tick_callbacks'])
check('no sprite after Remove',not events('sprite_after_remove'))
post=events('post_retire')
check('post-retirement P1 native W/S movement 259->123->259 with inventory unchanged',any(p['primary']['sector']==123 for p in post) and post[-1]['primary']['sector']==259 and all(p['p1_inventory_unchanged'] and p['all_weak_pointers_null'] for p in post))
inter=events('sprite_intermediate_submission');lerp=events('native_interpolation_sample')
check('interpolation step executed (command 4)',bool(events('interpolation_step_started')),required=False)
check('intermediate identity-correlated sprite submission recorded',bool(inter),required=False)
check('no native interpolation sample after any reset (no stale lerp)',not lerp)
osproof=load(run/'OS_SIGNALED.json');check('D7 retained+duplicate wait0, exit0, no emergency',osproof['wait']==osproof['duplicate_wait']==osproof['exit_code']==0 and osproof['no_emergency_cleanup'] and load(run/'exit.json')['exit_code']==0)
check('protected profiles equal',load(run/'protected_before.json')==load(run/'protected_after.json'))
bundle=load(OUT/'REA_FINAL.json');messages=[m for r in bundle['records'] if r['operation'] in ['frida_session_status','load_frida_script'] for m in r.get('normalized_result',{}).get('messages',[])]
payloads=[m.get('payload',{}) for m in messages if isinstance(m.get('payload'),dict)]
camera=next(p for p in payloads if p.get('kind')=='post_retirement_native_camera');check('local native camera follows original P1',camera['camera_matches'] and camera['player']==pre['primary']['player'] and camera['puppet']==pre['primary']['actor'])
restore=[p for p in payloads if p.get('kind')=='58A_hooks_restored'];check('all 23 site prefixes restored after unload',restore and all(p['passed'] and len(p['results'])==23 for p in restore))
guards=[p for p in payloads if p.get('kind')=='isolation_write_guard_summary'];guard=max(guards,key=lambda p:p.get('write_opens',0))
check('isolation guard no errors/overflow',guard.get('allowed_disk',0)>0 and guard.get('errors',0)==guard.get('unique_overflow',0)==0)
outside=[p for p in payloads if p.get('kind')=='file_write_open_observed' and p.get('succeeded') and not Path(p.get('path','')).resolve().is_relative_to(ROOT)]
check('no successful outside-workspace write opens',not outside)
check('no truncated or error MCP messages',all(not r.get('normalized_result',{}).get('messages_truncated') for r in bundle['records']) and not [m for m in messages if m.get('type')=='error'])
interp='PARTIAL' if not inter else 'PASS'
verdict=dict(SAME_SECTOR='PASS',INTERPOLATION=interp,CROSS_SECTOR='PASS',LIFECYCLE='PASS',OVERALL='PASS' if interp=='PASS' else 'PARTIAL')
dump(OUT/'NATIVE_ACCEPTANCE_REVIEW.json',dict(verdict=verdict,checks=checks,required_failed=[c for c in checks if c['required'] and not c['passed']],optional_not_met=[c['name'] for c in checks if not c['required'] and not c['passed']],
    simulation_steps=fin['simulation_ticks'],actor_tick_callbacks=fin['actor_tick_callbacks'],steps_after_transfer_at_remove=ms['steps_since_transfer'],post_retire_steps=post[-1]['post_retire_steps'],
    interpolation_reason=None if inter else 'Command 4 (native_step) was not sent: frustum-ordered plan gave positions[2]=[-7360,-6848] while command 4 required an X-only >=64 pair ending at positions[3]=[-7456,-6880]; sending it would have thrown and triggered safe-cleanup Remove before cross-sector. No intermediate sample exists in this run.',
    camera=camera,isolation_guard=guard))
dump(OUT/'CROSS_SECTOR_EVIDENCE.json',dict(plan=plan,attestation=att,link=link,movement=[m for m in moves if m['operation']=='sector'],first_destination_sprite=cross_sprites[0],stability=stab,preflight_lists=dict(source=pre['sector_list'],destination=pre['destination_sector_list'])))
dump(OUT/'MOVEMENT_OBSERVATIONS.json',dict(moves=moves,controller_status=status,sprites=sprites,return_teleport=ret,exception_smoke=smoke,interpolation_samples=lerp,intermediate_sprites=inter))
dump(OUT/'LIFECYCLE_JOURNAL.json',[x for x in rows if x['event'] in ['native_factory_call','factory_return','spawn_identity_verified','remove_request','native_remove_postcondition','deferred_drain_entry','deferred_drain_return','native_on_remove_entry','native_on_remove_return','actor_identity_retired','native_pool_return_entry','native_pool_return_complete','deferred_retired']])
dump(OUT/'VISUAL_EVIDENCE.json',[dict(metadata=load(p),metadata_sha256=sha(p)) for p in sorted(run.glob('FRAME_*.json'))])
print('REVIEW',verdict,sum(c['passed'] for c in checks),'/',len(checks),'checks')
