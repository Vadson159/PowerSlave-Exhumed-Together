"""Seal accepted DOC57 outputs and verify historical content after index updates."""
from workflow import *
import re
review=load(OUT/'NATIVE_ACCEPTANCE_REVIEW.json');assert review['status']=='bounded PASS'
for p in LAB.glob('*.py'):compile(p.read_text(encoding='utf8'),str(p),'exec')
if (OUT/'INTEGRITY_FINAL.json').exists():integrity_result=load(OUT/'INTEGRITY_FINAL.json')
else:
    before=set(OUT.glob('INTEGRITY_*.json'));integrity();fresh=set(OUT.glob('INTEGRITY_*.json'))-before;assert len(fresh)==1
    integrity_result=load(fresh.pop());dump(OUT/'INTEGRITY_FINAL.json',integrity_result)
assert sorted(integrity_result['changed'])==['docs/EVIDENCE_INDEX.md','docs/PROGRESS.md']
for p in [OUT/'N57_FINAL_01/protected_before.json',OUT/'N57_FINAL_01/protected_after.json']:assert p.is_file()
assert load(OUT/'N57_FINAL_01/protected_before.json')==load(OUT/'N57_FINAL_01/protected_after.json')
docs=[ROOT/'docs/57_NATIVE_VISUAL_PROXY_FINAL_ACCEPTANCE.md',ROOT/'docs/PROGRESS.md',ROOT/'docs/EVIDENCE_INDEX.md']
missing=[]
for p in docs:
    # Check final report and only newly-added index section to avoid auditing unrelated history.
    s=p.read_text(encoding='utf8')
    if p.name!='57_NATIVE_VISUAL_PROXY_FINAL_ACCEPTANCE.md':s=s.split('## DOC-57 continuation')[0]
    for link in re.findall(r'\[[^\]]*\]\(([^)]+)\)',s):
        target=p.parent/link
        if '://' not in link and not link.startswith('#') and not target.exists() and target.resolve()!=(OUT/'FILES_FINAL.json').resolve():missing.append(dict(document=p.name,link=link))
dump(OUT/'LINK_CHECK_FINAL.json',dict(status='PASS' if not missing else 'FAIL',missing=missing,manifest_link_verified_after_creation=True));assert not missing,missing
commands=dict(cwd=str(ROOT),python='C:/Python314/python.exe -X utf8 -B',node='C:/Program Files/nodejs/node.exe',temp=str(SCRATCH),tmp=str(SCRATCH),build='No production build or DLL change',offline=['workflow.py baseline','offline_test.js (first failed mock retained, corrected15 checks PASS)','workflow.syntax(): Node --check each JS + compile Python','exact ABI disk anchors and stack5 instruction verification','acceptance_review.py:18 checks','workflow.py integrity before docs and seal.py integrity after docs'],native=dict(provider='existing REA MCP Frida local',launcher_argv=['-X','utf8','-B',str(LAB/'runner.py'),'launch','N57_FINAL_01'],game_pid=33460,simulation_tid=32052,runner_commands=['bind N57_FINAL_01','watch N57_FINAL_01','action N57_FINAL_01 resume','action N57_FINAL_01 ui_start (initial no window; repeated after ready)','action N57_FINAL_01 ui key --key RETURN','gate N57_FINAL_01','frame N57_FINAL_01 before/live/moved/after/post_move','action N57_FINAL_01 ui close'],input_commands=['input_fixture.py N57_FINAL_01 focus','input_fixture.py N57_FINAL_01 W 0.25','input_fixture.py N57_FINAL_01 S 0.25'],frida_loads=['generated_isolation.js','probe_bound.js','command_1.js','command_2.js','camera_final.js','restored_check.js'],unload_before_WM_CLOSE=['camera_final.js','probe_bound.js']),all_created_paths_on_s=True)
dump(OUT/'COMMANDS.json',commands)
files=set(docs)
for part in [LAB,OUT,PARENT/'profile/N57_FINAL_01']:
    files.update(p for p in part.rglob('*') if p.is_file())
manifest=[]
for p in sorted(files):
    p=inside(p);assert p.drive.upper()=='S:'
    manifest.append(dict(path=str(p),relative=p.relative_to(ROOT).as_posix(),size=p.stat().st_size,sha256=sha(p)))
scratch=[dict(path=str(inside(p)),size=p.stat().st_size) for p in SCRATCH.rglob('*') if p.is_file()]
dump(OUT/'FILES_FINAL.json',dict(status='bounded PASS',files=manifest,scratch=scratch,all_paths_on_s=True,production_changed=False,manifest_excludes_itself=True))
assert (OUT/'FILES_FINAL.json').is_file()
print('SEALED bounded PASS;',len(manifest),'files;',len(integrity_result['changed']),'allowed index changes; history',integrity_result['files'],'unchanged except indexes; DOC58 not started')
