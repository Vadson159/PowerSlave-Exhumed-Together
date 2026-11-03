"""Bind/review M1.1b diagnostics from files; never executes or changes game state."""
from pathlib import Path
import collections
import datetime
import hashlib
import json
import os
import shutil
import sys

LAB = Path(__file__).resolve().parent
ROOT = LAB.parents[2]
OUT = ROOT / 'docs/evidence/m1'
PREP = OUT / 'M11B_PREPARATION_20261101'
DLL_HASH = '0cb84521c47dddd20bdebf3a2f42dda79f2f50ef1cfc3c280c14878812648629'
EXE_HASH = 'b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'


def read(p):
    return json.loads(p.read_text(encoding='utf-8-sig'))


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def dump_new(p, value):
    with p.open('x', encoding='utf8') as stream:
        json.dump(value, stream, indent=2, ensure_ascii=False)


def baseline():
    PREP.mkdir(parents=True, exist_ok=True)
    prior = read(OUT / 'M11_IMPLEMENTATION_20261101/BASELINE.json')
    paths = [ROOT / r['path'] for r in prior['files']
             if r['path'].startswith(('experiments\\m0\\', 'tools\\enet-prototype\\'))]
    native_lab = ROOT / 'experiments/m1/native_bootstrap'
    paths += [p for p in native_lab.rglob('*') if p.is_file() and 'build' not in p.parts and 'profile' not in p.parts and '__pycache__' not in p.parts]
    paths += [native_lab / 'build/Release/pse_m11_bootstrap.dll', native_lab / 'build/Release/pse_m11_bootstrap.pdb',
              ROOT / 'docs/41_M1_NATIVE_BOOTSTRAP.md', ROOT / 'docs/40_THREAD_BRIDGE_P0_REMEDIATION.md']
    if sha(native_lab / 'build/Release/pse_m11_bootstrap.dll') != DLL_HASH:
        raise ValueError('Not the accepted M1.1 DLL')
    if sha(ROOT / 'experiments/m0/game/anubis_Shipping_Steam_x64.exe') != EXE_HASH:
        raise ValueError('EXE build changed')
    frozen = OUT / 'M11_NATIVE_03/native_artifact/pse_m11_bootstrap.dll'
    if sha(frozen) != DLL_HASH:
        raise ValueError('Accepted frozen DLL mismatch')
    records = [dict(path=str(p.relative_to(ROOT)), size=p.stat().st_size, sha256=sha(p)) for p in sorted(set(paths))]
    dump_new(PREP / 'BASELINE.json', dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
             exe_sha256=EXE_HASH, dll_sha256=DLL_HASH, immutable_files=records, no_game_launched=True,
             scope='Existing M0/native DLL/harness/transport code remains unchanged; M1.1b passive diagnostics only'))
    print(json.dumps(dict(status='PASS', immutable_files=len(records), exe_sha256=EXE_HASH, dll_sha256=DLL_HASH)))


def bind(name):
    if not name.replace('_', '').isalnum():
        raise ValueError('Invalid run')
    run = OUT / name
    launch, artifact = read(run / 'launch.json'), read(run / 'NATIVE_ARTIFACT.json')
    cfg = artifact['config']
    if cfg['pid'] != launch['pid'] or cfg['dll_sha256'] != DLL_HASH or sha(Path(cfg['dll'])) != DLL_HASH or launch['binary_sha256'] != EXE_HASH:
        raise ValueError('Native run/artifact identity changed')
    gate = read(run / 'ISOLATION_GATE.json')
    if gate['status'] != 'PASS' or gate['pid'] != launch['pid']:
        raise ValueError('Current isolation gate missing')
    rows = [json.loads(s) for s in (run / 'native.jsonl').read_text(encoding='utf8').splitlines()]
    if any(r['event'] in ['blocked', 'native_fault', 'hook_error', 'hook_create_error', 'state_exit_entry', 'shutdown_entry'] for r in rows):
        raise ValueError('Run failed/teardown already began')
    snapshots = [r for r in rows if r['event'] in ['independent_spawn', 'movement_checkpoint', 'sequence_complete'] and r.get('p2_updates', 0) >= 3]
    if not snapshots:
        raise ValueError('Need real native P1/P2 update evidence first')
    live = snapshots[-1]
    if live['p1_health'] <= 0 or live['p2_health'] <= 0 or not live['guards'] or live['tid'] != launch['primary_thread_id']:
        raise ValueError('Live-P2 gate failed')
    profile = read(OUT / 'M11_IMPLEMENTATION_20261101/NATIVE_COORDINATES.json')
    sites = [dict(name=s['analyst_role'], rva=int(s['rva'], 16), prefix=s['prefix16']) for s in profile['sites'][:11]]
    config = dict(pid=launch['pid'], target=launch['target'], dll=cfg['dll'], dll_sha256=DLL_HASH,
                  simulation_tid=launch['primary_thread_id'], player='0x' + live['p2_player'].removeprefix('0x'), sites=sites,
                  controls=[s for s in sites if s['name'] in ['Update', 'Commands', 'Turning', 'Getter']])
    source = 'const cfg = ' + json.dumps(config) + ';\n' + (LAB / 'probe.js').read_text(encoding='utf8')
    dest = run / 'direct_quit_probe.js'
    with dest.open('x', encoding='utf8') as stream:
        stream.write(source)
    dump_new(run / 'DIRECT_QUIT_PROBE.json', dict(config=config, source_sha256=sha(dest), native_live_witness=live,
             abi='Documented Win64 VirtualQuery only; no KEX calls or game-object writes',
             source=str((LAB / 'probe.js').relative_to(ROOT))))
    print(json.dumps(dict(status='PASS', pid=launch['pid'], p2_updates=live['p2_updates'], p2_health=live['p2_health'], probe=str(dest))))


