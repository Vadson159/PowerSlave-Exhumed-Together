"""Transactional renderer selection in a verified TEST COPY only; original is read-only.
Stage after accepted launch verification, before the suspended primary is resumed.
Restore only after the retained launcher exit receipt, preserving exact original bytes.
"""
from pathlib import Path
import hashlib
import json
import os

ROOT=Path(__file__).resolve().parents[3]

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def stage(run):
    run=Path(run).resolve();assert run.is_relative_to(ROOT)
    launch=json.loads((run/'launch.json').read_text('utf8'))
    game=Path(launch['target']).resolve().parent
    allowed=[(ROOT/'experiments/m0/game').resolve(),(ROOT/'experiments/m1/native_second_instance/game').resolve()]
    if game not in allowed:raise ValueError('Only the two accepted test copies')
    path=game/'kexengine.cfg'
    prep=json.loads((ROOT/'docs/evidence/m0/M0_001_PREPARATION.json').read_text('utf8'))
    original=Path(prep['source'])/'kexengine.cfg'
    # kexengine.cfg is an extra diagnostic-copy file, absent from the original
    # installation's protected77 in this setup. If original has one, it remains
    # read-only and must be an independent matching file before stage.
    if original.exists() and (os.path.samefile(path,original) or sha(path)!=sha(original)):
        raise ValueError('Test config must match existing read-only original, independently')
    before_exists=path.exists();before=path.read_bytes() if before_exists else b''
    needle=b'seta r_rhirenderfamily "vulkan"'
    if before_exists and before.count(needle)!=1:raise ValueError('Expected one stock Vulkan selection')
    after=before.replace(needle,b'seta r_rhirenderfamily "d3d11"') if before_exists else b'seta r_rhirenderfamily "d3d11"\r\n'
    backup=run/'RHI_ORIGINAL.bin'
    with backup.open('xb') as stream:stream.write(before)
    receipt=dict(path=str(path),backup=str(backup),pid=launch['pid'],before_exists=before_exists,original_sha256=sha(original) if original.exists() else None,original_config_absent=not original.exists(),before_sha256=sha(backup),
                 staged_sha256=hashlib.sha256(after).hexdigest(),renderer='d3d11',test_copy_only=True,
                 stage='after full77 launch verify / before suspended primary resume')
    with (run/'RHI_STAGE.json').open('x',encoding='utf8') as stream:json.dump(receipt,stream,indent=2)
    path.write_bytes(after)

def restore(run):
    run=Path(run).resolve();assert run.is_relative_to(ROOT)
    receipt=json.loads((run/'RHI_STAGE.json').read_text('utf8'))
    exit=json.loads((run/'exit.json').read_text('utf8'))
    launch=json.loads((run/'launch.json').read_text('utf8'))
    identity=json.loads((run/'OS_HANDLE_IDENTITY.json').read_text('utf8'))
    signaled=json.loads((run/'OS_SIGNALED.json').read_text('utf8'))
    if not (signaled['pid']==receipt['pid']==launch['pid']==identity['primary']['pid'] and
            identity['compare_same_kernel_object'] and signaled['identity']==identity['primary'] and
            signaled['identity']['path'].lower()==launch['target'].lower() and
            signaled['wait']==signaled['duplicate_wait']==0 and signaled['exit_query_ok'] and
            signaled['exit_code']==exit['exit_code']==0 and signaled['no_emergency_cleanup']):
        raise ValueError('Exact retained process termination required')
    path=Path(receipt['path']).resolve();backup=Path(receipt['backup']).resolve()
    allowed=[(ROOT/'experiments/m0/game/kexengine.cfg').resolve(),(ROOT/'experiments/m1/native_second_instance/game/kexengine.cfg').resolve()]
    if path not in allowed or not backup.is_relative_to(run) or sha(backup)!=receipt['before_sha256']:raise ValueError('Restore provenance')
    if sha(path)!=receipt['staged_sha256']:raise ValueError('Unexpected test config change; preserve for review')
    if receipt.get('before_exists',True):path.write_bytes(backup.read_bytes())
    else:path.unlink()
    with (run/'RHI_RESTORED.json').open('x',encoding='utf8') as stream:
        exact=sha(path)==receipt['before_sha256'] if receipt.get('before_exists',True) else not path.exists()
        json.dump(dict(status='PASS' if exact else 'FAIL',path=str(path),sha256=sha(path) if path.exists() else None,absent=not path.exists(),exit_receipt=exit),stream,indent=2)

if __name__=='__main__':
    import sys
    {'stage':stage,'restore':restore}[sys.argv[1]](ROOT/sys.argv[2])
