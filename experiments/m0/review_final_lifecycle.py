"""Review saved M0 final lifecycle observations; never executes the game."""
import argparse,collections,hashlib,json
from pathlib import Path
from prepare import OUT,EXPECTED

def main():
    a=argparse.ArgumentParser();a.add_argument('run');a.add_argument('mode',choices=['inflight','quit']);a=a.parse_args()
    run=(OUT/a.run).resolve()
    if not run.is_relative_to(OUT.resolve()):raise ValueError('Outside Evidence root')
    captures=[]
    for path in run.glob('status_*.json'):
        raw=json.loads(path.read_text(encoding='utf-8-sig'));n=raw.get('structuredContent',{}).get('normalized_result',{})
        if n.get('state')=='detached':captures.append((path,n))
    path,n=max(captures,key=lambda pair:pair[0].stat().st_mtime_ns)
    events=[m['payload'] for m in n['messages'] if isinstance(m.get('payload'),dict)]
    by=collections.defaultdict(list)
    for e in events:by[e.get('kind')].append(e)
    def last(kind):return by[kind][-1] if by[kind] else None
    launch=json.loads((run/'launch.json').read_text());gate=json.loads((run/'ISOLATION_GATE.json').read_text())
    checks={'exact_build_isolation':launch['binary_sha256']==EXPECTED and gate['status']=='PASS' and n['pid']==launch['pid'],
      'complete_final_capture':not n['messages_truncated'] and n['state']=='detached',
      'no_recorded_script_errors':not [m for m in n['messages'] if m.get('type')=='error'],
      'no_recorded_non_system_faults':not [e for e in by['C_native_exception'] if e.get('type')!='system'],
      'no_lifecycle_diagnostic_errors':not [e for e in events if 'error' in e.get('kind','').lower()]}
    details={}
    if a.mode=='inflight':
        death=last('C_inflight_death_observed');created=last('C_inflight_E_respawn_created');hit=last('C_inflight_original_shooter_damage')
        enter=last('C_inflight_projectile_destructor_enter');release=last('C_inflight_projectile_destructor_return');world=last('C_native_world_exit_returned')
        if not all([death,created,hit,enter,release,world]):raise ValueError('Required projectile lifecycle events missing')
        old=created['old'];fresh=created['secondary'];projectile=created['projectile_after']
        after=[e for e in by['C_inflight_projectile_tick'] if e['after_respawn'] and e['moved']>0]
        impacts=[e for e in by['C_fixture_impact_holder'] if e['victim']==fresh['actor'] and e['reference']['actor']==projectile['actor'] and e['reference']['impact_type']==27]
        checks.update({
          'native_projectile_flying_at_death':death['projectile']['removed']==0 and death['projectile']['projectile_flags']&4==0 and any(death['projectile']['velocity']),
          'E_after_native_death':created['frame']>death['frame'] and created['press']['vk']==69 and created['press']['armed'],
          'distinct_native_generation':old['generation']==1 and fresh['generation']==2 and old['actor']!=fresh['actor'] and old['node']['node']!=fresh['node'] and fresh['owner']==fresh['player'],
          'old_identity_retained_with_holder':old['live'] and old['node']['payload']==old['actor'] and old['node']['refs']>0 and projectile['source_owner']==old['node']['node'],
          'SourceOwner_unchanged_at_respawn':created['projectile_before']==created['projectile_after'],
          'fresh_native_start_kit_camera':fresh['health']==200 and created['inventory']['owned']==[1,1,0,0,0,0,0,0] and created['inventory']['counters'][1]==60 and created['camera']['at_130']==created['camera']['at_13c']==104,
          'native_movement_then_collision_after_E':bool(after) and bool(impacts) and impacts[0]['utc']>=created['utc'],
          'damage_from_original_shooter_generation':hit['damage']['source']==projectile['actor'] and hit['damage']['victim']==fresh['actor'] and hit['damage']['source_attribution'].get('player_generation')==1 and hit['damage']['source_owner']==fresh['player'] and hit['damage']['after']<hit['damage']['before'],
          'one_projectile_destructor_releases_SourceOwner':enter['entries']==release['entries']==release['returns']==1 and release['source_before']==old['node']['node'] and release['source_after']=='0x0' and release['refs_before']['refs']==1 and release['refs_after']['refs']==0 and release['native_calls_by_observer']==0,
          'both_Puppet_destructor_returns':len(by['C_owned_puppet_actor_destructor_return'])==2 and {e['generation'] for e in by['C_owned_puppet_actor_destructor_return']}=={1,2},
          'external_unbound_before_world_exit':world['context_unbound'] and world['external_handle']=='0x0' and world['guards'] and all(p['native_actor_destructor_returned'] for p in world['owned_puppet_destructors']),
          'primary_health_identity_preserved':all(e['local']['health']==200 and e['local']['live_identity'] for e in by['C_alive_sample']) and created['primary']['health']==death['primary']['health']==200,
          'native_calls_once':all(c==1 for c in collections.Counter(e['operation'] for e in by['C_native_call_attempt']).values()),
          'explicit_gameplay_script_unloaded':json.loads((run/'unload_06_gameplay.json').read_text())['structuredContent']['unloaded'] is True})
        details={'death':death,'created':created,'post_respawn_motion':after,'impact':impacts,'damage':hit,'destructor_release':release,'world_exit':world,
          'auxiliary_checks_not_passed':by['C_inflight_respawn_incomplete'],
          'auxiliary_scope':'120 alive ticks / two post-respawn movement calls / Puppet MoveCollision watcher were additional fixture expectations, not the required projectile lifecycle. Actual native impact/Damage is recorded. Fresh body took139 damage then native source-null79 damage at lower Z; no reference repair performed.',
          'secondary_native_damage':by['C_damage_observed'][:4]}
    else:
        ready=last('C_direct_quit_ready');quiesced=last('C_context_native_shutdown_quiesced');engine=last('C_direct_quit_engine_shutdown_return');final=last('C_direct_quit_final_exit');os_exit=last('C_direct_quit_os_exit')
        if not all([ready,quiesced,engine,final,os_exit]):raise ValueError('Required bounded quit events missing')
        q=final['snapshot'];t=q['trace'];known_actor=[e for e in t['arenas'] if e['arena_kind']=='known_actor_arena']
        engine_arenas=[e for e in t['arenas'] if e['arena_kind']=='known_engine_arena']
        checks.update({
          'both_players_live_at_ready':ready['primary']['health']==ready['secondary']['health']==200 and ready['primary']['live_identity'] and ready['secondary']['live_identity'],
          'live_secondary_unbound_before_service_cleanup':quiesced['before']['health']==200 and quiesced['before']['live_identity'] and quiesced['external_handle']=='0x0' and quiesced['guards'] and quiesced['utc']<=last('C_direct_quit_engine_shutdown_entry')['utc'],
          'exact_control_hooks_restored':q['trace']['hooks']['passed'] and last('C_hooks_restored')['passed'],
          'known_engine_arenas_returned':{e['rva'] for e in engine_arenas}=={'0xaecc20','0xaecca0'} and all(e['returned'] for e in engine_arenas),
          'engine_shutdown_completed':t['engine_enter']==t['engine_return']==1,
          'direct_path_bypasses_owned_world_teardown':t['state_exit']==0 and not known_actor and not t['destructors'],
          'quiescent_until_final_exit':t['puppet_ticks_after_quiesce']==t['state_ticks_after_quiesce']==0,
          'bounded_diagnostic_capture':not t['diagnostic_overflow'],
          'pinned_context_and_data_through_OS_exit':q['external_handle']=='0x0' and q['guards'] and q['storage_and_script_pinned'] and os_exit['snapshot']['external_handle']=='0x0' and os_exit['snapshot']['guards'] and os_exit['snapshot']['storage_and_script_pinned'],
          'native_final_exit_observed':t['final_exit']==1 and final['noreturn'] and final['utc']<=os_exit['utc']})
        details={'ready':ready,'quiesced':quiesced,'engine':engine,'final':final,'OS_exit':os_exit,
          'owned_destructors':'Not called on this observed native Direct-Quit path. OS process termination is the endpoint; do not force per-level teardown.',
          'minimum_contract':'Unbind once before service teardown, restore control hooks, keep all callback code/storage pinned until process ends; no script hot unload on Direct Quit.'}
    result={'status':'PASS' if all(checks.values()) else 'PENDING','mode':a.mode,'scope':'Bounded native lifecycle; not campaign/network/capacity validation',
      'checks':checks,'details':details,'messages':len(n['messages']),'messages_truncated':n['messages_truncated'],'system_exception_events':len([e for e in by['C_native_exception'] if e.get('type')=='system']),
      'source_sha256':json.loads((run/('load_03_inflight.json' if a.mode=='inflight' else 'load_03_quit.json')).read_text())['structuredContent']['normalized_result']['source_sha256'],
      'final_capture':path.name,'final_capture_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'exit_auxiliary_only':json.loads((run/'exit.json').read_text()),'exit_not_lifecycle_proof':True}
    with (run/'RESULT_SUMMARY.json').open('x',encoding='utf8') as f:json.dump(result,f,indent=2)
    print(json.dumps({'run':run.name,'status':result['status'],'checks':checks,'messages':result['messages']}))

if __name__=='__main__':main()
