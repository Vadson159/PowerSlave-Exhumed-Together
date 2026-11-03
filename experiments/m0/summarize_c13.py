"""Read retained C13 evidence only. Never attach, run the game, or accept full M0."""
from pathlib import Path
import datetime, hashlib, json
from prepare import ROOT, OUT, EXPECTED

run=OUT/'C13'
raw=run/'status_05_post_audit.json'
evidence=json.loads(raw.read_text(encoding='utf8'))['structuredContent']
capture=evidence['normalized_result']
messages=capture['messages']
payloads=[m['payload'] for m in messages if isinstance(m.get('payload'),dict)]
def events(kind):return [p for p in payloads if p.get('kind')==kind]
def one(kind):
    values=events(kind)
    if len(values)!=1:raise ValueError(f'{kind}: expected one retained event, got {len(values)}')
    return values[0]

launch=json.loads((run/'launch.json').read_text(encoding='utf8'))
load=json.loads((run/'load_death.json').read_text(encoding='utf8'))['structuredContent']
source=run/'generated_death.js'
source_sha=hashlib.sha256(source.read_bytes()).hexdigest()
cfg=json.loads(source.read_text(encoding='utf8').splitlines()[0][len('const cfg = '):-1])
death=one('C_secondary_native_death')
isolated=one('C_secondary_death_isolation_passed')
recreated=one('C_same_context_recreated_alive')
complete=one('C_death_recreate_bounded_passed')
before=death['before'];after=death['after'];primary=before['primary']
graph_keys=['player','node','actor','owner','health']
same_p1=lambda p:all(p[k]==primary[k] for k in graph_keys)
errors=[m for m in messages if m.get('type')=='error']
errors += [p for p in payloads if p.get('kind') in ['C_error','C_cleanup_error','C_native_exception','C_enemy_experiment_error','C_hook_cleanup_error']]
checks={
 'same_target_build':launch['binary_sha256']==cfg['binary_sha256']==EXPECTED,
 'same_pid':launch['pid']==cfg['pid']==capture['pid'],
 'executed_source_digest_matches':source_sha==load['normalized_result']['source_sha256'],
 'retained_buffer_not_truncated':capture['messages_truncated'] is False,
 'controlled_native_death':death['direct_native_damage'] and death['amount']==before['secondary']['health'] and after['health']==0 and bool(after['puppet_flags']&0x200),
 'p1_and_gi_preserved_at_death':same_p1(death['primary_after']) and before['gi']==death['gi_after']=={'state':3,'pending':0},
 'dead_p2_isolation':isolated['primary_ticks']>=90 and isolated['dead_skips']>=90 and isolated['secondary_puppet_ticks']>=90 and isolated['secondary_native_updates']==0 and isolated['death_timer']==0 and same_p1(isolated['primary']),
 'same_context_recreated_alive':recreated['secondary']['player']==complete['same_player_context']==after['player'] and recreated['secondary']['health']>0 and not recreated['secondary']['puppet_flags']&0x200 and recreated['secondary']['death_timer']==0 and recreated['distance']>1 and same_p1(recreated['primary']),
 'two_zero_node_retirements':len(complete['completed_cycles'])==2 and all(c['secondary_node']['payload']=='0x0' and c['secondary_node']['refs']==0 and c['guards'] and same_p1(c['primary']) for c in complete['completed_cycles']),
 'two_original_hook_restorations':len(events('C_hooks_restored'))==2 and all(p['passed'] for p in events('C_hooks_restored')),
 'no_recorded_script_or_probe_error':not errors,
}
exit_record=json.loads((run/'exit.json').read_text(encoding='utf8')) if (run/'exit.json').exists() else None
protected=json.loads((run/'protected_before.json').read_text())==json.loads((run/'protected_after.json').read_text()) if (run/'protected_after.json').exists() else None
result={
 'generated_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
 'status':'BOUNDED_RUNTIME_OBSERVED' if all(checks.values()) else 'REVIEW_REQUIRED',
 'binary_sha256':EXPECTED,'pid':launch['pid'],'simulation_tid':cfg['simulation_tid'],
 'source_sha256':source_sha,'source_snapshot':cfg.get('probe_source_snapshot'),
 'evidence':{'path':str(raw.relative_to(ROOT)),'evidence_id':evidence['evidence_id'],'sha256':hashlib.sha256(raw.read_bytes()).hexdigest(),'message_count':len(messages)},
 'checks':checks,'death':death,'isolation':isolated,'recreated':recreated,'completed':complete,
 'exit':exit_record,'final_frida_state':capture['state'],'shutdown_method':'UNKNOWN: no owned WM_CLOSE or native shutdown trace retained',
 'protected_before_after_equal':protected,'m0_accepted':False,
 'not_tested':['P1 death without global restart','P2 death from real AI/projectile and incoming holders','E-triggered respawn with pending old Puppet','verified level-start origin, camera/viewZ and starter machete/pistol/ammo','natural basic projectile shooter-node release','directed wall/floor/portal/mutual collision acceptance','level unload with live P2','explicit gameplay-script unload/context deallocation in C13'],
 'limitations':['Native damage was an explicit controlled call using P1 as source, not a weapon/AI hit.','Zero-node observations occurred in a holder-free fixture and are not a requirement for immediate destruction with live holders.','Exit0/detached are observed; shutdown cause/sequence and general heap/owner lifetime are not established.','Recorded messages are selective instrumentation, not a complete native call trace.'],
}
with (run/'RESULT_SUMMARY.json').open('x',encoding='utf8') as f:json.dump(result,f,indent=2)
print(json.dumps({'status':result['status'],'checks':checks,'exit':exit_record,'final_frida_state':capture['state'],'m0_accepted':False}))
