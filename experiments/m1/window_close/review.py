"""DOC-48 offline cross-check. Never starts a game, client or test executable."""
from pathlib import Path
import collections, datetime, hashlib, json, sys
LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2]
OUT=ROOT/'docs/evidence/m1/M12B_WINDOW_CLOSE_48';RUN=ROOT/'docs/evidence/m1/M12B_WINDOW_CLOSE_01'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def dump(p,o):p.write_text(json.dumps(o,indent=2,ensure_ascii=False),encoding='utf8')
def main():
    launch,artifact=read(RUN/'launch.json'),read(RUN/'NATIVE_ARTIFACT.json')
    rows=[json.loads(l) for l in (RUN/'native.jsonl').read_text(encoding='utf8').splitlines()]
    clients=[json.loads(l) for l in (RUN/'client.jsonl').read_text(encoding='utf8').splitlines()]
    e=collections.defaultdict(list)
    for r in rows:e[r['event']].append(r)
    capture=read(RUN/'status_final.json')['structuredContent']['normalized_result']
    payload=[m['payload'] for m in capture['messages'] if isinstance(m.get('payload'),dict)]
    os=collections.defaultdict(list)
    for r in payload:os[r.get('kind')].append(r)
    wc=[r for r in payload if r.get('kind','').startswith('wc_')]
    sim=launch['primary_thread_id'];worker=os['wc_socket_io'][0]['tid'];sock=os['wc_socket_bind'][0]['socket']
    resets=[r for r in e['native_call_return'] if r.get('operation')=='exit_reset']
    attempts=[r for r in e['native_call_attempt'] if r.get('operation')=='exit_reset']
    errors=[m for m in capture['messages'] if m['type']=='error']
    faults=[r for r in rows if r['event'] in ['blocked','native_fault','hook_error','hook_create_error']]
    one=lambda name:e[name][0]
    checks={}
    def check(name,result):checks[name]=bool(result)
    check('one_game_run_directory',len(list((ROOT/'docs/evidence/m1').glob('M12B_WINDOW_CLOSE_0*')))==1)
    check('fresh_DLL_exact_frozen_hash',sha(Path(artifact['config']['dll']))==artifact['config']['dll_sha256']=='0e48b179eb955939bafeb8498a5fc2d7230d3d13992ac9423de4a32505680b96')
    manifest=read(OUT/'BUILD_MANIFEST.json')
    build_sources=[r for r in manifest['sources'] if Path(r['path']).suffix in ['.c','.cpp','.h','.hpp'] or Path(r['path']).name=='CMakeLists.txt']
    check('compiled_sources_unchanged',all(sha(ROOT/r['path'])==r['sha256'] for r in build_sources))
    m47=read(ROOT/'docs/evidence/m1/M12B_ADMISSION_47/BUILD_MANIFEST.json')
    product=['tools/enet-prototype/include/thread_bridge_dto.h','tools/enet-prototype/include/enet_worker.h',
        'tools/enet-prototype/src/enet_worker.cpp','tools/enet-prototype/include/enet_host.h','tools/enet-prototype/src/enet_host.cpp',
        'tools/enet-prototype/include/simulation.h','tools/enet-prototype/src/simulation.cpp','experiments/m1/network_input/src/network_input.cpp']
    expected47={r['path'].replace('\\','/'):r['sha256'] for r in m47['sources']}
    patch_proof=[dict(path=p,current_sha256=sha(ROOT/p),DOC47_sha256=expected47[p]) for p in product]
    check('DOC46_DOC47_sources_match_accepted_after',all(r['current_sha256']==r['DOC47_sha256'] for r in patch_proof))
    updates=e['network_native_update'];motion=e['network_native_motion'];spawn=one('independent_spawn')
    check('one_real_P2_spawn',len(e['independent_spawn'])==1 and spawn['p1_actor']!=spawn['p2_actor'] and spawn['p1_player']!=spawn['p2_player'] and spawn['p1_node']!=spawn['p2_node'])
    check('native_simulation_P1_P2_preserved',len(updates)>30 and len(updates)==len(motion) and all(r['tid']==sim and r['commands_this_update']==1 and r['unchanged'] and r['p1_before']==r['p1_after'] for r in updates))
    check('one_update_per_tick',len({r['tick'] for r in updates})==len(updates))
    check('native_Puppet_ticks_and_health',motion[-1]['puppet_ticks']==len(updates) and all(r['p1_health']>0 and r['p2_health']>0 and r['guards'] for r in motion))
    sends={(r['slot'],r['generation'],r['input_seq'],r['forward']) for r in clients if r['event']=='sent'}
    accepted=e['network_input_accepted']
    check('all_accepted_commands_in_external_client',all((r['slot'],r['generation'],r['input_seq'],r['forward']) in sends for r in accepted))
    check('KEX_journal_simulation_thread_only',all(r['tid']==sim for r in rows if r['event'] not in ['dll_prepared','bootstrap_queued']))
    check('worker_distinct_active_ENet',worker!=sim and all(r['tid']==worker and r['socket']==sock for r in os['wc_socket_io']) and sock==os['wc_socket_close_return'][0]['socket'])
    ui=[read(p) for p in RUN.glob('ui_*.json')];close=[r for r in ui if r.get('action')=='close']
    check('exactly_one_owned_WM_CLOSE',len(close)==1 and close[0]['post_close_result'] and close[0]['pid']==launch['pid'] and close[0]['hwnd']==os['wc_dispatch_message'][0]['hwnd'])
    check('WM_CLOSE_SDL_QUIT_dispatch_observed',len(os['wc_dispatch_message'])==1 and any(r['type']==256 for r in os['wc_sdl_push']) and any(r['type']==256 for r in os['wc_sdl_poll']) and any(r['type']==256 for r in os['wc_native_dispatch']))
    check('route_executes_on_simulation_thread',all(r['tid']==sim for r in wc if r['kind'] in ['wc_dispatch_message','wc_sdl_push','wc_sdl_poll','wc_native_dispatch','wc_native_route','wc_game_pre_shutdown_entry','wc_game_pre_shutdown_return','wc_sdl_cleanup']))
    check('quit_engine_platform_route_observed',[r['site'] for r in os['wc_native_route']]==['quit_request','engine_quit'] and len(os['wc_game_pre_shutdown_return'])==1)
    check('live_P2_active_worker_at_shutdown',one('shutdown_entry')['live_external_handle'] and one('network_stop_begin')['running_before'] and not one('shutdown_entry')['state_exit_preceded'])
    wait=os['wc_worker_wait_return'];wcclose=os['wc_socket_close_return']
    check('OS_join_worker_exited',len(wait)==1 and wait[0]['tid']==sim and wait[0]['target_tid']==worker and wait[0]['result']==0 and wait[0]['get_exit_ok'] and wait[0]['exit_code']==0)
    check('OS_socket_close_once_on_worker',len(wcclose)==1 and wcclose[0]['tid']==worker and wcclose[0]['result']==0)
    check('close_precedes_wait_return',os['wc_worker_wait_entry'][0]['order']<wcclose[0]['order']<wait[0]['order'])
    joined=one('network_joined')
    check('network_join_no_loss_or_affinity_fault',not joined['worker_running'] and joined['affinity_violations']==0 and joined['lost_on_stop']==0 and joined['retired_outbound']==0 and joined['resync_reasons']==0)
    check('one_stop_one_retirement',len(e['network_stop_begin'])==len(e['network_joined'])==len(e['network_retired'])==1)
    check('one_native_exit_Reset_safe',len(resets)==len(attempts)==1 and resets[0]['handle_null'] and resets[0]['p1_unchanged'] and resets[0]['guards'])
    check('exact_control_restore',len(e['control_restore'])==4 and all(r['exact16'] for r in e['control_restore']))
    check('native_event_order',one('shutdown_entry')['seq']<one('network_stop_begin')['seq']<joined['seq']<one('network_retired')['seq']<e['control_restore'][0]['seq']<e['control_restore'][-1]['seq']<attempts[0]['seq']<resets[0]['seq']<one('shutdown_quiesced')['seq']<one('engine_shutdown_entry')['seq']<one('engine_shutdown_return')['seq']<one('native_final_exit')['seq'])
    check('OS_wait_before_Reset_and_engine_cleanup',wait[0]['utc']<=resets[0]['utc']<one('engine_shutdown_entry')['utc'])
    check('StateExit_and_owned_dtor_not_forced',not e['state_exit_entry'] and not e['owned_actor_dtor_entry'])
    final=one('native_final_exit')
    check('pinned_safe_final',final['module_pinned'] and final['storage_pinned'] and final['external_handle_null'] and final['guards'] and final['foreign_calls']==0 and final['ticks_after_shutdown']==0 and not final['failed'])
    check('exit0_with_observed_shutdown',read(RUN/'exit.json')['exit_code']==0 and read(RUN/'client_exit.json')['exit_code']==0 and bool(wait) and bool(resets))
    check('capture_complete_no_recorded_errors',capture['state']=='detached' and not capture['messages_truncated'] and not errors and not faults)
    outside=[r for r in os['file_write_open_observed'] if r.get('succeeded') and not Path(r['path']).resolve().is_relative_to(ROOT)]
    guards=os['isolation_write_guard_summary'];guard=guards[-1]
    check('isolation_gate_no_outside_writes',read(RUN/'ISOLATION_GATE.json')['status']=='PASS' and not outside and guard['errors']==0 and guard['unique_overflow']==0)
    check('protected_before_after_unchanged',read(RUN/'protected_before.json')==read(RUN/'protected_after.json'))
    integrity=read(sorted(OUT.glob('INTEGRITY_*.json'))[-1])
    check('final_immutable_original_copy_protected_PASS',integrity['status']=='PASS')
    check('final_OS_no_process_or_udp',read(OUT/'OS_final.json')['processes']==[] and read(OUT/'OS_final.json')['udp']==[])
    regression=read(OUT/'REGRESSION.json')
    check('suites_and_R1_pass',all(r['exit_code']==0 for r in regression if r['name']!='r2_fixed'))
    check('R2_correct_branch_pass',read(OUT/'RUN_r2_fixed_correct_argv/RESULT.json')['exit_code']==0)
    summary=dict(verdict='PASS' if all(checks.values()) else 'PARTIAL',scope='One bounded native WM_CLOSE with active external client; not full M1.2b',
        checks=checks,checks_passed=sum(checks.values()),checks_total=len(checks),pid=launch['pid'],simulation_tid=sim,worker_tid=worker,
        client_pid=read(RUN/'client_process.json')['pid'],socket=sock,native_events=len(rows),native_event_counts={k:len(v) for k,v in e.items() if v},
        native_sequence=[r for r in rows if r['event'] in ['shutdown_entry','network_stop_begin','network_joined','network_retired','control_restore','before_unbind','shutdown_quiesced','engine_shutdown_entry','engine_shutdown_return','native_final_exit'] or r.get('operation')=='exit_reset'],
        OS_sequence=wc,close_request=close,errors=errors,faults=faults,spawn=spawn,last_motion=motion[-1],source_patch_proof=patch_proof,
        compiled_source_records=len(build_sources),client_sends=sum(r['event']=='sent' for r in clients),
        accepted_commands=len(accepted),capture_messages=len(capture['messages']),capture_truncated=capture['messages_truncated'],
        guard_summary=guard,successful_outside_write_opens=outside,integrity=str(sorted(OUT.glob('INTEGRITY_*.json'))[-1].relative_to(ROOT)),
        limitations=['Native fault handler records AV/illegal instruction/stack overflow, not all exceptions; no universal UAF proof.',
          'Final guard summary precedes exit; file-open records continue during shutdown. Diagnostic path guard is not an OS sandbox.',
          'Probe engine_quit returnAddress can be Frida private trampoline after intercepted tail jump; not treated as a new KEX address.',
          'Single neutral-input connection gen1; DOC46/47 R1/R2 adversarial scenarios exercised autonomously, not replayed inside KEX.',
          'Process pin/static ownership proof reused; OS ExitProcess entry marker not required or newly claimed.',
          'P2-2 NOT FIXED. Gemini reports unchanged; no timer/throttle/timeout edits.'])
    dump(RUN/'RESULT_SUMMARY.json',summary)
    dump(OUT/'RESULT_SUMMARY.json',summary)
    dump(OUT/'LIFECYCLE.json',summary['native_sequence'])
    dump(OUT/'OS_SEQUENCE.json',wc)
    # Final source manifest separates build inputs from post-build diagnostics.
    records=[]
    for p in sorted(LAB.glob('*')):
        if not p.is_file():continue
        dest=OUT/'final_harness'/p.name;dest.parent.mkdir(exist_ok=True);dest.write_bytes(p.read_bytes())
        records.append(dict(path=str(p.relative_to(ROOT)),sha256=sha(p)))
    dump(OUT/'HARNESS_MANIFEST.json',dict(files=records,build_inputs_unchanged=checks['compiled_sources_unchanged']))
    print(json.dumps(dict(verdict=summary['verdict'],passed=summary['checks_passed'],total=summary['checks_total'],failed=[k for k,v in checks.items() if not v])))
    assert all(checks.values())
if __name__=='__main__':main()
