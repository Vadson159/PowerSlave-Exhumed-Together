"""DOC56 freeze: source diff vs baseline, new-file hashes, artifacts, autonomous gate. No game start."""
from pathlib import Path
import difflib,json,shutil,sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from workflow import LAB,ROOT,OUT,sha,dump,stamp,game_check,TARGETS
CHANGED=['experiments/m1/native_bootstrap/src/bootstrap.cpp','experiments/m1/native_second_instance/src/client.cpp',
         'experiments/m1/network_input/src/network_input.cpp','experiments/m1/network_input/src/network_input.hpp']
def main():
    base=json.loads((OUT/'BASELINE.json').read_text(encoding='utf8'))
    changed=[r['path'] for r in base['files'] if not (ROOT/r['path']).is_file() or sha(ROOT/r['path'])!=r['sha256']]
    assert sorted(changed)==sorted(CHANGED),changed
    patch=[]
    for rel in CHANGED:
        patch+=difflib.unified_diff((OUT/'before'/rel).read_text(encoding='utf8').splitlines(True),(ROOT/rel).read_text(encoding='utf8').splitlines(True),fromfile='before/'+rel,tofile=rel)
    (OUT/'SOURCE_CHANGES.patch').write_text(''.join(patch),encoding='utf8')
    new=[p for p in sorted(LAB.rglob('*')) if p.is_file() and not {'build','__pycache__'}.intersection(p.relative_to(LAB).parts)]
    artifacts=[]
    for name in ['pse_m11_bootstrap.dll','pse_m11_bootstrap.pdb','pse_native_client.dll','pse_native_client.pdb','map_ready_contract.exe']:
        dst=OUT/'artifacts'/name;dst.parent.mkdir(exist_ok=True);shutil.copy2(LAB/'build/Release'/name,dst)
        artifacts.append(dict(path=dst.relative_to(ROOT).as_posix(),sha256=sha(dst),size=dst.stat().st_size))
    rows=[]
    for name in TARGETS[2:]:
        folder=sorted(OUT.glob('TEST_'+name+'_*'))[-1];o=json.loads((folder/'RESULT.json').read_text(encoding='utf8'))
        assert o['exit_code']==0,(name,o);rows.append(o)
    wrong=json.loads((OUT/'WRONG_PROCESS_REJECTION.json').read_text(encoding='utf8'))
    assert all(r['return_code']==193 and not r['log_created'] for r in wrong['results'].values())
    g=game_check();assert not g['errors'] and not g['client_copy_errors'] and g['original_full_snapshot_equal'] and g['protected']==base['game']['protected']
    wire=ROOT/'tools/enet-prototype/include/net_protocol.h';assert sha(wire)=='8272aa3ef7bcd9e555e8a7b6bc12a1fe320b378dc34c8edbe6b38891e3bbac0f'
    dump(OUT/'BUILD_MANIFEST.json',dict(utc=stamp(),changed_existing=[dict(path=r,sha256=sha(ROOT/r)) for r in CHANGED],
        new_files=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in new],artifacts=artifacts,
        defines=dict(pse_m12a=['PSE_M12A','PSE_DOC55_SNAPSHOT','PSE_DOC56_MAPREADY'],pse_native_client=['PSE_DOC55_SNAPSHOT','PSE_DOC56_MAPREADY']),
        wire_header=dict(path=wire.relative_to(ROOT).as_posix(),sha256=sha(wire),protocol='v1.10 unchanged'),
        exe_sha256='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'))
    dump(OUT/'AUTONOMOUS_GATE.json',dict(status='PASS',scope='Offline/source/loopback ENet only; NOT native KEX acceptance',
        regressions=[dict(target=Path(r['argv'][0]).stem,exit_code=r['exit_code'],log=r['log'],binary_sha256=r.get('binary_sha256')) for r in rows],
        wrong_process=wrong,game=g,manifest_sha256=sha(OUT/'BUILD_MANIFEST.json')))
    print('FROZEN',len(new),'new files; gate PASS')
if __name__=='__main__':main()
