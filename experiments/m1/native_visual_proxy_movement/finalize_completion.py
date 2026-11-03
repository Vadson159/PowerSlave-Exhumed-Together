"""DOC58A completion: post-run offline/syntax rerun, source manifest and changed-file list (no KEX)."""
from workflow import *
mode=sys.argv[1]
if mode=='postrun':
    command(['C:/Program Files/nodejs/node.exe',LAB/'offline_test.js'],'OFFLINE_POSTRUN')
    syntax()
    run=OUT/'N58A_C01';m=load(run/'PROBE_MANIFEST.json');backup=SCRATCH
    executed={r['path']:r['sha256'] for r in m['sources']}
    current=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p),executed_in_kex_sha256=executed.get(p.relative_to(ROOT).as_posix()),
        changed_after_native_run=(p.relative_to(ROOT).as_posix() in executed and executed[p.relative_to(ROOT).as_posix()]!=sha(p)),
        pre_task_sha256=sha(backup/p.name) if (backup/p.name).exists() else None) for p in sorted(LAB.iterdir()) if p.suffix in ['.py','.js']]
    dump(OUT/'SOURCE_MANIFEST.json',dict(binary_sha256=EXPECTED,executed_bound_probe=dict(path=(run/'probe_bound.js').relative_to(ROOT).as_posix(),sha256=sha(run/'probe_bound.js'),source_sha256=m['config']['source_sha256']),
        pre_native_gate_sha256=sha(OUT/'PRE_NATIVE_GATE.json'),sources=current,
        note='executed_in_kex_sha256 comes from PROBE_MANIFEST at bind time. probe.js changed after the run (command-4 target selection fix): offline/syntax only, NOT native-verified.'))
    print('POSTRUN DONE')
elif mode=='changed':
    lab=[p for p in sorted(LAB.iterdir()) if p.is_file() and p.suffix in ['.py','.js','.md']]
    # Pre-task state = frozen historical DOC58A manifest (all lab sources at the end of the PARTIAL task).
    hist={r['relative']:r['sha256'] for r in load(HISTORICAL_58A/'SOURCE_MANIFEST.json')['files']}
    files=[]
    for p in lab:
        old=hist.get(p.relative_to(ROOT).as_posix())
        state='new' if old is None else ('modified' if old!=sha(p) else 'unchanged')
        if state!='unchanged':files.append(dict(path=str(p),state=state,sha256=sha(p)))
    for p in sorted(OUT.rglob('*')):
        if p.is_file():files.append(dict(path=str(p),state='new_evidence',sha256=sha(p)))
    for p in [ROOT/'docs/58A_NATIVE_VISUAL_PROXY_COMPLETION.md',ROOT/'docs/PROGRESS.md',ROOT/'docs/EVIDENCE_INDEX.md']:
        files.append(dict(path=str(p),state='new' if p.name.startswith('58A_NATIVE_VISUAL_PROXY_COMPLETION') else 'modified_current_index',sha256=sha(p)))
    other=[str(p) for p in [PARENT/'profile/N58A_C01',SCRATCH]]
    assert all(Path(f['path']).resolve().is_relative_to(ROOT) for f in files)
    dump(OUT/('CHANGED_FILES_'+stamp()+'.json'),dict(all_on_s=True,root=str(ROOT),files=[f for f in files if not f['path'].endswith(('CHANGED_FILES.json',))],directories_created=other,
        production_changed=False,historical_docs_changed=False,note='Task-local profile and scratch directories listed as directories; files under them are run/test data.'))
    print('CHANGED',len(files))
