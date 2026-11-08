"""Read-only P16 evidence audit; never starts KEX or grants native permits.

python -X utf8 -B experiments/pse_together/tools/audit_p16.py --out NEW_DIR
All retained runtime assertions explicitly refer to the accepted P7 run03,
not to the new offline P16 policy. Package status remains PARTIAL.
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import subprocess
import dll_compare

ROOT=Path(__file__).resolve().parents[3]
E=ROOT/'docs/evidence/m1/P16_RESPAWN'
EXE='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'

def read(p):
    return json.loads(Path(p).read_text(encoding='utf-8-sig'))

def sha(p):
    with Path(p).open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()

def snapshot(path):
    path=Path(path)
    return dict(path=str(path),exists=path.exists(),files=[dict(relative_path=str(p.relative_to(path)),size=p.stat().st_size,sha256=sha(p))
        for p in sorted(path.rglob('*')) if p.is_file()] if path.exists() else [])

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',required=True);args=ap.parse_args()
    out=(ROOT/args.out).resolve()
    if not out.is_relative_to(ROOT) or out.exists():raise ValueError('A fresh output inside the workspace is required')
    out.mkdir(parents=True)
    checks=[]
    def check(name,ok,**detail):checks.append(dict(name=name,passed=bool(ok),**detail))
    prep=read(ROOT/'docs/evidence/m0/M0_001_PREPARATION.json')
    original=Path(prep['source']);original_now=snapshot(original)
    check('full original snapshot still equals 77-file original baseline',original_now==prep['original_installation_before'] and len(original_now['files'])==77)
    files=[]
    copies=[ROOT/'experiments/m0/game',ROOT/'experiments/m1/native_second_instance/game']
    for row in prep['independent_copy_files']:
        p=original/row['relative_path']
        record=dict(path=row['relative_path'],original=sha(p),copies=[])
        for folder in copies:
            q=folder/row['relative_path']
            record['copies'].append(dict(path=str(q),sha256=sha(q),size=q.stat().st_size,independent=not os.path.samefile(p,q)))
        record['passed']=record['original']==row['sha256'] and all(x['sha256']==row['sha256'] and x['size']==row['size'] and x['independent'] for x in record['copies'])
        files.append(record)
    check('independent rehash original77 and both copied sets',len(files)==77 and all(r['passed'] for r in files))
    check('both exact target EXEs',all(sha(p/'anubis_Shipping_Steam_x64.exe')==EXE for p in copies))
    p7=ROOT/'docs/evidence/m1/PSE_TOGETHER_P7'
    protected=read(p7/'P7_HOST_03/protected_before.json')
    protected_now=[snapshot(r['path']) for r in protected]
    check('actual five protected files unchanged from retained runtime baseline',protected_now==protected and sum(len(r['files']) for r in protected_now)==5)
    offline=read(E/'OFFLINE04/RESULT.json')
    expected=len(read(ROOT/'experiments/pse_together/tools/expected_suites.json')['suites'])
    check('complete fresh offline manifest passes',offline['status']=='PASS' and offline['passed']==offline['total_expected']==expected)
    for suite in ['p16_respawn_contract','p16_respawn_enet','p16_weapon_contract']:
        row=next(r for r in offline['suites'] if r['name']==suite)
        check(suite+' separately registered and passed',row['verdict']=='PASS')
    restores={};builds={}
    for role in ['host','client']:
        run=p7/f'P7_{role.upper()}_03'
        rows=[json.loads(line) for line in (run/'native.jsonl').read_text(encoding='utf8').splitlines()]
        controls=[r for r in rows if r['event'] in ['control_restore','p4_control_restore','client_control_restored','client_update_restored']]
        expected={'Getter','Commands','Turning','Update'} if role=='host' else {'Commands','Turning','Update'}
        check(role+' retained P7 controls exact16, unique expected sites',bool(controls) and {r.get('site','Update') for r in controls}==expected and all(r['exact16'] for r in controls))
        native=[r for r in rows if r['event']=='p7_native_hook_restore']
        if role=='client':check('client retained P7 all fourteen native hook restores exact16',len(native)==14 and all(r['exact16'] and r['storage_pinned'] for r in native))
        check(role+' retained P7 normal process exit0',read(run/'exit.json')['exit_code']==0)
        restores[role]=dict(source=str(run.relative_to(ROOT)),controls=controls,native=native,note='Retained accepted P7; NOT a P16 game run')
        cfg=read(run/'NATIVE_ARTIFACT.json')['config']
        name='pse_m11_bootstrap' if role=='host' else 'pse_native_client58b'
        build=dll_compare.compare(cfg['dll'],ROOT/f'b87/dll/pse_dll_{role}_p7/{name}.dll')
        builds[role]=build
        check(role+' accepted P7 payload unaffected by P16 policy additions',build['all_sections_equal_masked'] and build['exports_equal'])
    protocol=[]
    for gate in [p7/'PRE_NATIVE_RUN03.json',ROOT/'docs/evidence/m1/P23_NATIVE/PRE_NATIVE05.json']:
        for row in read(gate)['source_copies']:
            source=row.get('source','')
            if any('/src/'+part+'/' in source for part in ['wire111','wire112','wire113']):
                p=ROOT/source;protocol.append(dict(path=source,passed=p.is_file() and sha(p)==row['sha256']))
    check('retained v111 v112 v113 source snapshots still match',bool(protocol) and all(r['passed'] for r in protocol))
    check('frozen v110 header exact SHA',sha(ROOT/'tools/enet-prototype/include/net_protocol.h')=='8272aa3ef7bcd9e555e8a7b6bc12a1fe320b378dc34c8edbe6b38891e3bbac0f')
    query="@(Get-CimInstance Win32_Process | Where-Object { $_.Name -in @('anubis_Shipping_Steam_x64.exe','p16_respawn_enet.exe','p16_respawn_contract.exe') } | Select-Object ProcessId,Name,ExecutablePath) | ConvertTo-Json -Compress"
    process=subprocess.run(['powershell','-NoProfile','-Command',query],capture_output=True,text=True,timeout=30)
    processes=json.loads(process.stdout) if process.stdout.strip() else []
    check('no game or P16 diagnostic process remains',process.returncode==0 and not processes)
    sources={str(p.relative_to(ROOT)):sha(p) for p in [ROOT/'experiments/pse_together/src/session/respawn_p16.hpp',
        ROOT/'experiments/pse_together/tests/session/respawn_p16.cpp',ROOT/'experiments/pse_together/tests/session/respawn_enet_p16.cpp',
        ROOT/'experiments/pse_together/src/native/host_p16.cpp',ROOT/'experiments/pse_together/src/native/weapon_pickup_p16.cpp',
        ROOT/'experiments/pse_together/src/native/weapon_pickup_p16.hpp',ROOT/'experiments/pse_together/tests/native/weapon_pickup_p16.cpp',
        ROOT/'experiments/pse_together/tools/audit_p16.py',ROOT/'experiments/pse_together/CMakeLists.txt',ROOT/'experiments/pse_together/tools/expected_suites.json']}
    result=dict(status='PASS' if all(r['passed'] for r in checks) else 'FAIL',package_status='PARTIAL',
        utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=sum(r['passed'] for r in checks),total=len(checks),checks=checks,
        original77_and_copies=files,protected=protected_now,retained_restores=restores,accepted_P7_builds=builds,protocol_sources=protocol,
        processes=processes,sources=sources,game_launched=False,frida_sessions_created=0,new_hooks_installed=0,
        limitations=['P16 respawn policy is not connected to native host/client DLLs; isolated host candidate contains only the persistent weapon seam.',
                     'Native E respawn, TPK and BLK-11 reacquisition are NOT accepted.',
                     'ENet native completion is a fixture. Retained P7 restores are reused evidence only.'])
    (out/'RESULT.json').write_text(json.dumps(result,indent=2,ensure_ascii=False),encoding='utf8')
    print(result['status'],result['passed'],'/',result['total'],'checks; PACKAGE PARTIAL, NOT KEX')
    for row in checks:
        if not row['passed']:print('FAIL',row['name'])
    return 0 if result['status']=='PASS' else 1

if __name__=='__main__':raise SystemExit(main())
