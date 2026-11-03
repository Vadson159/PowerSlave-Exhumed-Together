"""Offline consistency review of the single stopped DOC57 run. Never runs KEX."""
from pathlib import Path
import json,sys
from workflow import ROOT,LAB,OUT,load,dump,sha,stamp

def main():
    run=OUT/'N57_STEP0_01';launch=load(run/'launch.json')
    native=[json.loads(s) for s in (run/'native.jsonl').read_text(encoding='utf8').splitlines()]
    scout=[json.loads(s) for s in (run/'scout.jsonl').read_text(encoding='utf8').splitlines()]
    bundle=load(OUT/'REA_05_FIXTURES.json')
    statuses=[r for r in bundle['records'] if r['operation']=='frida_session_status' and r.get('parameters',{}).get('session_id')=='87dbacd6-0392-497c-b78b-d0f1c8aab5b1']
    status=max(statuses,key=lambda r:len(r['normalized_result'].get('messages',[])))['normalized_result']
    payloads=[m['payload'] for m in status['messages'] if isinstance(m.get('payload'),dict)]
    errors=[m for m in status['messages'] if m.get('type')=='error']
    guards=[p for p in payloads if p.get('kind')=='isolation_write_guard_summary']
    guard=max(guards,key=lambda p:p.get('write_opens',0),default={})
    witness=load(OUT/'FAILURE_READONLY_WITNESS.json')['normalized_result']['messages'][0]['payload']
    pre=next(r for r in native if r['event']=='spawn_preflight')
    blocked=[r for r in native if r['event']=='blocked']
    extra=[a for a in witness['actors'] if a['vtable']=='0x140528608' and a['pos']==[-7420.8037109375,-6848.013671875,1152]]
    same={k:pre['primary'][k]==witness['primary'][k] for k in ['player','node','actor','owner','own_node','vtable','health','sector','pos']}
    os=load(run/'OS_SIGNALED.json');handles=load(run/'OS_HANDLE_IDENTITY.json')
    regressions=load(sorted(OUT.glob('REGRESSIONS_*.json'))[-1])
    fixtures=load(OUT/'TOOL_FIXTURES.json')
    top=fixtures['top_level']['normalized_result']['messages'][0]['payload']
    nested=fixtures['nested']['normalized_result']['messages'][0]['payload']
    checks=dict(one_spawn_preflight=sum(r['event']=='spawn_preflight' for r in native)==1,one_block=len(blocked)==1,
       one_extra_native_actor=len(extra)==1 and witness['sector259_count']==2,extra_flags_fullbright=extra[0]['flags']==64 if extra else False,
       extra_refs0=extra[0]['refs']==0 if extra else False,p1_observed_graph_health_position_preserved=all(same.values()),
       valid_sector_planes=pre['geometry']['inside'],no_lifecycle_success_markers=not any(r['event'] in ['spawned','stable','remove_request','deferred_retired'] for r in native),
       normal_os_exit=os['exit_code']==0 and os['wait']==0 and os['duplicate_wait']==0 and os['no_emergency_cleanup'] and handles['compare_same_kernel_object'],
       no_provider_script_error=not errors,no_capture_truncation=not status.get('messages_truncated'),regressions_15_exit0=len(regressions)==15 and all(r['exit_code']==0 for r in regressions),
       nested_hook_suppression_reproduced=top['default_hook_calls']==1 and nested['nested']['default_hook_calls']==0 and nested['nested']['all_hook_calls']==0)
    assert all(checks.values()),checks
    static=load(OUT/'STATIC_AUDIT.json')
    coords=[{**r,'runtime_va':hex(int(r['rva'],16)+int(witness['image_base'],16))} for r in static['sites']]
    dump(OUT/'RUNTIME_COORDINATES.json',dict(image_base=witness['image_base'],pid=launch['pid'],simulation_tid=launch['primary_thread_id'],sites=coords,note='Runtime VA derived from measured module base plus statically verified RVA; not proof every function executed'))
    metrics=dict(verdict='BLOCKED',scope='DOC57 native visual proxy Step 0; one stopped KEX process',pid=launch['pid'],simulation_tid=launch['primary_thread_id'],
       executable_sha256=launch['binary_sha256'],image_base=witness['image_base'],checks=checks,spawn_preflight=pre,
       native_spawn_calls_requested=1,observed_factory_entry_callbacks=0,logged_spawn_success=0,extra_native_actors_observed=extra,
       verified_actor_tick_samples=0,actual_actor_lifetime_ticks=None,visual_pass=False,visual_status='NOT DEMONSTRATED',
       remove_calls_requested=0,deferred_retirement='UNKNOWN / NOT TESTED',destructor_completion='UNKNOWN / NOT OBSERVED',
       p1_compared_fields=same,p1_control_movement='NOT TESTED after spawn',p1_inventory_after_spawn='UNKNOWN; failure witness did not capture it',
       blocked=blocked,factory_return_value='NOT RECORDED: probe rejected missing factoryWitness before retaining/logging result',
       root_cause='Nested Frida Interceptor listener coverage hole reproduced in benign Python/CModule fixture; this explains missing observer callback. It does not prove visibility or lifecycle acceptance.',
       fixture_top_level=top,fixture_nested=nested,guard_last_summary=guard,provider_script_errors=errors,messages=len(status['messages']),messages_truncated=status.get('messages_truncated'),
       os_exit=os,regressions=regressions,before_frame='N57_STEP0_01/window_20261103_094235_233518.png',after_spawn_frame='N57_STEP0_01/window_20261103_094304_434253.png',
       new_native_dll=None,engine_object_writes='Only normal native SpawnActor; native Remove was never called. No direct game-field writes.',
       doc58_ready=False,next_gate='Repair DOC57 observation/placement in a new separately authorized bounded native run; no network implementation')
    dump(OUT/'RESULT_SUMMARY.json',metrics)
    commands=[]
    for r in regressions:
        text=(ROOT/r['log']).read_text(encoding='utf8',errors='replace')
        commands.append(dict(name=Path(r['argv'][0]).stem,exit_code=r['exit_code'],tail=text.splitlines()[-12:]))
    dump(OUT/'REGRESSION_SUMMARIES.json',commands)
    print(json.dumps(dict(verdict=metrics['verdict'],checks=checks,extra_actors=len(extra),guard=guard,regression_summaries=commands),ensure_ascii=False,indent=2))
if __name__=='__main__':main()
