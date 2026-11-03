"""Review one completed DOC52 scenario. Does not run, kill, or attach anything."""
from pathlib import Path
import collections, datetime, hashlib, json, re, shutil
import audit
ROOT,OUT,E=audit.ROOT,audit.OUT,audit.E
def events(run):return [json.loads(x) for x in (run/'native.jsonl').read_text().splitlines()]
def review():
    rows=[]
    for role in ['HOST','CLIENT']:
        run=E/('N52_'+role+'_DIAG_01');launch=audit.load(run/'launch.json');journal=events(run)
        identity=audit.load(run/'OS_HANDLE_IDENTITY.json');signaled=audit.load(run/'OS_SIGNALED.json');closed=audit.load(run/'OBSERVER_CLOSED.json')
        status=audit.normalized(run/'STATUS_EXIT.json');messages=status['messages'];payloads=[m['payload'] for m in messages if isinstance(m.get('payload'),dict)]
        assert identity['compare_same_kernel_object'] and identity['primary']['creation_filetime']==identity['duplicate']['creation_filetime']
        assert signaled['handle']==identity['primary']['handle'] and signaled['pid']==launch['pid']
        assert signaled['wait']==0 and signaled['duplicate_wait']==0 and signaled['exit_query_ok'] and signaled['exit_code']==0
        assert not (run/'EMERGENCY_CLEANUP.json').exists() and closed['close_primary'] and closed['close_duplicate']
        assert status['state']=='detached' and not status['messages_truncated'] and not [m for m in messages if m.get('type')=='error']
        assert audit.load(run/'protected_before.json')==audit.load(run/'protected_after.json')
        assert not [x for x in journal if x['event'] in ['native_fault','blocked','hook_error','hook_create_error']]
        sim=launch['primary_thread_id'];stop=next(x for x in journal if x['event']==('network_stop_begin' if role=='HOST' else 'client_stop_begin'))
        closes=[p for p in payloads if p.get('kind')=='n51_socket_close_return' and p['utc']>=stop['utc'] and p['result']==0 and p['tid']!=sim]
        assert len(closes)==1;worker=closes[0]['tid']
        waits=[p for p in payloads if p.get('kind')=='n51_thread_wait_return' and p['utc']>=stop['utc'] and p['target_tid']==worker and p['tid']==sim and p['result']==0 and p['exit_code']==0]
        assert waits
        final=journal[-1];assert final['event']==('native_final_exit' if role=='HOST' else 'client_native_final_exit') and not final['failed']
        assert any(x['event']=='engine_shutdown_return' for x in journal)
        if role=='HOST':
            restores=[x for x in journal if x['event']=='control_restore'];assert len(restores)==4 and all(x['exact16'] for x in restores)
            resets=[x for x in journal if x['event']=='native_call_return' and x.get('operation')=='exit_reset'];assert len(resets)==1 and resets[0]['handle_null'] and resets[0]['p1_unchanged']
            assert final['external_handle_null'] and final['guards'] and final['foreign_calls']==0 and final['ticks_after_shutdown']==0
            updates=[x for x in journal if x['event']=='network_native_update'];assert updates and all(x['unchanged'] for x in updates)
        pss=(run/'LIVE_SNAPSHOT.log').read_text(encoding='utf8')
        assert 'PSS_CAPTURE result=0' in pss and 'PSS_FREE result=0' in pss
        counts=collections.Counter(x['event'] for x in journal)
        row=dict(role=role,pid=launch['pid'],primary_tid=sim,worker_tid=worker,identity=identity,signaled=signaled,native_final=final,event_counts=dict(counts),worker_close=closes,worker_wait=waits,live_snapshot_threads=len(re.findall(r'^THREAD tid=',pss,re.M)),live_snapshot_frames=len(re.findall(r'^FRAME tid=',pss,re.M)),live_snapshot_wct_cycles=re.findall(r'^WCT .*cycle=1',pss,re.M),live_snapshot_only=True,normal_exit=True,forced_cleanup=False)
        if role=='HOST':
            polls=[json.loads(s) for s in (run/'PROCESS_WAIT.jsonl').read_text().splitlines()]
            row['terminal_ledger_limit']='No entry harvested: all six slots zero at preceding poll; RPM error299 after fast normal exit. Native terminal API entry/return NOT observed.'
            row['process_observer_samples']=polls
        rows.append(row)
    final_os=audit.load(OUT/'OS_AFTER_CLOSE.json');assert not final_os['Processes'] and not final_os['Udp']
    sources=[dict(path=p.relative_to(ROOT).as_posix(),sha256=audit.sha(p)) for p in sorted(audit.LAB.glob('*')) if p.is_file()]
    frozen=OUT/'frozen_harness';frozen.mkdir()
    for p in audit.LAB.glob('*'):
        if p.is_file():shutil.copy2(p,frozen/p.name)
    binary=audit.LAB/'build/Release/terminal_snapshot.exe'
    shutil.copy2(binary,frozen/binary.name)
    native_artifacts=[]
    for role in ['HOST','CLIENT']:
        run=E/('N52_'+role+'_DIAG_01');cfg=audit.load(run/'NATIVE_ARTIFACT.json')
        p=Path(cfg['config']['dll']);assert audit.sha(p)==cfg['config']['dll_sha256'];native_artifacts.append(cfg)
    audit.dump('BUILD_SOURCE_MANIFEST.json',dict(configuration='External diagnostics only; VS2022 MSVC19.44 SDK10.0.26100.0 /MT /W4 /WX C++20 Release x64; production not rebuilt',diagnostic_sources=sources,diagnostic_exe=dict(path=binary.relative_to(ROOT).as_posix(),sha256=audit.sha(binary)),native_artifacts=native_artifacts,production_changes=0,wire_sequence_timeout_admission_unchanged=True))
    result=dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),verdict='PARTIAL',scenario_verdict='PASS: normal OS termination of this single diagnostic two-KEX scenario',root_cause_doc51='UNKNOWN',remediation='NONE: no proven cause; no production patch',native_scenarios=1,native_host_runs=1,native_client_runs=1,verification_runs_after_fix=0,rows=rows,final_os=final_os,scope='Neutral transport with live native P2; no W/S/movement re-proof, no world replication, no second run. Does not reproduce duration/activation history of DOC51.')
    audit.dump('RESULT_SUMMARY.json',result)
    for r in rows:print(r['role'],r['pid'],'worker',r['worker_tid'],'normal OS wait0/code0','threads',r['live_snapshot_threads'],'frames',r['live_snapshot_frames'])
    print('DOC52 PARTIAL; bounded scenario PASS; old cause UNKNOWN; production changes0')
if __name__=='__main__':review()
