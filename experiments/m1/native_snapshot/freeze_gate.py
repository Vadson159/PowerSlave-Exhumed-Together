"""Freeze exact DOC55 build and gate the single native PoC after actual regressions."""
from pathlib import Path
import ctypes,difflib,json,shutil,subprocess,sys
from workflow import ROOT,LAB,OUT,TARGETS,sha,dump,stamp,game_check
CHANGES=['tools/enet-prototype/include/thread_bridge_dto.h','tools/enet-prototype/include/enet_worker.h','tools/enet-prototype/src/enet_worker.cpp','tools/enet-prototype/src/enet_client.cpp','experiments/m1/network_input/src/network_input.hpp','experiments/m1/network_input/src/network_input.cpp','experiments/m1/native_bootstrap/src/bootstrap.cpp','experiments/m1/native_second_instance/src/client.cpp']
def freeze():
    records=[]
    dirs=['experiments/m1/native_bootstrap','experiments/m1/network_input','experiments/m1/native_second_instance','experiments/m1/native_snapshot','experiments/m1/doc53_followup','tools/enet-prototype']
    for d in dirs:
        for p in sorted((ROOT/d).rglob('*')):
            if not p.is_file() or {'build','profile','game','generated','__pycache__','.git','field-test'}.intersection(p.relative_to(ROOT/d).parts):continue
            if p.suffix not in ['.cpp','.c','.hpp','.h','.txt','.py','.js']:continue
            dst=OUT/'frozen_sources'/p.relative_to(ROOT);dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dst)
            records.append(dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)))
    artifacts=[]
    for name in ['pse_m11_bootstrap.dll','pse_m11_bootstrap.pdb','pse_native_client.dll','pse_native_client.pdb','snapshot_contract.exe']:
        source=LAB/'build/Release'/name;dst=OUT/'artifacts'/name;dst.parent.mkdir(exist_ok=True);shutil.copy2(source,dst)
        artifacts.append(dict(path=dst.relative_to(ROOT).as_posix(),sha256=sha(dst),size=dst.stat().st_size))
    dump(OUT/'BUILD_MANIFEST.json',dict(sources=records,artifacts=artifacts,exe_sha256='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d',protocol_header_sha256=sha(ROOT/'tools/enet-prototype/include/net_protocol.h'),build='VS2022 x64 Release /MT C++20; native /W4 /WX; PSE_DOC55_SNAPSHOT for Host and Client only',changed_existing_sources=CHANGES))
    patch=[]
    for rel in CHANGES:patch+=list(difflib.unified_diff((OUT/'before'/rel).read_text(encoding='utf8').splitlines(True),(ROOT/rel).read_text(encoding='utf8').splitlines(True),fromfile='before55/'+rel,tofile=rel))
    (OUT/'SOURCE_CHANGES.patch').write_text(''.join(patch),encoding='utf8')
    shutil.copy2(ROOT/'docs/evidence/m1/HOST_BACKGROUND_53/NATIVE_COORDINATES.json',OUT/'REUSED_NATIVE_COORDINATES.json')
    dump(OUT/'NATIVE_FIELD_CONTRACT.json',dict(binary_sha256='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d',pointers='runtime module base + stored RVA; no Camera handoff offsets',fields={'Player+0xd8':'ObjectNode handle','Node+0x28':'Puppet payload','Puppet+0x10':'reciprocal node','Puppet+0x1b8':'owner Player','Puppet+0x18/+0x1c/+0x20':'origin XYZ','Puppet+0x30/+0x38':'rotation X/Z mapped to raw native pitch/yaw; no client application','Puppet+0x17a':'int16 health','Puppet+0x3c':'sector'},evidence=['docs/evidence/ghidra_2022/G_FUNC_1401db8a0.json','docs/evidence/ghidra_2022/G_FUNC_1401d4780.json','docs/evidence/m0/C01/RESULT.md','docs/evidence/m1/N53F_HOST_01/native.jsonl'],capture_order='original Player updates and world/Puppet traversal returned; exact native P2 tick matches current StateTick; no extra Update',entity_generation='increment once per successfully validated adapter Player/Puppet binding, independent of connection ledger; unsupported rebind fails closed',map_epoch='actual Host StateTick at attested first-level Session start; agreed over existing Welcome/MapChange/MapReady; fixed Tomb UI/start witness; no general native map-ready/transition contract'))
    print('FROZEN DOC55',len(records),'sources',len(artifacts),'artifacts')
def gate():
    manifest=json.loads((OUT/'BUILD_MANIFEST.json').read_text(encoding='utf8'))
    for r in manifest['sources']:assert sha(ROOT/r['path'])==r['sha256'],r['path']
    assert manifest['protocol_header_sha256']=='8272aa3ef7bcd9e555e8a7b6bc12a1fe320b378dc34c8edbe6b38891e3bbac0f'
    baseline=json.loads((OUT/'BASELINE.json').read_text(encoding='utf8'))
    changed=[r['path'] for r in baseline['files'] if not (ROOT/r['path']).is_file() or sha(ROOT/r['path'])!=r['sha256']]
    assert set(changed)==set(CHANGES),changed
    tests=[]
    for name in TARGETS[2:]:
        folder=sorted(OUT.glob('TEST_'+name+'_*'))[-1];o=json.loads((folder/'RESULT.json').read_text(encoding='utf8'));assert o['exit_code']==0,(name,o)
        o['summary']='\n'.join((folder/'console.log').read_text(encoding='utf8').splitlines()[-7:]);tests.append(o)
    wrong=[]
    for name in ['pse_m11_bootstrap.dll','pse_native_client.dll']:
        lib=ctypes.WinDLL(str(OUT/'artifacts'/name));fn=lib.M11Prepare;fn.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32];fn.restype=ctypes.c_uint32
        path=OUT/(name+'_MUST_NOT_EXIST.jsonl');rc=fn(str(path),ctypes.windll.kernel32.GetCurrentThreadId());assert rc==193 and not path.exists()
        wrong.append(dict(dll=name,rc=rc,no_log=True))
    g=game_check();assert not g['errors'] and not g['client_copy_errors'] and g['original_full_snapshot_equal'] and g['protected']==baseline['game']['protected']
    ps="$p=@(Get-CimInstance Win32_Process -Filter \"Name='anubis_Shipping_Steam_x64.exe'\" | Select-Object ProcessId,ExecutablePath);$u=@(Get-NetUDPEndpoint -ErrorAction SilentlyContinue | Where-Object LocalPort -in @(28745,28855,28856) | Select-Object LocalAddress,LocalPort,OwningProcess);@{processes=$p;udp=$u}|ConvertTo-Json -Depth 4"
    result=subprocess.run(['powershell.exe','-NoProfile','-Command',ps],capture_output=True,encoding='utf8',timeout=30);assert result.returncode==0
    os=json.loads(result.stdout);assert not os['processes'] and not os['udp']
    dump(OUT/'AUTONOMOUS_GATE.json',dict(status='PASS',native_scenarios=1,tests=tests,wrong_process=wrong,game=g,changed_existing=changed,immutable_other_files=len(baseline['files'])-len(changed),os_preflight=os,build_manifest_sha256=sha(OUT/'BUILD_MANIFEST.json'),prior_gate_sha256=sha(ROOT/'docs/evidence/m1/DOC53_FOLLOWUP/DOC55_GATE.json')))
    print('DOC55 AUTONOMOUS GATE PASS')
if __name__=='__main__':{'freeze':freeze,'gate':gate}[sys.argv[1]]()
