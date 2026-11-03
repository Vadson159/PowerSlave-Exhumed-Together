"""Offline review of retained M1.1 runtime evidence. Never attaches or launches."""
from pathlib import Path
import ast
import collections
import datetime
import hashlib
import json
import math
import shutil

LAB = Path(__file__).resolve().parent
ROOT = LAB.parents[2]
OUT = ROOT / 'docs/evidence/m1'
RUN = OUT / 'M11_NATIVE_03'
IMPLEMENTATION = OUT / 'M11_IMPLEMENTATION_20261101'


def read(p):
    return json.loads(p.read_text(encoding='utf-8-sig'))


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def write(p, data):
    p.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf8')


def main():
    launch, artifact = read(RUN / 'launch.json'), read(RUN / 'NATIVE_ARTIFACT.json')
    rows = [json.loads(s) for s in (RUN / 'native.jsonl').read_text(encoding='utf8').splitlines()]
    by = collections.defaultdict(list)
    for row in rows:
        by[row['event']].append(row)
    one = lambda event: by[event][0]
    final_capture = read(RUN / 'status_final_after_quit.json')['structuredContent']['normalized_result']
    messages = final_capture['messages']
    payloads = [m['payload'] for m in messages if isinstance(m.get('payload'), dict)]
    summaries = [p for p in payloads if p.get('kind') == 'isolation_write_guard_summary']
    guard = max(summaries, key=lambda p: (p.get('write_opens', 0), p.get('utc', '')))
    write_opens = [p for p in payloads if p.get('kind') == 'file_write_open_observed' and p.get('succeeded')]
    outside = [p for p in write_opens if not Path(p['path']).resolve().is_relative_to(ROOT)]
    failures = [r for r in rows if r['event'] in ['blocked', 'hook_error', 'hook_create_error', 'native_fault']]
    script_errors = [m for m in messages if m.get('type') == 'error']
    a, b, metrics = one('independent_spawn'), one('sequence_complete'), one('sequence_metrics')
    pre_exit, reset, dtor, world_return, process_exit = one('before_unbind'), [r for r in by['native_call_return'] if r['operation'] == 'exit_reset'][0], one('owned_actor_dtor_return'), one('state_exit_return'), one('native_final_exit')
    phases = by['movement_checkpoint'] + [b]
    ops = collections.Counter(r['operation'] for r in by['native_call_attempt'])
    returns = collections.Counter(r['operation'] for r in by['native_call_return'])
    p1_keys = ['p1_player', 'p1_actor', 'p1_node', 'p1_owner', 'p1_health']
    input_rows = by['local_input_after_sequence']
    input_distance = math.dist(input_rows[0]['pos'], input_rows[-1]['pos']) if input_rows else 0
    source = LAB / 'src/bootstrap.cpp'
    frozen_source = RUN / 'native_artifact/source/src/bootstrap.cpp'
    criteria = {
        'dll_loaded_and_bootstrapped': one('dll_prepared')['binary_sha256'] == launch['binary_sha256'] and one('bootstrap_ready')['module_pinned'],
        'exact_frozen_dll_and_native_source': sha(Path(artifact['config']['dll'])) == artifact['config']['dll_sha256'] == sha(LAB / 'build/Release/pse_m11_bootstrap.dll') and sha(source) == sha(frozen_source),
        'simulation_thread_attested': one('simulation_attested')['observed_tid'] == launch['primary_thread_id'] and all(r['tid'] == launch['primary_thread_id'] for r in rows[2:]) and process_exit['foreign_calls'] == 0,
        'separate_native_puppet': a['p1_actor'] != a['p2_actor'] and a['p1_node'] != a['p2_node'] and a['p1_player'] == a['p1_owner'] and a['p2_player'] == a['p2_owner'] and a['p1_player'] != a['p2_player'] and any(r.get('operation') == 'spawn' and r.get('type') == 0 for r in rows),
        '120_commands_controller_and_native_world_updates': metrics['native_p2_updates'] == metrics['native_p2_puppet_ticks'] == metrics['native_p2_commands'] == 120 and [p['p2_updates'] for p in phases] == [45, 60, 105, 120],
        'actual_finite_native_movement': metrics['max_native_xy_displacement'] > 1 and all(all(math.isfinite(x) for x in p['p2_pos']) for p in phases),
        'p1_identity_health_input_preserved': all(a[k] == b[k] == pre_exit[k] for k in p1_keys) and a['p1_pos'] == b['p1_pos'] and metrics['input_comparisons'] == 120 and not failures and not metrics['failed'],
        'local_window_input_continued': bool(input_rows) and [r['held_forward'] for r in input_rows] == list(range(1, len(input_rows) + 1)) and input_distance > 1,
        'final_p2_command_neutral': metrics['neutral_final_buttons'] == 0,
        'native_lifecycle_once_only': ops == returns == collections.Counter({'initial_reset': 1, 'spawn': 1, 'weapon_ready': 1, 'exit_reset': 1}),
        'state_exit_unbind_before_native_destructor': one('state_exit_entry')['seq'] < reset['seq'] < one('owned_actor_dtor_entry')['seq'] < dtor['seq'] < world_return['seq'] and reset['handle_null'] and reset['p1_unchanged'] and world_return['owned_dtor_returns'] == 1 and world_return['external_handle_null'] and not dtor['actor_reads_after_return'],
        'exact_control_restoration': one('getter_scope_restored')['exact16'] and len(by['control_restore']) == 8 and all(r['exact16'] for r in by['control_restore']),
        'quit_after_world_exit_pinned_until_native_final_exit': one('shutdown_entry')['state_exit_preceded'] and not one('shutdown_entry')['live_external_handle'] and one('shutdown_quiesced')['pin_until_process_exit'] and not one('shutdown_quiesced')['forced_teardown'] and one('engine_shutdown_return')['seq'] < process_exit['seq'] and process_exit['module_pinned'] and process_exit['storage_pinned'] and process_exit['ticks_after_shutdown'] == 0 and not process_exit['actor_reads'],
        'guards_and_recorded_faults': all(r['guards'] for r in rows if 'guards' in r) and not failures and not process_exit['failed'],
        'bounded_isolation_observation': read(RUN / 'ISOLATION_GATE.json')['status'] == 'PASS' and not outside and not guard['errors'] and not guard['unique_overflow'] and not script_errors and not final_capture['messages_truncated'],
        'native_capture_complete_and_terminated': [r['seq'] for r in rows] == list(range(1, len(rows) + 1)) and final_capture['state'] == 'detached' and read(RUN / 'exit.json')['exit_code'] == 0,
    }

    baseline = read(IMPLEMENTATION / 'BASELINE.json')
    immutable = [r for r in baseline['files'] if r['path'].startswith(('experiments\\m0\\', 'tools\\enet-prototype\\'))]
    changed = [r['path'] for r in immutable if not (ROOT / r['path']).exists() or sha(ROOT / r['path']) != r['sha256']]
    original_manifest = read(ROOT / 'docs/evidence/m0/M0_001_PREPARATION.json')
    source_game = Path(original_manifest['source'])
    copy_game = Path(original_manifest['copy'])
    game_errors = []
    for record in original_manifest['independent_copy_files']:
        for folder in [source_game, copy_game]:
            p = folder / record['relative_path']
            if sha(p) != record['sha256']:
                game_errors.append(str(p))
    protected = []
    for name in ['M11_NATIVE_02', 'M11_NATIVE_03']:
        run = OUT / name
        protected.append(dict(run=name, equal=read(run / 'protected_before.json') == read(run / 'protected_after.json')))
        launch_record = read(run / 'launch.json')
        profile = Path(launch_record['profile'])
        for p in profile.rglob('*'):
            if p.is_file() and p.name in ['stdout.txt', 'stderr.txt', 'kexengine.cfg', 'consoleHistory.txt']:
                dest = run / 'native_files' / p.relative_to(profile)
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(p, dest)
    integrity = dict(original_and_copy_files_checked=len(original_manifest['independent_copy_files']), game_errors=game_errors,
                     protected_profiles=protected, immutable_m0_and_transport_source_files_checked=len(immutable), changed_immutable_sources=changed)
    criteria['original_profiles_m0_and_transport_unchanged'] = not game_errors and not changed and all(p['equal'] for p in protected)
    for p in LAB.glob('*.py'):
        ast.parse(p.read_text(encoding='utf8'))
    final_harness = IMPLEMENTATION / 'final_harness'
    final_harness.mkdir(exist_ok=True)
    for p in list(LAB.glob('*.py')) + [LAB / 'README.md', LAB / 'CMakeLists.txt']:
        shutil.copy2(p, final_harness / p.name)

    result = dict(status='PASS' if all(criteria.values()) else 'PARTIAL',
                  scope='M1.1, exact-build 2-player native DLL, one permitted 120-update P2 sequence, StateExit then title-screen Quit. Not M1.2.',
                  utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), binary_sha256=launch['binary_sha256'], dll_sha256=artifact['config']['dll_sha256'],
                  pid=launch['pid'], simulation_tid=launch['primary_thread_id'], criteria=criteria, native_records=len(rows), provider_messages=len(messages),
                  sequence=metrics, primary_input_samples=input_rows, primary_displacement_during_logged_input=input_distance,
                  primary_displacement_before_unbind=math.dist(b['p1_pos'], pre_exit['p1_pos']),
                  lifetime_order=[r for r in rows if r['event'] in ['state_exit_entry', 'owned_actor_dtor_entry', 'owned_actor_dtor_return', 'state_exit_return', 'shutdown_entry', 'shutdown_quiesced', 'engine_shutdown_entry', 'engine_shutdown_return', 'native_final_exit'] or r.get('operation') == 'exit_reset'],
                  isolation_guard_last_recorded=guard, successful_outside_write_opens=outside, script_errors=script_errors, recorded_native_faults=failures,
                  integrity=integrity, limitations=[
                      'Two earlier startup/preflight attempts retained:01 no game process,02 no permit/P2 due diagnostic ledger overflow.',
                      'The final run has exactly one P2 construction and controlled sequence. No repeated M0 experiment.',
                      'Live-P2 Direct Quit branch of this DLL was not executed: final Quit followed accepted StateExit. C25 remains its native foundation.',
                      'P1 window input was PostMessageW, not physical hardware. The P2 thirteen-button polling loop remains; broad concurrent input behavior is unaccepted.',
                      'DLL/code/storage/trampolines remain pinned even after StateExit. No hot unload or general deallocation claim.',
                      'Diagnostic API guard is not a complete OS sandbox; last summary precedes final shutdown opens, which CreateFileW records separately.',
                      'Native fault logger covers access violation, illegal instruction and stack overflow; no claim of exhaustive exception tracing.',
                      'No network, bridge, 3-8 capacity, campaign, respawn, weapon damage, achievements or long-session acceptance in M1.1.'])
    write(RUN / 'RESULT_SUMMARY.json', result)
    write(IMPLEMENTATION / 'INTEGRITY.json', integrity)
    print(json.dumps(dict(status=result['status'], criteria=len(criteria), failed=[k for k, v in criteria.items() if not v],
                          native_records=len(rows), provider_messages=len(messages), integrity=integrity), ensure_ascii=False))
    raise SystemExit(result['status'] != 'PASS')


if __name__ == '__main__':
    main()
