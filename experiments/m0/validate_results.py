"""Offline integrity/coverage validation. Never runs or loads the game."""
import datetime,hashlib,json,re,sys
from pathlib import Path
from prepare import ROOT,OUT,COPY,EXPECTED,digest,snapshot

def main():
    errors=[];prep=json.loads((OUT/'M0_001_PREPARATION.json').read_text());source=Path(prep['source'])
    (OUT/'M0_VALIDATION.json').write_text(json.dumps({'status':'RUNNING','scope':'Offline integrity validation only'}),encoding='utf8')
    original=snapshot(source);original_ok=original==prep['original_installation_before']
    if not original_ok:errors.append('Original installation snapshot changed')
    copy_errors=[r['relative_path'] for r in prep['independent_copy_files'] if digest(COPY/r['relative_path'])!=r['sha256'] or (source/r['relative_path']).samefile(COPY/r['relative_path'])]
    if copy_errors:errors.append('Copy identity/content mismatch')
    runs={}
    for run in sorted(p for p in OUT.iterdir() if p.is_dir() and (p/'launch.json').exists()):
        name=run.name;before=json.loads((run/'protected_before.json').read_text());after=json.loads((run/'protected_after.json').read_text())
        current=[snapshot(Path(p['path'])) for p in before];equal=before==after==current
        runs[name]={'profiles_unchanged':equal,'exit':json.loads((run/'exit.json').read_text())}
        if not equal:errors.append(name+' protected profiles changed')
    parsed=0;files=[]
    for root in [OUT,ROOT/'docs/evidence/m0_static',ROOT/'docs/evidence/m0_static_lifetime',ROOT/'docs/evidence/m0_static_host_death',ROOT/'docs/evidence/m0_static_respawn',ROOT/'docs/evidence/m0_static_physics',ROOT/'docs/evidence/m0_static_final_acceptance']:
        for p in sorted(root.rglob('*')):
            if not p.is_file() or p.name in ['M0_VALIDATION.json','M0_FILES.json']:continue
            if p.suffix=='.json':json.loads(p.read_text(encoding='utf-8-sig'));parsed+=1
            files.append({'path':str(p.relative_to(ROOT)).replace('\\','/'),'bytes':p.stat().st_size,'sha256':digest(p)})
    for p in sorted((ROOT/'experiments/m0').glob('*')):
        if p.is_file():files.append({'path':str(p.relative_to(ROOT)).replace('\\','/'),'bytes':p.stat().st_size,'sha256':digest(p)})
    (OUT/'M0_FILES.json').write_text(json.dumps({'files':files,'note':'Captured runtime/static evidence and top-level experiment sources; self/validation excluded'},indent=2),encoding='utf8')
    link_count=0
    summaries=[ROOT/'docs'/name for name in ['M0_EXPERIMENTS.md','PROGRESS.md','EVIDENCE_INDEX.md','27_M0_COMPLETION_REPORT.md','M0_TO_M1_HANDOFF.md'] if (ROOT/'docs'/name).exists()]
    snapshot_root=OUT/'M0_FINAL_ACCEPTANCE_20261101'
    snapshot_manifest=snapshot_root/'BASELINE.json'
    snapshot_files={Path(row['path']):row['sha256'] for row in json.loads(snapshot_manifest.read_text())['files']} if snapshot_manifest.exists() else {}
    for p in list(OUT.rglob('*.md'))+summaries:
        link_base=p.parent
        # Archived documents retain their exact original bytes/relative links.
        # Resolve those links in the recorded source context after hash checking
        # the backup, rather than rewriting or silently excluding the snapshot.
        if p.is_relative_to(snapshot_root):
            original_relative=p.relative_to(snapshot_root)
            if snapshot_files.get(original_relative)!=digest(p):errors.append('Snapshot document digest mismatch: '+str(p.relative_to(ROOT)))
            else:link_base=(ROOT/original_relative).parent
        s=re.sub(r'```.*?```','',p.read_text(encoding='utf8'),flags=re.S)
        for match in re.finditer(r'\[[^\]\n]+\]\(([^)\n]+)\)',s):
            dest=match.group(1)
            if re.match(r'^(https?://|#)',dest):continue
            link_count+=1
            if not (link_base/dest.split('#')[0]).exists():errors.append('Broken link '+str(p.relative_to(ROOT))+' -> '+dest)
    # Use the newest recorded game run, never silently report a historical C05 session as current.
    launched=[p for p in OUT.iterdir() if p.is_dir() and (p/'launch.json').exists()]
    newest=max(launched,key=lambda p:json.loads((p/'launch.json').read_text())['started_utc'])
    launch=json.loads((newest/'launch.json').read_text())
    candidates=[]
    for path in newest.glob('status_*.json'):
        saved=json.loads(path.read_text(encoding='utf-8-sig')).get('structuredContent',{}).get('normalized_result',{})
        if saved.get('pid')==launch['pid'] and saved.get('state')=='detached':candidates.append(path)
    if not candidates:raise RuntimeError('Latest run has no saved detached Frida status: '+newest.name)
    latest=max(candidates,key=lambda p:p.stat().st_mtime_ns)
    final=json.loads(latest.read_text())['structuredContent']['normalized_result']
    if final['state']!='detached':errors.append('Final Frida status not detached')
    # Formal native acceptance is a separately reviewed manifest tied to runtime
    # Evidence hashes. Artifact integrity or compilation alone never accepts M0.
    acceptance_path=OUT/'M0_ACCEPTANCE.json';acceptance=None;acceptance_valid=False
    if acceptance_path.exists():
        acceptance=json.loads(acceptance_path.read_text(encoding='utf8'))
        acceptance_errors=[]
        if acceptance.get('status')!='PASS' or acceptance.get('binary_sha256')!=EXPECTED:acceptance_errors.append('Acceptance status/build mismatch')
        if not acceptance.get('mandatory_contracts') or not all(c.get('status')=='PASS' for c in acceptance['mandatory_contracts']):acceptance_errors.append('Mandatory native contract not accepted')
        for record in acceptance.get('evidence_files',[]):
            path=(ROOT/record['path']).resolve()
            if not path.is_relative_to(OUT.resolve()) or not path.is_file() or digest(path)!=record['sha256']:acceptance_errors.append('Acceptance Evidence mismatch: '+record['path'])
        if not acceptance.get('evidence_files'):acceptance_errors.append('Missing native acceptance Evidence')
        for summary in acceptance.get('required_runtime_summaries',[]):
            data=json.loads((ROOT/summary).read_text(encoding='utf8'))
            if data.get('status')!='PASS' or not data.get('checks') or not all(data['checks'].values()):acceptance_errors.append('Required native runtime summary not PASS: '+summary)
        if not acceptance.get('required_runtime_summaries'):acceptance_errors.append('Missing required runtime summaries')
        errors.extend(acceptance_errors);acceptance_valid=not acceptance_errors
    result={'captured_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'status':'PASS' if not errors else 'FAIL',
      'scope':'Artifact integrity/observed coverage only; does NOT accept M0 gameplay',
      'binary_sha256':EXPECTED,'original_installation_files':len(original['files']),'original_installation_unchanged':original_ok,
      'independent_copy_files':len(prep['independent_copy_files']),'copy_errors':copy_errors,'runs':runs,
      'parsed_json':parsed,'indexed_files':len(files),'local_links_checked':link_count,'final_frida_state':final['state'],'final_frida_evidence':str(latest.relative_to(ROOT)),'errors':errors,
      'm0_accepted':acceptance_valid and not errors,'m0_acceptance_manifest':str(acceptance_path.relative_to(ROOT)) if acceptance else None,
      'm0_acceptance_scope':acceptance.get('scope') if acceptance else None,
      'filesystem_isolation_status':{'historical_C05':'FAIL: Steam overlay/cache outside writes retained as evidence',
        'latest_run':newest.name,'bounded_api_gate':json.loads((newest/'ISOLATION_GATE.json').read_text())['status'] if (newest/'ISOLATION_GATE.json').exists() else 'UNKNOWN',
        'limitations':'Guarded write-open observation is not a complete OS filesystem sandbox. C09 found shared-copy campaign paths; inspect latest run profile evidence.'},
      'limitations':['A01/A02 early native logs escaped workspace into sandbox-account Saved Games; documented, not erased.',
       'CreateFileW/KnownFolder observation is not a complete OS filesystem audit.',
       'C01/C4 never reached Spawn; old root cause remains unknown. C05 Default desktop/user-active run achieved one real enemy hit.',
       'Remaining references with live or pooled native holders are pending; immediate refs0 is not required. C13 covered a holder-free controlled death/recreate only.',
       'C05 native auxiliary Steam overlay/cache files were opened for writing outside workspace. Protected save snapshots do not cover all service files.']}
    (OUT/'M0_VALIDATION.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    print(json.dumps(result));sys.exit(bool(errors))

if __name__=='__main__':main()
