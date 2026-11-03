"""File-only DOC53 review of the one completed scenario. No game execution."""
from pathlib import Path
import json,math,struct
from audit import ROOT,LAB,OUT,dump,sha,game_check

E=ROOT/'docs/evidence/m1'
H=E/'N53_HOST_MAIN_01';C=E/'N53_CLIENT_MAIN_01'
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def rows(p):return [json.loads(x) for x in p.read_text(encoding='utf8').splitlines()]
def norm(o):return o.get('structuredContent',o).get('normalized_result',o.get('structuredContent',o))
def payloads(o):return [x['payload'] for x in norm(o)['messages'] if isinstance(x.get('payload'),dict)]
def event(a,name):return [r for r in a if r['event']==name]
def main():
    h=rows(H/'native.jsonl');c=rows(C/'native.jsonl')
    hs=load(H/'STATUS_EXIT.json');cs=load(C/'STATUS_EXIT_RETAINED.json')
    hp,cp=payloads(hs),payloads(cs)
    hl,cl=load(H/'launch.json'),load(C/'launch.json')
    updates=event(h,'network_native_update');motion=event(h,'network_native_motion');accepted=event(h,'network_input_accepted');dispatched=event(c,'client_input_dispatched')
    udp=[p for p in cp if p.get('kind')=='n51_udp_input_send_return' and p['result']==0 and not p['overlapped']]
    identity=lambda r:(r['slot'],r['generation'],r['input_seq'])
    sends={identity(r):r for r in dispatched};os_sends={identity(r):r for r in udp};accepts={identity(r):r for r in accepted}
    checks={}
    def check(name,ok):checks[name]=bool(ok)
    check('two_independent_KEX_paths',hl['pid']!=cl['pid'] and hl['target']!=cl['target'] and hl['profile']!=cl['profile'])
    check('one_native_spawn',len(event(h,'independent_spawn'))==1)
    check('all_accepts_correlate_dispatch_and_actual_udp',all(identity(r) in sends and identity(r) in os_sends and r['forward']==sends[identity(r)]['forward']==os_sends[identity(r)]['forward'] for r in accepted))
    check('unique_updates_and_motion_per_tick',len(updates)==len({r['tick'] for r in updates})==len(motion)==len({r['tick'] for r in motion}))
    check('native_KEX_caller_every_primary_application',all(r['primary_caller_rva']==0x1b7018 and r['commands_this_update']==1 for r in updates))
    check('every_native_application_has_accepted_identity',all(identity(r) in accepts for r in updates))
    check('all_P1_deep_comparisons_equal',all(r['unchanged'] and r['p1_before']==r['p1_after'] for r in updates))
    bg=[r for r in updates if r['focus_override']]
    check('background_real_client_foreground_and_P1_neutral',len(bg)>60 and all(r['foreground_pid']==cl['pid'] and r['p1_neutral'] and not any(r['p1_before']['input']) for r in bg))
    check('finite_native_positions_and_guards',all(r['guards'] and all(math.isfinite(v) for v in r['before']+r['after']) for r in motion))
    begin=event(h,'background_session_begin')[0];spawn=event(h,'independent_spawn')[0]
    check('READY_starts_background_before_P2_exists',not begin['p2_exists'] and begin['seq']<spawn['seq'] and begin['foreground_pid']==cl['pid'])
    check('focus_byte_restored_each_logged_background_tick',all(r['focus_restored']==1 for r in event(h,'background_tick')))
    sp=load(OUT/'SINGLEPLAYER_FOCUS_BASELINE.json')
    check('singleplayer_focus_pause_preserved',len(sp)>2 and len({r['local_updates'] for r in sp})==1 and all(r['focus_byte_after']==1 and not r['network_running'] and not r['override_this_tick'] and r['local_this_tick']==0 for r in sp))
    policies=event(h,'background_policy_status')
    # Screenshot and key journal define the Esc interval; +20c is NOT Esc.
    menu=[r for r in policies if '2026-11-02T20:01:04'<r['utc']<'2026-11-02T20:02:27']
    check('manual_Esc_pause_preserved_foreground_and_background',len(menu)>4 and len({r['local_updates'] for r in menu})==1 and all(r['local_this_tick']==0 for r in menu) and any(r['foreground_pid']==hl['pid'] for r in menu) and any(r['foreground_pid']==cl['pid'] for r in menu))
    normal=[r for r in updates if not r['focus_override'] and r['utc']>'2026-11-02T20:00:20' and r['p1_before']['input'][3]>0]
    check('ordinary_P1_W_returns_after_real_focus_gain',len(normal)>5)
    check('held_P1_S_positive_before_second_loss',any(not r['focus_override'] and r['utc']>'2026-11-02T20:03:01' and r['p1_before']['input'][4]>0 for r in updates))
    segments=[]
    for r in motion:
        if not segments or segments[-1]['buttons']!=r['native_buttons']:segments.append(dict(buttons=r['native_buttons'],records=[]))
        segments[-1]['records'].append(r)
    selected=[]
    for s in segments:
        a=s['records'];selected.append(dict(buttons=s['buttons'],updates=len(a),first_tick=a[0]['tick'],last_tick=a[-1]['tick'],first_input_seq=a[0]['input_seq'],last_input_seq=a[-1]['input_seq'],net_xy=math.dist(a[0]['before'][:2],a[-1]['after'][:2]),path_xy=sum(math.dist(x['before'][:2],x['after'][:2]) for x in a),last_30_max_step=max(math.dist(x['before'][:2],x['after'][:2]) for x in a[-30:])) )
    for button in [4,8]:
        indices=[i for i,s in enumerate(selected) if s['buttons']==button]
        check('movement_'+str(button),bool(indices) and all(selected[i]['path_xy']>0 for i in indices))
        check('release_and_neutral_after_'+str(button),bool(indices) and all(i+1<len(selected) and selected[i+1]['buttons']==0 and selected[i+1]['last_30_max_step']==0 for i in indices))
    shutdown=event(h,'shutdown_entry')[0]
    check('no_override_or_update_after_shutdown_entry',not any(r['seq']>shutdown['seq'] for r in updates+event(h,'background_session_begin')+event(h,'background_tick')))
    returns=event(h,'native_call_return');reset=[r for r in returns if r['operation']=='exit_reset']
    controls=event(h,'control_restore');joined=event(h,'network_joined')[0]
    check('ordered_join_controls_one_Reset_engine_cleanup',len(controls)==4 and all(r['exact16'] for r in controls) and len(reset)==1 and reset[0]['handle_null'] and reset[0]['p1_unchanged'] and joined['seq']<min(r['seq'] for r in controls)<reset[0]['seq']<event(h,'engine_shutdown_entry')[0]['seq'])
    check('native_updates_commands_actual_Puppet_ticks_match',event(h,'before_unbind')[0]['p2_updates']==len(updates)==event(h,'before_unbind')[0]['p2_commands']==event(h,'before_unbind')[0]['p2_puppet_ticks'])
    workers={}
    for name,pp,tid in [('host',hp,hl['primary_thread_id']),('client',cp,cl['primary_thread_id'])]:
        closes=[x for x in pp if x.get('kind')=='n51_socket_close_return' and x['result']==0 and x['tid']!=tid]
        matched=[(x,y) for x in closes for y in pp if y.get('kind')=='n51_thread_wait_return' and y['target_tid']==x['tid'] and y['tid']==tid and y['result']==0 and y['exit_code']==0]
        check(name+'_actual_socket_close_and_worker_wait',bool(matched));workers[name]=matched
    for name,run in [('host',H),('client',C)]:
        sig=load(run/'OS_SIGNALED.json');ident=load(run/'OS_HANDLE_IDENTITY.json')
        check(name+'_same_process_object_signaled_exit0',sig['wait']==0 and sig['duplicate_wait']==0 and sig['exit_code']==0 and sig['identity']['handle']==ident['primary']['handle'] and sig['identity']['creation_filetime']==ident['primary']['creation_filetime'] and sig['no_emergency_cleanup'])
        check(name+'_launcher_normal_exit0',load(run/'exit.json')['exit_code']==0)
        check(name+'_protected_profiles_before_after_equal',load(run/'protected_before.json')==load(run/'protected_after.json'))
    faults=[r for r in h+c if r['event'] in ['blocked','native_fault','hook_error','hook_create_error']]
    check('registered_native_faults_and_blocks_zero',not faults)
    for name,status,pp in [('host',hs,hp),('client',cs,cp)]:
        n=norm(status);guard=max((p for p in pp if p.get('kind')=='isolation_write_guard_summary'),key=lambda p:p['write_opens'])
        check(name+'_full_provider_capture',not n['messages_truncated'] and not any(x.get('type')=='error' for x in n['messages']))
        check(name+'_guard_no_errors_overflow',guard['errors']==0 and guard['unique_overflow']==0)
    ui=[load(p) for p in OUT.glob('UI_*.json')]
    check('all_recorded_UI_fixtures_send_no_synthetic_focus_messages',all(x.get('synthetic_focus_messages')==0 for x in ui))
    g=game_check();check('original_copies_protected_unchanged',not g['errors'] and not g['client_copy_errors'] and g['original_full_snapshot_equal'])
    summary=dict(overall='PARTIAL',background_chain='PASS' if all(checks.values()) else 'FAIL',checks=checks,checks_passed=sum(checks.values()),checks_total=len(checks),host=hl,client=cl,counts=dict(accepted=len(accepted),dispatched=len(dispatched),actual_udp=len(udp),native_updates=len(updates),background_native_updates=len(bg),foreground_native_updates=len(updates)-len(bg),provider_messages_host=len(norm(hs)['messages']),provider_messages_client=len(norm(cs)['messages'])),segments=selected,workers=workers,spawn=spawn,background_first=begin,shutdown=dict(host=load(H/'OS_SIGNALED.json'),client=load(C/'OS_SIGNALED.json')),unknowns=['Mouse Attack fixture did not produce a positive native held Attack counter; posted button down is not sufficient proof.', 'No positive nonzero mouse-delta-on-focus-loss fixture.', 'Exact native Esc skip gate among preserved original gates not separately instrumented; pause/resume behavior observed.', 'Native loading/cutscene/modal/WorldExit/reconnect/minimized/8-player scenarios not newly executed.', 'Pristine default-timer suite44/46; original assertions/deadlines with scoped1ms timer46/46. Single comparison does not establish a universal cause.', 'DOC52 historical terminal hang root cause remains UNKNOWN.'],game=g)
    dump(OUT/'RESULT_SUMMARY.json',summary)
    dump(OUT/'NATIVE_SELECTED.json',dict(focus=event(h,'native_focus_decision'),lease_begin=event(h,'background_session_begin'),lease_return=event(h,'background_focus_return'),bindings_reset=event(h,'local_bindings_reset'),policies=policies,background_ticks=event(h,'background_tick'),menu_observed_interval=menu,shutdown=[r for r in h if r['seq']>=shutdown['seq']],p1_return_input_samples=normal[:3]))
    print(json.dumps({k:summary[k] for k in ['overall','background_chain','checks_passed','checks_total','counts','segments']},indent=2));assert all(checks.values()),[k for k,v in checks.items() if not v]
if __name__=='__main__':main()
