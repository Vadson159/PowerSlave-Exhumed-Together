"""Executor/checker P16 fresh read-only gate (user assigned both roles).
Reads a complete native log prefix, verifies current PID/TID/DLL/EXE and the
scoped getter witness before producing any mailbox permit. No KEX calls.
"""
import argparse,json,hashlib
from pathlib import Path
from runtime_p16 import r
def main():
    p=argparse.ArgumentParser();p.add_argument('role',choices=['host','client']);p.add_argument('operation',choices=['keys','restore']);p.add_argument('review',type=int);a=p.parse_args()
    run=r.OUT/r.RUNS[a.role];cfg=r.load(run/'NATIVE_ARTIFACT.json')['config'];launch=r.load(run/'launch.json');iso=r.load(run/'ISOLATION_GATE.json')
    raw=Path(cfg['native_log']).read_bytes();raw=raw[:raw.rfind(b'\n')+1];rows=[json.loads(line) for line in raw.splitlines()]
    event='p16_keys_readonly_probe' if a.operation=='restore' else ('p7_keys_readonly_probe' if a.role=='host' else 'p7_keys_read_probe')
    row=next(x for x in reversed(rows) if x['event']==event)
    probe=row['probe'] if a.operation=='restore' else row['serial']
    receiver=row['receiver'] if 'receiver' in row else row['player'];expected=row.get('expected',row.get('expected_reference'))
    ref=row['reference'];generation=row.get('generation',row.get('entity_generation'));epoch=row.get('epoch',row.get('map_epoch'))
    lifetime=[x for x in rows if x['event']=='p16_keys_readonly_probe' and int(x['receiver'],16)==int(receiver,16)]
    current_generation=lifetime[-1]['generation'] if lifetime else 1
    checks=dict(isolation=iso['status']=='PASS' and iso['pid']==launch['pid']==cfg['pid'],
        exe=r.sha(cfg['target'])==r.EXPECTED,dll=r.sha(cfg['dll'])==cfg['dll_sha256'],sim=row['tid']==launch['primary_thread_id'],
        reference=int(ref,16)==int(receiver,16)+0x98==int(expected,16),read_only=row.get('writes',0)==0 and not row.get('write',False),
        scalar=0<=row['value']<=15,generation=generation>0 and generation==current_generation,epoch=epoch is not None and epoch>0,probe=probe>0,
        no_failure=not any(x['event'] in ['blocked','native_fault','client_native_fault','p7_native_fault'] for x in rows))
    if a.operation=='restore':checks['fresh_allocation']=row['allocation']>0 and int(row['actor'],16)>0 and int(row['node'],16)>0
    record=dict(status='PASS' if all(checks.values()) else 'FAIL',pid=cfg['pid'],role=a.role,operation=a.operation,probe=probe,review=a.review,
        generation=generation,epoch=epoch,witness=row,checks=checks,log_prefix_bytes=len(raw),log_prefix_sha256=hashlib.sha256(raw).hexdigest(),
        dll_sha256=cfg['dll_sha256'],binary_sha256=r.EXPECTED,reviewer='current executor/checker, explicitly assigned by user',writes=0)
    path=run/f'KEYS_{a.operation}_{generation}_{probe}_{a.review}.json';r.dump(path,record)
    print(path);print(record['status'],checks)
    return 0 if record['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
