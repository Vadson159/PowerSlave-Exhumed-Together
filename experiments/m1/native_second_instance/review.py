"""Review retained actual DOC51 evidence, never substitutes fixtures for KEX.

Does not launch, instrument, replay input or repair processes. No interprocess
clock subtraction. Dispatch, successful OS UDP return, Session acceptance and
native application are distinct sets joined by slot/generation/input_seq.
"""
from pathlib import Path
import collections,hashlib,json,math
ROOT=Path(__file__).resolve().parents[3];OUT=ROOT/'docs/evidence/m1/NATIVE_SECOND_INSTANCE_51'
H=ROOT/'docs/evidence/m1/N51_HOST_01';C=ROOT/'docs/evidence/m1/N51_CLIENT_MAIN_01'
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def lines(p):return [json.loads(s) for s in p.read_text(encoding='utf8').splitlines()]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def key(x):return x['slot'],x['generation'],x['input_seq']
def events(rows,name):return [r for r in rows if r['event']==name]
def norm(run,name):return load(run/name)['structuredContent']['normalized_result']
def osrows(n):return [m['payload'] for m in n['messages'] if isinstance(m.get('payload'),dict)]
def rate(rows,field):return (len(rows)-1)*1e9/(rows[-1][field]-rows[0][field]) if len(rows)>1 else 0
def main():
    host,client=lines(H/'native.jsonl'),lines(C/'native.jsonl');criteria={}
    def check(n,v):criteria[n]=bool(v)
    hl,cl=load(H/'launch.json'),load(C/'launch.json')
    ha,ca=load(H/'NATIVE_ARTIFACT.json'),load(C/'NATIVE_ARTIFACT.json')
    hm,cm=load(OUT/'BUILD_MANIFEST.json'),load(OUT/'BUILD_MANIFEST_MAIN.json')
    live=load(OUT/'OS_live_active_host.json');osids={r['ProcessId']:r for r in live['processes']}
    check('two_real_os_kex_processes',hl['pid']!=cl['pid'] and all(x['pid'] in osids and Path(osids[x['pid']]['ExecutablePath']).resolve()==Path(x['target']).resolve() for x in [hl,cl]))
    check('independent_copies_and_profiles',hl['target']!=cl['target'] and hl['cwd']!=cl['cwd'] and hl['profile']!=cl['profile'] and load(OUT/'CLIENT_COPY.json')['independent'])
    check('loaded_exact_frozen_dlls',all(sha(Path(a['config']['dll']))==a['config']['dll_sha256'] for a in [ha,ca]) and ha['build_manifest_sha256']==sha(OUT/'BUILD_MANIFEST.json') and ca['build_manifest_sha256']==sha(OUT/'BUILD_MANIFEST_MAIN.json'))
    def shared(m):return {r['path']:r['sha256'] for r in m['sources'] if r['path'].startswith(('tools/enet-prototype/','experiments/m1/network_input/','experiments/m1/native_bootstrap/')) and Path(r['path']).suffix in ['.c','.cpp','.h','.hpp','.txt']}
    check('coherent_shared_host_client_sources',shared(hm)==shared(cm))
    check('isolation_gates',all(load(p/'ISOLATION_GATE.json')['status']=='PASS' for p in [H,C]))
    check('handshake_input_domain_only',events(client,'client_welcome') and events(client,'client_input_domain_ready') and all(r['slot']==1 and r['generation']==1 and not r['world_synchronized'] for r in events(client,'client_welcome')+events(client,'client_input_domain_ready')))
    samples={r['sample_ordinal']:r for r in events(client,'client_input_sampled')}
    dispatch=events(client,'client_input_dispatched');sent={key(r):r for r in dispatch}
    prev={};sequence_ok=True
    for d in dispatch:
        ident=d['slot'],d['generation'];old=prev.get(ident)
        sequence_ok &= d['input_seq']<=10800 if old is None else old<d['input_seq'] and d['input_seq']-old<=1800
        prev[ident]=d['input_seq']
    check('doc50_per_wire_generation_sequence',sequence_ok and len(sent)==len(dispatch))
    matched=[]
    for d in dispatch:
        s=samples.get(d['sample_ordinal'])
        if d['fresh']:
            matched.append(s is not None and s['bridge_queued'] and s['valid'] and s['pid']==cl['pid'] and s['tid']==cl['primary_thread_id'] and s['sampled_ns']==d['sampled_ns'] and s['tick']==d['sample_tick'] and s['forward']==d['forward'] and d['slot']==s['slot'] and d['generation']==s['generation'] and 0<=d['generated_ns']-s['sampled_ns']<=100000000)
        else:matched.append(d['forward']==0)
    check('real_kex_samples_bridge_dispatch',all(matched) and any(d['forward']==1 for d in dispatch) and any(d['forward']==-1 for d in dispatch))
    cn=norm(C,'STATUS_FINAL.json');hn=norm(H,'STATUS_EXIT.json');co,ho=osrows(cn),osrows(hn)
    udp=[p for p in co if p.get('kind')=='n51_udp_input_send_return' and p['result']==0 and not p['overlapped'] and p['bytes_sent']>0]
    wire={key(p):p for p in udp}
    check('udp_is_independent_from_enqueue',bool(udp) and all(key(p) in sent and sent[key(p)]['forward']==p['forward'] and p['pid']==cl['pid'] and p['tid'] in {d['tid'] for d in dispatch} for p in udp))
    accepted=events(host,'network_input_accepted');accept={key(r):r for r in accepted}
    check('host_acceptance_correlates_actual_udp',bool(accepted) and all(key(r) in wire and key(r) in sent and r['forward']==wire[key(r)]['forward']==sent[key(r)]['forward'] for r in accepted))
    updates=events(host,'network_native_update');motion=events(host,'network_native_motion');spawn=events(host,'independent_spawn')
    check('separate_native_p2',len(spawn)==1 and spawn[0]['p1_actor']!=spawn[0]['p2_actor'] and spawn[0]['p1_player']!=spawn[0]['p2_player'] and spawn[0]['p1_node']!=spawn[0]['p2_node'] and spawn[0]['guards'])
    check('native_application_identity',bool(updates) and all(key(u) in accept and u['commands_this_update']==1 and u['tid']==hl['primary_thread_id'] for u in updates) and len({u['tick'] for u in updates})==len(updates))
    check('p1_preserved_every_p2_update',bool(updates) and all(u['unchanged'] and u['p1_before']==u['p1_after'] for u in updates))
    check('native_motion_and_guards',len(motion)==len(updates) and all(m['guards'] and m['p1_health']==m['p2_health']==200 and all(math.isfinite(v) for v in m['before']+m['after']) for m in motion))
    segments=[]
    for m in motion:
        if not segments or segments[-1]['buttons']!=m['native_buttons']:segments.append(dict(buttons=m['native_buttons'],rows=[]))
        segments[-1]['rows'].append(m)
    summaries=[]
    for s in segments:
        r=s['rows'];steps=[math.dist(x['before'][:2],x['after'][:2]) for x in r]
        summaries.append(dict(buttons=s['buttons'],count=len(r),first_tick=r[0]['tick'],last_tick=r[-1]['tick'],first_input=r[0]['input_seq'],last_input=r[-1]['input_seq'],xy_path=sum(steps),xy_net=math.dist(r[0]['before'][:2],r[-1]['after'][:2]),first_step=steps[0],last_step=steps[-1],tail_max_step=max(steps[-30:]),first_position=r[0]['before'],last_position=r[-1]['after']))
    holds=[s for s in summaries if s['buttons'] in [4,8]]
    check('w_and_s_cause_native_movement',all(any(s['buttons']==b and s['count']>=30 and s['xy_net']>10 for s in holds) for b in [4,8]))
    releases=[b for a,b in zip(summaries,summaries[1:]) if a['buttons'] in [4,8] and b['buttons']==0 and accept[(1,1,b['first_input'])]['forward']==0]
    check('release_neutral_and_physics_rest',len(releases)==2 and all(s['count']>=60 and s['tail_max_step']<.001 for s in releases))
    check('p1_position_and_health_preserved',spawn and events(host,'before_unbind') and spawn[0]['p1_pos']==events(host,'before_unbind')[0]['p1_pos'] and events(host,'before_unbind')[0]['p1_health']==200)
    def joined_os(rows,worker_tid,sim_tid):
        waits=[r for r in rows if r.get('kind')=='n51_thread_wait_return' and r['target_tid']==worker_tid and r['tid']==sim_tid and r['result']==0 and r['exit_code']==0]
        closes=[r for r in rows if r.get('kind')=='n51_socket_close_return' and r['tid']==worker_tid and r['result']==0]
        return bool(waits and closes and closes[0]['order']<waits[-1]['order'])
    cworker={d['tid'] for d in dispatch};hbind=[r for r in ho if r.get('kind')=='n51_socket_bind' and r['port']==28745 and r['result']==0]
    # Existing startHost binds on its starting SIMULATION caller before the
    # background loop owns service/teardown. Identify the worker by closure of
    # that exact bound socket, not by the startup bind thread.
    hsockets={r['socket'] for r in hbind}
    hclose=[r for r in ho if r.get('kind')=='n51_socket_close_return' and r['socket'] in hsockets and r['result']==0]
    hworker=hclose[0]['tid'] if hclose else 0
    check('os_worker_join_and_socket_close_both',len(cworker)==1 and hworker and joined_os(co,next(iter(cworker)),cl['primary_thread_id']) and joined_os(ho,hworker,hl['primary_thread_id']))
    cj=events(client,'client_worker_joined');hj=events(host,'network_joined');reset=[r for r in host if r['event']=='native_call_return' and r.get('operation')=='exit_reset']
    check('stop_join_before_native_cleanup',len(cj)==len(hj)==len(reset)==1 and cj[0]['record']<events(client,'engine_shutdown_entry')[0]['record'] and hj[0]['seq']<reset[0]['seq']<events(host,'engine_shutdown_entry')[0]['seq'] and reset[0]['handle_null'] and reset[0]['p1_unchanged'])
    check('no_registered_native_or_affinity_faults',not any(r['event'] in ['blocked','native_fault','hook_error','hook_create_error'] for r in host+client) and not cj[0]['affinity_violations'] and not hj[0]['affinity_violations'])
    check('complete_provider_observation',not cn['messages_truncated'] and not hn['messages_truncated'] and not any(m['type']=='error' for n in [cn,hn] for m in n['messages']))
    check('client_normal_exit',load(C/'exit.json')['exit_code']==0 and cn['state']=='detached' and events(client,'client_native_final_exit')[0]['quiesced'])
    terminal=load(OUT/'OS_TERMINAL_STATE.json');cleanup=load(OUT/'OS_CLEANUP_ATTEMPT.json')
    check('host_normal_os_exit',terminal['zero_timeout_process_wait']==0 and not cleanup['terminate_result'])
    final=load(OUT/'OS_final_after_cleanup.json');check('all_processes_endpoints_cleaned',not final['processes'] and not final['udp'])
    integrity=load(sorted(OUT.glob('INTEGRITY_*.json'))[-1]);check('original_profiles_and_history_unchanged',integrity['status']=='PASS' and all(load(p/'protected_before.json')==load(p/'protected_after.json') for p in [H,C,ROOT/'docs/evidence/m1/N51_CLIENT_01']))
    check('autonomous_regressions',len(load(OUT/'REGRESSIONS.json'))==12 and all(r['exit_code']==0 for r in load(OUT/'REGRESSIONS.json')))
    activation=[load(p) for p in H.glob('ui_*.json') if load(p).get('action')=='activate']
    summary=dict(verdict='PASS' if all(criteria.values()) else 'PARTIAL',criteria=criteria,failed=[k for k,v in criteria.items() if not v],pids=dict(host=hl['pid'],client=cl['pid']),simulation_tids=dict(host=hl['primary_thread_id'],client=cl['primary_thread_id']),worker_tids=dict(host=hworker,client=next(iter(cworker))),counts=dict(client_samples=len(samples),dispatches=len(dispatch),successful_udp_inputs=len(udp),host_accepts=len(accepted),native_updates=len(updates),native_motion=len(motion)),sequence=dict(first=dispatch[0]['input_seq'],last=dispatch[-1]['input_seq'],generations=sorted({d['generation'] for d in dispatch})),client_dispatch_hz=rate(dispatch,'generated_ns'),segments=summaries,releases=releases,synthetic_input=load(C/'WINDOW_INPUT_ACTIVE_HOST.json'),host_activation=activation,host_terminal_state=terminal,os_cleanup=cleanup,scope='Bounded real two-process INPUT chain, host WINDOW_ACTIVATE fixture required. No world replication. Host normal OS shutdown FAILED; external OS cleanup used. No cross-process latency computed.')
    dest=OUT/'RESULT_SUMMARY_FINAL.json'
    assert not dest.exists();dest.write_text(json.dumps(summary,indent=2),encoding='utf8')
    print(json.dumps({k:v for k,v in summary.items() if k in ['verdict','failed','pids','simulation_tids','worker_tids','counts','sequence','client_dispatch_hz']}))
    print('SEGMENTS',json.dumps(summaries))
if __name__=='__main__':main()
