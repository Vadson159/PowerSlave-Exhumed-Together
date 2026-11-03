"""Seal DOC57 continuation without altering historical or production files."""
from workflow import *
import difflib,re
run=OUT/'N57_CONT_01'
r=load(OUT/'NATIVE_REVIEW.json');assert r['positive_checks']==23 and all(c['passed'] for c in r['checks'])
assert r['total_verified_lower_bound']==597 and r['post_retire_distinct_world_steps']==1920
assert r['os_exit']['exit_code']==0 and load(OUT/'A_HARNESS_GATE.json')['status']=='PASS'
for p in LAB.glob('*.py'):compile(p.read_text(encoding='utf8'),str(p),'exec')
doc=ROOT/'docs/57_NATIVE_VISUAL_PROXY_CONTINUATION.md'
assert '**Общий статус: PARTIAL.' in doc.read_text(encoding='utf8')
summary=dict(status='PARTIAL',native_visual_lifecycle_subexperiment='bounded PASS',date_utc='2026-11-03',run='N57_CONT_01',pid=29204,simulation_tid=18952,
    actor_type=1007,actor_name='Obj_Ramses_Body_Rest',actor='0x61aa4310',node='0x61a7b7e0',sector=259,position=[-7424,-6880,1240],flags=64,
    nested_listener_fix='VERIFIED FIXTURE and direct native return; missing listener never treated as null',factory_abi='Win64 RCX/EDX/R8/XMM3/entry_stack+28, RAX Actor*',
    visible=True,visual_proof='actual owned game-window before/live/moved/after images plus genuine1eb650 sprite/55f40 vertex submissions',
    simulation_steps_before_guard=585,actor_tick_callbacks_before_guard=586,cleanup_world_steps=12,cleanup_actor_callbacks=12,verified_simulation_minimum=597,exact_total_lifetime='UNKNOWN',
    p1_ownership_unchanged=True,p1_motion_before_and_after_remove=True,health_weapon_ammo_preserved=True,extended_inventory_preserved='UNKNOWN',
    remove_calls=1,deferred_retirement='VERIFIED NATIVE: queue refs1->0, sector unlink, OnRemove, Actor pool-return, ObjectNode payloadnull/in_use0/refs0 in same sweep; pointers invalidated',
    general_destructor_completion='UNKNOWN; no delete/free/destructor call made by probe',clean_os_exit=0,post_retire_measured_steps=1920,
    remaining_critical_gate='Full inventory/native story fields were not sampled before/after; artifacts/abilities/keys/questItems/teamDolls/ankh UNKNOWN',
    other_limits=['Continuous counting interrupted by constant-sector-count observer error; gap not assigned ticks','Whole post-run corrected probe not native replayed','Other maps/types/transitions/reconnect/network position updates untested'],
    production_regressions_rerun=False,checks=['Actual benign Frida nested fixture A02','JS/Python syntax','Cleanup-bound syntax','Nine captured-native membership cases','23 native Evidence consistency checks','16579-file historical and game/profile integrity'],
    production_cpp_dll_changes=False,new_injector=False,all_task_paths_on_s=True,historical_blocked_preserved=True,doc58_ready=False,doc58_started=False,stop_after_doc57=True)
dump(OUT/'RESULT_SUMMARY.json',summary)
before=set(OUT.glob('INTEGRITY_*.json'));integrity();fresh=set(OUT.glob('INTEGRITY_*.json'))-before;assert len(fresh)==1
i=load(fresh.pop());assert i['status']=='PASS' and sorted(i['changed'])==['docs/EVIDENCE_INDEX.md','docs/PROGRESS.md']
dump(OUT/'INTEGRITY_FINAL.json',i)
sources=sorted(p for p in LAB.iterdir() if p.is_file())+[doc]
patch=[]
for name in ['PROGRESS.md','EVIDENCE_INDEX.md']:
    old=(OUT/(name+'.before')).read_text(encoding='utf8').splitlines(keepends=True);new=(ROOT/'docs'/name).read_text(encoding='utf8').splitlines(keepends=True)
    patch.extend(difflib.unified_diff(old,new,fromfile='a/docs/'+name,tofile='b/docs/'+name))
for p in sources:
    rel=p.relative_to(ROOT).as_posix();patch.extend(difflib.unified_diff([],p.read_text(encoding='utf8').splitlines(keepends=True),fromfile='/dev/null',tofile='b/'+rel))
with (OUT/'SOURCE_CHANGES.patch').open('x',encoding='utf8') as f:f.write(''.join(patch))
paths=sources+[ROOT/'docs/PROGRESS.md',ROOT/'docs/EVIDENCE_INDEX.md']
paths+=list(p for p in OUT.rglob('*') if p.is_file())
paths+=list(p for p in (PARENT/'profile/N57_CONT_01').rglob('*') if p.is_file())
paths=sorted(set(paths));assert all(inside(p).drive.upper()=='S:' for p in paths)
files=[dict(path=str(p),relative=p.relative_to(ROOT).as_posix(),size=p.stat().st_size,sha256=sha(p)) for p in paths]
scratch=[dict(path=str(p),size=p.stat().st_size) for p in SCRATCH.rglob('*') if p.is_file()]
assert all(inside(p['path']).drive.upper()=='S:' for p in scratch)
dump(OUT/'FILES_FINAL.json',dict(files=files,scratch_files=scratch,scope='New continuation sources/Evidence/profile/addendum and two changed indexes. Scratch cache inventory included without hashing caches. Manifest excludes itself.',all_paths_on_s=True,production_cpp_dll_changes=False))
missing=[]
for p in [doc,LAB/'README.md']:
    for link in re.findall(r'\[[^\]]*\]\(([^)]+)\)',p.read_text(encoding='utf8')):
        if '://' in link or link.startswith('#'):continue
        q=Path(link) if Path(link).is_absolute() else p.parent/link
        if not q.exists():missing.append(dict(document=str(p),link=link))
dump(OUT/'LINK_CHECK.json',dict(status='PASS' if not missing else 'FAIL',missing=missing))
assert not missing,missing
print('SEALED overall PARTIAL; native visual/lifecycle bounded PASS;',len(files),'task files; all S:; no DOC58',flush=True)
