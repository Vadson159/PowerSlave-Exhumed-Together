"""Recheck retained P24 evidence only; never attaches, launches or calls the game."""
from pathlib import Path
import argparse
import json
import ntpath
import p24

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bundle', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    bundle = p24.load(a.bundle)
    checks = []
    reports = {}
    def check(name, passed, **data): checks.append(dict(name=name, passed=bool(passed), **data))
    for role in ['host', 'client']:
        stage = p24.load(p24.OUT / ('STAGE_' + role + '.json'))
        run = p24.OUT / stage['run']
        launch = p24.load(run / 'launch.json')
        tid, pid = launch['primary_thread_id'], launch['pid']
        captures = [r for r in bundle['records'] if r.get('normalized_result', {}).get('pid') == pid
                    and 'messages' in r['normalized_result']]
        record = max(captures, key=lambda r: len(r['normalized_result']['messages']))
        capture = record['normalized_result']
        payload = [m['payload'] for m in capture['messages'] if isinstance(m.get('payload'), dict)]
        native = [json.loads(s) for s in (run / 'native.jsonl').read_text(encoding='utf8').splitlines()]
        def rows(event): return [r for r in native if r['event'] == event]
        guard = max([p for p in payload if p.get('kind') == 'isolation_write_guard_summary'], key=lambda p: (p['write_opens'], p['utc']))
        outside = [p for p in payload if p.get('kind') == 'file_write_open_observed' and p.get('succeeded') and
                   not p24.ROOT.resolve() in Path(p.get('path', '')).resolve().parents]
        errors = [m for m in capture['messages'] if m['type'] == 'error']
        faults = [r for r in native if r['event'] in ['blocked', 'native_fault', 'hook_error', 'hook_create_error', 'native_proxy_fault']]
        prepared = rows('dll_prepared' if role == 'host' else 'client_dll_prepared')
        ticks = [r for r in rows('tick_status') if r.get('state') == 3 and r.get('pending') == 0 and r.get('local_updates', 0) > 0 and r.get('local_this_tick') == 1 and r['tid'] == tid]
        observed = [p for p in payload if p.get('kind') == 'p24_loaded_modules' and p.get('pid') == pid]
        modules = {m['name'].lower(): m['path'] for m in observed[-1]['modules']}
        expected = {name: str(p24.COPIES[role] / name) for name in ['SDL2.dll', 'SDL2_pse_original.dll', 'pe-together.dll', 'pse-runtime.dll']}
        restored = rows('control_restore') if role == 'host' else rows('native_proxy_hook_restored')
        final = rows('native_final_exit' if role == 'host' else 'client_native_final_exit')
        gate = p24.load(run / 'ISOLATION_GATE.json')
        scope = p24.load(run / 'LOADER_TEST_SCOPE.json')
        source = (run / 'generated_isolation.js').read_text(encoding='utf8')
        check(role + ' exact EXE', launch['binary_sha256'] == p24.SHA and p24.sha(p24.COPIES[role] / p24.EXE) == p24.SHA)
        check(role + ' frozen P1 payload', stage['payload_sha'] == p24.PAYLOAD_SHA[role] and p24.sha(p24.PAYLOAD[role]) == p24.PAYLOAD_SHA[role])
        check(role + ' independently loaded layers observed', all(ntpath.normcase(modules.get(n.lower(), '')) == ntpath.normcase(p) for n, p in expected.items()))
        check(role + ' prepare on primary once', len(prepared) == 1 and prepared[0]['tid'] == tid)
        check(role + ' bootstrap ready once', len(rows('bootstrap_ready')) == 1 and rows('bootstrap_ready')[0]['tid'] == tid)
        check(role + ' native simulation', bool(ticks), first_tick=ticks[0] if ticks else None)
        check(role + ' native thread affinity', all(r['tid'] == tid for r in native), native_records=len(native))
        check(role + ' no Frida DLL loader', not scope['js_dll_loader'] and not (run / 'load_native.js').exists()
              and 'M11Prepare' not in source and 'LoadLibrary' not in source and not any(p.get('kind') == 'm11_dll_loader' for p in payload))
        check(role + ' isolation gate', gate['status'] == 'PASS' and gate['pid'] == pid and guard['allowed_disk'] > 0
              and not guard['errors'] and not guard['unique_overflow'] and not outside, guard=guard)
        check(role + ' no fault or capture loss', not errors and not faults and not capture.get('messages_truncated')
              and not any(p.get('kind') == 'native_exception' for p in payload))
        check(role + ' no gameplay permit or P2/proxy spawn', not scope['gameplay_permit'] and not rows('proxy_spawned') and not rows('spawned') and not rows('isolation_permit'))
        check(role + ' exact control restores', len(restored) == (4 if role == 'host' else 6) and all(r['exact16'] for r in restored))
        check(role + ' native terminal quiescence', len(final) == 1 and not final[0]['failed'] and final[0]['foreign_calls'] == 0 and final[0]['module_pinned'])
        check(role + ' native exit0', p24.load(run / 'exit.json')['exit_code'] == 0)
        check(role + ' protected snapshots equal', p24.load(run / 'protected_before.json') == p24.load(run / 'protected_after.json'))
        reports[role] = dict(pid=pid, primary_tid=tid, native_sha256=p24.sha(run / 'native.jsonl'),
             capture_evidence=record['evidence_id'], guard=guard, prepared=prepared, bootstrap=rows('bootstrap_ready'),
             final=final, restoration_records=restored, scope='Loader and unpermitted native bootstrap only; no network/gameplay acceptance.')
    ok = all(c['passed'] for c in checks)
    p24.dump(a.out, dict(status='PASS' if ok else 'FAIL', passed=sum(c['passed'] for c in checks), total=len(checks), checks=checks, roles=reports,
             bundle=str(a.bundle), bundle_sha256=p24.sha(a.bundle),
             limitation='Frida guarded profiles and observed modules. Loader is independent; a completely Frida-free game process is NOT observed.'))
    print(f"P24 retained runtime {'PASS' if ok else 'FAIL'} {sum(c['passed'] for c in checks)}/{len(checks)}")
    for c in checks:
        if not c['passed']: print(c)
    return 0 if ok else 1

if __name__ == '__main__': raise SystemExit(main())
