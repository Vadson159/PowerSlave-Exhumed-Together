"""Offline audit of saved C20 evidence. Never attaches to or launches a game."""
import hashlib,json,math
from pathlib import Path
from prepare import OUT

run=OUT/'C20_CONTEXT_WORLD_EXIT'
def read(name):return json.loads((run/name).read_text(encoding='utf-8-sig'))
def norm(name):return read(name)['structuredContent']['normalized_result']
def payloads(name):return [m['payload'] for m in norm(name)['messages'] if isinstance(m.get('payload'),dict)]
final=norm('status_13_after_close.json');p=payloads('status_13_after_close.json')
def last(kind):return [x for x in p if x.get('kind')==kind][-1]
unbound=last('C_context_unbound_before_world_exit');returned=last('C_native_world_exit_returned')
destructors=[x for x in p if x.get('kind')=='C_manual_native_puppet_actor_destructor_return']
release=last('C_manual_native_source_owner_release');hooks=last('C_hooks_restored');gate=last('C_host_restart_gate_restored')
before=[x for x in payloads('status_11_solo_before_W.json') if x.get('kind')=='tick_sample'][-1]
after=[x for x in payloads('status_12_solo_after_W.json') if x.get('kind')=='tick_sample'][-1]
distance=math.dist(before['player']['origin'],after['player']['origin'])
checks={
 'complete_saved_capture':not final.get('messages_truncated') and final.get('state')=='detached',
 'live_owned_context_at_boundary':unbound['before']['live_identity'] and unbound['before']['health']==200 and unbound['before']['refs']==1,
 'handle_null_before_destructors':unbound['external_handle_after']=='0x0' and all(unbound['utc']<x['utc'] for x in destructors),
 'primary_unchanged_during_unbind':unbound['primary_before']==unbound['primary_after'] and unbound['primary_inventory_before']==unbound['primary_inventory_after'],
 'both_generations_destructed':{x['generation'] for x in destructors}=={1,2} and all(x['native_actor_destructor_returned'] for x in returned['owned_puppet_destructors']),
 'native_world_exit_returned_unbound':returned['context_unbound'] and returned['external_handle']=='0x0' and returned['guards'] and all(x['utc']<=returned['utc'] for x in destructors),
 'natural_old_source_release':release['before']['refs']==1 and release['after']['refs']==0 and release['source_owner_after']=='0x0' and release['native_calls_by_observer']==0,
 'exact_native_hooks_restored':hooks['passed'] and gate['passed'],
 'explicit_gameplay_script_unloaded':read('unload_08_gameplay_script.json')['structuredContent']['unloaded'] is True,
 'solo_gameplay_after_unload':before['state']==after['state']==3 and before['pending']==after['pending']==0 and before['player']['owner']==after['player']['owner']=='0x140af33e0' and before['player']['health']==after['player']['health']==200 and distance>1 and after['tick_count']-before['tick_count']>=600,
 'no_recorded_script_or_non_system_faults':not [m for m in final['messages'] if m.get('type')=='error'] and not [x for x in p if x.get('kind')=='C_native_exception' and x.get('type')!='system'],
}
result={'status':'PASS' if all(checks.values()) else 'FAIL','scope':'Bounded external Player unbind BEFORE native world teardown, both Puppet generations, explicit gameplay script unload and solo reentry. Not full M0 acceptance.','full_m0':False,'checks':checks,
 'source_sha256':norm('load_03_respawn.json')['source_sha256'],'boundary':unbound,'world_exit':returned,
 'natural_source_release':release,'solo_after_unload':{'before':before,'after':after,'movement':distance,'native_tick_delta':after['tick_count']-before['tick_count']},
 'exit_auxiliary_only':read('exit.json'),'exit_not_lifecycle_proof':True,
 'evidence_sha256':{n:hashlib.sha256((run/n).read_bytes()).hexdigest() for n in ['status_13_after_close.json','status_06_native_world_exit.json','unload_08_gameplay_script.json','status_11_solo_before_W.json','status_12_solo_after_W.json','exit.json']},
 'limitations':['System/OutputDebugString exception events retained without asserting exception-free shutdown.','After gameplay script unload its exception observer is gone; solo ticks/movement and final exit are the available post-unload observations.','Loading initial zero-time test save did not open a level; cause UNKNOWN. New empty slot was used for solo reentry without overwriting the first.','Later source refactor/direct-Quit hook and directed physics were not executed by C20.']}
with (run/'RESULT_SUMMARY.json').open('x',encoding='utf8') as stream:json.dump(result,stream,indent=2)
print(json.dumps({'status':result['status'],'checks':checks,'movement':distance,'tick_delta':after['tick_count']-before['tick_count'],'exit_used_as_lifecycle_proof':False}))
raise SystemExit(not all(checks.values()))
