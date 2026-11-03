"""Package source review/diff and validate DOC-44's local Evidence links. File-only."""
from pathlib import Path
import datetime, difflib, hashlib, json, re, shutil
LAB=Path(__file__).resolve().parent
ROOT=LAB.parents[2]
OUT=ROOT/'docs/evidence/m1/M12A_IMPLEMENTATION_20261102'
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    selections={
        'tools/enet-prototype/include/net_protocol.h':[(15,25),(148,156),(201,209),(307,326),(330,395),(452,495)],
        'tools/enet-prototype/src/enet_worker.cpp':[(35,62),(76,86),(204,251),(293,326),(335,365),(569,611)],
        'tools/enet-prototype/src/enet_host.cpp':[(190,215),(295,332)],
        'tools/enet-prototype/src/enet_client.cpp':[(145,165),(246,266),(337,365)],
        'tools/m12a-test-client/src/m12a_client.cpp':[(109,162),(242,267)],
        'tools/m12a-test-client/src/m12a_scenarios.cpp':[(292,318),(346,404)],
        'tools/m12a-adversarial-qa/src/main.cpp':[(565,620)],
        'tools/multiclient-test-runner/kex_adapter.py':[(196,217),(660,724)],
        'experiments/m1/native_bootstrap/src/bootstrap.cpp':[(144,160),(178,214),(232,247),(333,382)],
        'experiments/m1/network_input/src/network_input.cpp':[(11,33),(35,73),(75,137),(138,171)],
    }
    records=[]
    for relative,ranges in selections.items():
        p=ROOT/relative; lines=p.read_text(encoding='utf8').splitlines()
        records.append(dict(path=relative,sha256=digest(p),excerpts=[dict(first_line=a,last_line=min(b,len(lines)),text='\n'.join(f'{i+1}: {lines[i]}' for i in range(a-1,min(b,len(lines))))) for a,b in ranges]))
    baseline=json.loads((ROOT/'docs/evidence/m1/M11_IMPLEMENTATION_20261101/BASELINE.json').read_text(encoding='utf8'))['files']
    expected_protocol=next(r['sha256'] for r in baseline if r['path'].replace('\\','/')=='tools/enet-prototype/include/net_protocol.h')
    assert digest(ROOT/'tools/enet-prototype/include/net_protocol.h')==expected_protocol
    audit=dict(evidence_type='VERIFIED static source observations; runtime proof linked separately in DOC-44',sources=records,
               unchanged_protocol_sha256=digest(ROOT/'tools/enet-prototype/include/net_protocol.h'),
               native_binary_sha256='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d',
               native_contracts='Reused unchanged DOC-41 coordinates and M0/M1.1 ABI/lifecycle; no new binary analysis',
               findings=['v1.10 serializers include HELLO/WELCOME reconnectKey despite omissions in prose; input payload34+header11 bytes.',
                         'Gemini client dispatch journal is not host arrival/native motion; legacy MAP_READY is not asset-load evidence.',
                         'NET-14 fixed ADV09 branch is present, but generic REJECT/fresh-session fallbacks are broader than a strict hijack proof.',
                         'Adversarial runner main returns0 regardless of printed scorecard; exit0 cannot certify scorecard.',
                         'Gemini KEX regex/marker gate is supplementary; it cannot authenticate native provenance by itself.',
                         'Real ENet generation reproducer failed before minimal guard and passed after: generation rejected before seq/edge mailbox mutation.',
                         'New Session owns critical FIFO retry and terminal reconnect resync; movement/neutral input is applied only by native simulation thread.',
                         'Session object is process-retained via trivial Context pointer; worker resources are explicitly stopped/joined before native unbind.',
                         'Headless control domain/empty baseline is not world replication or proof of a client KEX level.',
                         'General channel/header-version validation, malicious traffic, new participants/slots, long sessions are not accepted by this bounded runtime.'])
    (OUT/'SOURCE_REVIEW.json').write_text(json.dumps(audit,indent=2),encoding='utf8')
    changes=[]
    for old,relative in [('bootstrap_before.cpp','experiments/m1/native_bootstrap/src/bootstrap.cpp'),('enet_worker_before.cpp','tools/enet-prototype/src/enet_worker.cpp')]:
        changes+=list(difflib.unified_diff((OUT/old).read_text(encoding='utf8').splitlines(True),(ROOT/relative).read_text(encoding='utf8').splitlines(True),fromfile=relative+' (before)',tofile=relative))
    (OUT/'SOURCE_CHANGES.patch').write_text(''.join(changes),encoding='utf8')
    run1=ROOT/'docs/evidence/m1/M12A_NATIVE_01'
    # Preserve exactly the first OS source, including its initial coverage gap.
    initial=(LAB/'network_os_probe.js').read_text(encoding='utf8').replace("['recvfrom','sendto','WSARecvFrom','WSASendTo']","['recvfrom','sendto']")
    initial_path=run1/'network_os_probe_loaded.js'
    initial_path.write_bytes(initial.encode('utf8'))
    expected=json.loads((run1/'load_network_os.json').read_text(encoding='utf8'))['structuredContent']['normalized_result']['source_sha256']
    assert digest(initial_path)==expected
    shutil.copy2(ROOT/'docs/evidence/m1/M11_IMPLEMENTATION_20261101/NATIVE_COORDINATES.json',OUT/'REUSED_NATIVE_COORDINATES.json')
    for name in ['PROGRESS.md','EVIDENCE_INDEX.md']:
        dest=OUT/(name+'.before44')
        if not dest.exists(): shutil.copy2(ROOT/'docs'/name,dest)
    doc=ROOT/'docs/44_M1_FIRST_NETWORK_INPUT.md'
    missing=[]
    for target in re.findall(r'\]\(([^)]+)\)',doc.read_text(encoding='utf8')):
        if not target.startswith(('http:','https:')) and not (doc.parent/target.split('#')[0]).exists(): missing.append(target)
    record=dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),missing_local_DOC44_links=missing,initial_OS_probe_sha256=expected,protocol_unchanged=True)
    (OUT/'DOCUMENT_CHECK.json').write_text(json.dumps(record,indent=2),encoding='utf8')
    print(json.dumps(record)); assert not missing
if __name__=='__main__': main()