def review(name):
    run = OUT / name
    launch, artifact = read(run / 'launch.json'), read(run / 'NATIVE_ARTIFACT.json')
    rows = [json.loads(s) for s in (run / 'native.jsonl').read_text(encoding='utf8').splitlines()]
    events = collections.defaultdict(list)
    for row in rows:
        events[row['event']].append(row)
    capture = read(run / 'status_post_exit_drain.json')['structuredContent']['normalized_result']
    messages = capture['messages']
    payloads = [m['payload'] for m in messages if isinstance(m.get('payload'), dict)]
    ready = [p for p in payloads if p.get('kind') == 'm11b_exit_observer_ready']
    os_exits = [p for p in payloads if p.get('kind') == 'm11b_os_exit']
    releases = [p for p in payloads if p.get('kind') in ['m11b_target_free_library', 'm11b_target_ldr_unload', 'm11b_target_virtual_free']]
    errors = [m for m in messages if m.get('type') == 'error']
    faults = [r for r in rows if r['event'] in ['blocked', 'native_fault', 'hook_error', 'hook_create_error']]
    one = lambda k: events[k][0]
    resets = [r for r in events['native_call_return'] if r['operation'] == 'exit_reset']
    attempts = [r for r in events['native_call_attempt'] if r['operation'] == 'exit_reset']
    guard = max([p for p in payloads if p.get('kind') == 'isolation_write_guard_summary'], key=lambda p: (p['write_opens'], p.get('utc', '')))
    outside = [p for p in payloads if p.get('kind') == 'file_write_open_observed' and p.get('succeeded') and not Path(p['path']).resolve().is_relative_to(ROOT)]
    baseline_record = read(PREP / 'BASELINE.json')
    changed = [r['path'] for r in baseline_record['immutable_files'] if sha(ROOT / r['path']) != r['sha256']]
    preparation = read(ROOT / 'docs/evidence/m0/M0_001_PREPARATION.json')
    game_errors = []
    for r in preparation['independent_copy_files']:
        a, b = Path(preparation['source']) / r['relative_path'], Path(preparation['copy']) / r['relative_path']
        if sha(a) != r['sha256'] or sha(b) != r['sha256'] or os.path.samefile(a, b):
            game_errors.append(r['relative_path'])
    protected_before, protected_after = read(run / 'protected_before.json'), read(run / 'protected_after.json')
    integrity = dict(immutable_files_checked=len(baseline_record['immutable_files']), changed_immutable_files=changed,
                     original_and_copy_files_checked=len(preparation['independent_copy_files']), game_errors=game_errors,
                     protected_profile_roots=len(protected_before), protected_snapshots_equal=protected_before == protected_after,
                     existing_dll_sha256=sha(ROOT / 'experiments/m1/native_bootstrap/build/Release/pse_m11_bootstrap.dll'))
    profile = Path(launch['profile'])
    for p in profile.rglob('*'):
        if p.is_file() and p.name in ['stdout.txt', 'stderr.txt', 'kexengine.cfg', 'consoleHistory.txt']:
            dest = run / 'native_files' / p.relative_to(profile)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(p, dest)
    checkpoints = {
        'existing_accepted_dll': artifact['config']['dll_sha256'] == DLL_HASH == sha(Path(artifact['config']['dll'])) == integrity['existing_dll_sha256'],
        'fresh_isolation_gate': read(run / 'ISOLATION_GATE.json')['status'] == 'PASS' and not outside and not guard['errors'] and not guard['unique_overflow'],
        'live_independent_p2_and_native_updates': one('before_unbind')['p2_health'] > 0 and one('before_unbind')['p1_health'] > 0 and one('sequence_metrics')['native_p2_updates'] >= 3 and one('sequence_metrics')['input_comparisons'] >= 3,
        'native_direct_quit_no_state_exit': not events['state_exit_entry'] and not events['state_exit_return'] and not one('shutdown_entry')['state_exit_preceded'] and one('shutdown_entry')['live_external_handle'],
        'one_unbind_before_engine_services': len(attempts) == len(resets) == 1 and attempts[0]['reason'] == 'DirectQuit' and resets[0]['handle_null'] and resets[0]['p1_unchanged'] and one('shutdown_entry')['seq'] < resets[0]['seq'] < one('shutdown_quiesced')['seq'] < one('engine_shutdown_entry')['seq'],
        'control_hooks_restored_exactly': len(events['control_restore']) == 4 and all(r['exact16'] for r in events['control_restore']),
        'no_observed_owned_destructor_or_forced_teardown': not events['owned_actor_dtor_entry'] and not events['owned_actor_dtor_return'] and not one('shutdown_quiesced')['forced_teardown'],
        'native_final_pin_quiesce_witness': one('engine_shutdown_return')['seq'] < one('native_final_exit')['seq'] and one('native_final_exit')['module_pinned'] and one('native_final_exit')['storage_pinned'] and one('native_final_exit')['external_handle_null'] and one('native_final_exit')['guards'] and one('native_final_exit')['ticks_after_shutdown'] == 0,
        'pre_quit_mapped_code_storage_and_trampolines': bool(ready) and ready[0]['snapshot']['callback_code_mapped'] and ready[0]['snapshot']['trampoline_code_mapped'] and ready[0]['snapshot']['player_storage_readable'] and ready[0]['snapshot']['guards'],
        'os_exitprocess_observed': any(p['api'] == 'kernel32.dll!ExitProcess' for p in os_exits),
        'code_storage_and_trampolines_at_exitprocess': any(p['api'] == 'kernel32.dll!ExitProcess' and p['snapshot']['callback_code_mapped'] and p['snapshot']['trampoline_code_mapped'] and p['snapshot']['player_storage_readable'] and p['snapshot']['external_handle'] == '0x0' and p['snapshot']['guards'] and p['snapshot']['controls_restored'] for p in os_exits),
        'thread_guards_and_recorded_faults': all(r['tid'] == launch['primary_thread_id'] for r in rows[2:]) and one('native_final_exit')['foreign_calls'] == 0 and not one('native_final_exit')['failed'] and not faults and not errors,
        'immutable_game_profiles_dll_and_transport': not changed and not game_errors and integrity['protected_snapshots_equal'],
        'complete_capture_and_native_termination': [r['seq'] for r in rows] == list(range(1, len(rows)+1)) and not capture['messages_truncated'] and capture['state'] == 'detached' and read(run / 'exit.json')['exit_code'] == 0,
    }
    report = dict(status='PASS' if all(checkpoints.values()) else 'PARTIAL', runtime_tests=1,
                  pid=launch['pid'], simulation_tid=launch['primary_thread_id'], exe_sha256=EXE_HASH, dll_sha256=DLL_HASH,
                  checkpoints=checkpoints, pending=[k for k, v in checkpoints.items() if not v],
                  blocker='OS observer delivered readiness but no ExitProcess/RtlExitUserProcess boundary snapshots. Cause UNKNOWN; no second run authorized/performed.' if not os_exits else None,
                  native_records=len(rows), provider_messages=len(messages), provider_truncated=capture['messages_truncated'],
                  native_shutdown_records=[r for r in rows if r['seq'] >= one('shutdown_entry')['seq']],
                  os_exit_records=os_exits, pre_quit_observer=ready, observed_release_attempts=releases,
                  recorded_native_faults=faults, script_errors=errors, last_guard_summary=guard, successful_outside_write_opens=outside,
                  integrity=integrity, preserved_to_native_final_exit=True,
                  preserved_to_actual_os_exit_checkpoint='UNKNOWN' if not os_exits else 'see snapshots',
                  no_dll_rebuild_or_edit=True, no_forced_teardown_or_hot_unload=True, no_m12_or_network_changes=True,
                  limits=['Module PIN is source-verified and reported at native final exit; direct OS memory checkpoint was not captured.',
                          'No recorded target FreeLibrary/LdrUnloadDll/VirtualFree events does not replace the missing final checkpoint.',
                          'No recorded AV/illegal instruction/stack overflow is not exhaustive exception tracing.',
                          'Bounded file API guard is not an OS sandbox; protected hashes and original/copy hashes checked separately.'])
    dump_new(run / 'RESULT_SUMMARY.json', report)
    dump_new(run / 'INTEGRITY.json', integrity)
    dump_new(run / 'LIFECYCLE.json', report['native_shutdown_records'])
    print(json.dumps(dict(status=report['status'], pending=report['pending'], native_records=len(rows), provider_messages=len(messages), integrity=integrity), ensure_ascii=False))


if __name__ == '__main__':
    if sys.argv[1] == 'baseline':
        baseline()
    elif sys.argv[1] == 'bind':
        bind(sys.argv[2])
    elif sys.argv[1] == 'review':
        review(sys.argv[2])
    else:
        raise ValueError('Unknown file-only action')
