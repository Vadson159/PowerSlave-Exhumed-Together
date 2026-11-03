"""Offline review of saved directed-physics captures. No game operations or overwrites."""
import argparse, hashlib, json, math
from pathlib import Path
from prepare import ROOT, OUT, EXPECTED

def main():
    parser=argparse.ArgumentParser();parser.add_argument('run');a=parser.parse_args()
    run=(OUT/a.run).resolve()
    if not run.is_relative_to(OUT.resolve()):raise ValueError('Outside owned Evidence')
    statuses=[]
    for p in run.glob('status_*.json'):
        d=json.loads(p.read_text(encoding='utf-8-sig')).get('structuredContent',{}).get('normalized_result',{})
        if d.get('state')=='detached':statuses.append((p,d))
    path,data=max(statuses,key=lambda pair:pair[0].stat().st_mtime_ns)
    messages=data['messages'];events=[m['payload'] for m in messages if isinstance(m.get('payload'),dict)]
    def find(kind):return [p for p in events if p.get('kind')==kind]
    observed=find('C_directed_physics_observed');cases=observed[-1]['cases'] if observed else []
    names=['p2_wall_portal','p2_into_p1','p2_clear','p1_into_p2','p1_clear','p2_jump_floor','p1_jump_floor']
    case={c['name']:c for c in cases}
    finite=lambda x:isinstance(x,(int,float)) and math.isfinite(x)
    aggregates=bool(cases) and all(finite(c['min_distance']) and all(finite(c[w][f]) for w in ['p1','p2'] for f in ['min_floor_gap','max_floor_gap','min_z','max_z']) for c in cases)
    contacts=[]
    for phase,which,other in [('p2_into_p1','p2','p1'),('p1_into_p2','p1','p2')]:
        c=case.get(phase)
        if not c:continue
        matched=[s for s in find('C_directed_collision_sample') if s['phase']==phase and s['which']==which and s['hit'] and s['opaque_reference']==c['initial'][other]['actor']]
        stable_fields=['player','node','actor','owner','health','origin','sector','live_identity']
        stationary_stable=all(c['initial'][other][f]==c['final'][other][f] for f in stable_fields)
        contacts.append({'phase':phase,'moving':which,'stationary':other,'matching_native_samples':len(matched),
          'stationary_unchanged':stationary_stable,
          'horizontal_clips':c[which]['horizontal_clips'],
          'sample_distances':[math.dist(s['origin'],c['initial'][other]['origin']) for s in matched],
          'minimum_distance':c['min_distance']})
    jump=[]
    for which in ['p2','p1']:
        c=case.get(which+'_jump_floor')
        if not c:continue
        other='p1' if which=='p2' else 'p2'
        samples=[s for s in find('C_directed_jump_input_sample') if s['phase']==c['name']]
        jump.append({'player':which,'counters':[s[which+'_jump_counter'] for s in samples],
          'initial_z':c['initial'][which]['origin'][2],'final_z':c['final'][which]['origin'][2],
          'sample_peak_z':max((s['primary' if which=='p1' else 'secondary']['origin'][2] for s in samples),default=None),
          'native_max_z':c[which].get('max_z'),'floor_gap':{k:c[which][k] for k in ['min_floor_gap','max_floor_gap']},
          'landed':c['final'][which]['origin'][2]==c['initial'][which]['origin'][2] and c['final'][which]['velocity'][2]==0,
          'other_unchanged':c['initial'][other]==c['final'][other]})
    shutdown=find('C_context_native_shutdown_quiesced')
    world=find('C_native_world_exit_returned')
    hook=find('C_hooks_restored')
    faults=[p for p in find('C_native_exception') if p.get('type')!='system']
    script_errors=[m for m in messages if m.get('type')=='error']
    errors=[p for p in events if 'error' in p.get('kind','').lower()]
    checks={
      'complete_capture':not data.get('messages_truncated',True) and data['state']=='detached',
      'seven_bounded_phases':[c['name'] for c in cases]==names and all(c['ticks']==120 for c in cases),
      'finite_physics_aggregates':aggregates,
      'both_native_directed_contacts':len(contacts)==2 and all(c['matching_native_samples']>=1 and c['horizontal_clips']>0 and c['stationary_unchanged'] for c in contacts),
      'p2_sector_and_wall':bool(case.get('p2_wall_portal',{}).get('p2',{}).get('sector_changes')) and bool(case.get('p2_clear',{}).get('p2',{}).get('horizontal_clips')),
      'independent_jump_and_landing':len(jump)==2 and all(j['landed'] and j['other_unchanged'] and j['sample_peak_z']>j['initial_z']+50 and j['counters']==[0,1,2,3,4,0,0,0] for j in jump),
      'both_players_alive_200':bool(observed) and all(observed[-1][w]['live_identity'] and observed[-1][w]['health']==200 for w in ['primary','secondary']),
      'exact_hooks_restored':bool(hook) and hook[-1]['passed'],
      'no_recorded_diagnostic_or_non_system_faults':not(script_errors or errors or faults),
    }
    launch=json.loads((run/'launch.json').read_text());gate=json.loads((run/'ISOLATION_GATE.json').read_text())
    checks['exact_build_isolation']=launch['binary_sha256']==EXPECTED and gate['status']=='PASS' and launch['pid']==data['pid']
    result={'status':'PASS' if all(checks.values()) else 'PENDING','scope':'Directed native physics only; not full M0 acceptance','full_m0':False,
      'run':run.name,'checks':checks,'contacts':contacts,'jumps':jump,
      'source_sha256':json.loads((run/'load_03_physics.json').read_text())['structuredContent']['normalized_result']['source_sha256'],
      'direct_quit_unbind_observed':bool(shutdown) and shutdown[-1]['external_handle']=='0x0' and shutdown[-1]['guards'],
      'native_world_exit':world[-1] if world else None,
      'owned_puppet_destructor_returns':find('C_owned_puppet_actor_destructor_return'),
      'diagnostic_errors':errors,'script_errors':script_errors,'non_system_faults':faults,
      'system_exception_events':len(find('C_native_exception'))-len(faults),
      'capture_messages':len(messages),'exit_auxiliary_only':json.loads((run/'exit.json').read_text()),'exit_not_lifecycle_proof':True,
      'final_capture':path.name,'capture_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
      'limitations':['Opaque reference equality is used only for independently validated known player identities; arbitrary pointer is never dereferenced.',
        'Finite floor/distance aggregates are mandatory; null values remain unaccepted.',
        'Direct-Quit unbind does not itself prove Puppet destructor/free; per-level teardown is recorded separately.',
        'Two fixtures, one map, native synthetic command inputs; not capacity/campaign/network acceptance.']}
    with (run/'RESULT_SUMMARY.json').open('x',encoding='utf8') as f:json.dump(result,f,indent=2)
    print(json.dumps({'run':run.name,'status':result['status'],'checks':checks,'contacts':contacts,'jumps':jump}))

if __name__=='__main__':main()
