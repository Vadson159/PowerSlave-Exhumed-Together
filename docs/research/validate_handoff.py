"""Validate documentation/evidence only. Never executes/loads the target game."""
from pathlib import Path
from urllib.parse import unquote
import datetime, hashlib, json, re
from inventory import PE, sha

ROOT=Path(__file__).resolve().parents[2]
DOC=ROOT/'docs'
E=DOC/'evidence'
EXCLUDE={'EVIDENCE_FILES.json','FINAL_VALIDATION.json'}

def read_json(p):
    b=p.read_bytes()
    return json.loads(b.decode('utf-16') if b.startswith((b'\xff\xfe',b'\xfe\xff')) else b.decode('utf-8-sig'))

def main():
    errors=[];checks={}
    (E/'FINAL_VALIDATION.json').write_text(json.dumps({'status':'RUNNING','scope':'Artifact validation in progress; no target execution'}),encoding='utf8')
    required=['README.md','00_SCOPE.md','01_BINARY_MANIFEST.md','02_ENGINE_ARCHITECTURE.md',
      '03_PLAYER_LIFECYCLE.md','04_ACTORS_AI.md','05_PHYSICS_WEAPONS_DAMAGE.md','06_LEVELS_CAMPAIGN_SAVE.md',
      '07_HOOK_CANDIDATES.md','08_NETWORK_ARCHITECTURE.md','09_POC_PLAN.md','10_OPUS_HANDOFF.md',
      'EVIDENCE_INDEX.md','OPEN_QUESTIONS.md','PROGRESS.md']
    for n in required:
        p=DOC/n
        if not p.is_file() or p.stat().st_size<300:errors.append('Missing/empty document '+n)
    if not (ROOT/'AGENTS.md').is_file():errors.append('AGENTS missing')
    checks['required_documents']=len(required)+1
    alljson={}
    for p in E.rglob('*.json'):
        if p.name in EXCLUDE:continue
        try:alljson[p]=read_json(p)
        except Exception as ex:errors.append('Invalid JSON '+str(p.relative_to(ROOT))+': '+str(ex))
    checks['parsed_evidence_json']=len(alljson)
    # Complete file index. Does not include itself or the validation report to avoid hash cycles.
    files=[]
    for p in sorted(E.rglob('*')):
        if p.is_file() and p.name not in EXCLUDE:
            files.append({'path':str(p.relative_to(DOC)).replace('\\','/'),'bytes':p.stat().st_size,'sha256':sha(p)})
    (E/'EVIDENCE_FILES.json').write_text(json.dumps({'captured_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
      'scope':'All evidence files except this self-index and FINAL_VALIDATION.json; research projects/caches excluded',
      'files':files},ensure_ascii=False,indent=2),encoding='utf8')
    checks['indexed_evidence_files']=len(files)
    # Local links only; no web requests or loading of reference executables.
    links=0
    for p in [ROOT/'AGENTS.md']+list(DOC.glob('*.md'))+list((DOC/'research').glob('*.md')):
        s=p.read_text(encoding='utf-8-sig')
        if '\ufffd' in s:errors.append('Replacement character in '+str(p.relative_to(ROOT)))
        if re.search(r'\?{5,}',s):errors.append('Damaged question-mark text in '+str(p.relative_to(ROOT)))
        clean=re.sub(r'```.*?```','',s,flags=re.S)
        for match in re.finditer(r'\[[^\]\n]+\]\(([^)\n]+)\)',clean):
            dest=match.group(1).strip().strip('<>')
            if re.match(r'^(https?://|mailto:|app://|plugin://|#)',dest):continue
            dest=unquote(dest.split('#',1)[0]);dest=re.sub(r':\d+$','',dest)
            path=Path(dest) if re.match(r'^[A-Za-z]:[/\\]',dest) or dest.startswith('/') else p.parent/dest
            links+=1
            if not path.exists():errors.append('Broken link '+str(p.relative_to(ROOT))+' -> '+dest)
    checks['local_links_checked']=links
    manifest=read_json(E/'E005_BINARY_MANIFEST.json');input_results=[]
    for b in manifest['binaries']:
        actual=sha(Path(b['path']));ok=actual==b['sha256'];input_results.append({'path':b['path'],'sha256':actual,'matches_initial':ok})
        if not ok:errors.append('Binary hash changed '+b['path'])
    k=read_json(E/'E007_KPF_INVENTORY.json');actual=sha(Path(k['path']));ok=actual==k['sha256']
    input_results.append({'path':k['path'],'sha256':actual,'matches_initial':ok})
    if not ok:errors.append('Game KPF hash changed')
    checks['input_binary_and_kpf_hashes']=input_results
    # Verify all native catalog coordinates against original file-backed PE section mapping.
    pe=PE(manifest['binaries'][0]['path']);mainsha=manifest['binaries'][0]['sha256']
    catalog=read_json(E/'FUNCTION_CATALOG.json')['records'];by={r['va']:r for r in catalog}
    for r in catalog:
        p=DOC/r['evidence'];d=read_json(p);rv=int(r['rva'],16)
        if int(r['va'],16)!=pe.base+rv or int(r['file_offset'],16)!=pe.off(rv):errors.append('Coordinate mismatch '+r['id'])
        if r['binary_sha256']!=mainsha or d['binary_sha256']!=mainsha:errors.append('Build mismatch '+r['id'])
        if sha(p)!=r['evidence_sha256']:errors.append('Dossier changed '+r['id'])
        if d.get('decompile_completed') is not True:errors.append('Incomplete decompile '+r['id'])
    checks['native_dossiers_verified']=len(catalog)
    hooks=read_json(E/'HOOK_CANDIDATES.json')['records']
    for r in hooks:
        c=by.get(r['va'])
        if not c or any(r[k]!=c[k] for k in ['rva','file_offset','binary_sha256','evidence']):errors.append('Hook identity mismatch '+r['id'])
        if not r['risk'] or not r['dynamic_validation'] or not r['argument_hypothesis']:errors.append('Incomplete hook candidate '+r['id'])
    checks['critical_candidates_verified']=len(hooks)
    ach=read_json(E/'E018_ACHIEVEMENT_DEFINITIONS.json')['definition']['achievements']
    keys={a['string_id'] for a in ach};raw=read_json(E/'E017_ACHIEVEMENT_ANCHORS.json')['strings']
    observed={a['value'] for a in raw if re.fullmatch('ACH_[A-Z0-9_]+',a['value'])}
    if len(ach)!=20 or len(keys)!=20 or {a['ordinal'] for a in ach}!=set(range(20)) or keys!=observed:errors.append('Achievement inventory mismatch')
    checks['achievement_definitions_and_keys']=len(ach)
    handoff=(DOC/'10_OPUS_HANDOFF.md').read_text(encoding='utf8')
    tokens=['Full Player','Player Proxy','ENet','2–8','Shared Achievement Progress','0x1dbc60','0x184110','VERIFIED','INFERRED','UNKNOWN','M0','late join','0x1e0910','0x1b5850']
    missing=[x for x in tokens if x not in handoff]
    if missing:errors.append('Handoff missing required topics '+str(missing))
    checks['handoff_required_topics']=len(tokens)
    # Reference source identity is checked only where an exact path and hash were captured.
    refs={}
    def walk(o):
        if isinstance(o,dict):
            pairs=[('path','sha256'),('source_path','source_sha256'),('file_path','source_sha256'),('absolute_path','sha256')]
            for pk,hk in pairs:
                if isinstance(o.get(pk),str) and re.fullmatch('[0-9a-fA-F]{64}',str(o.get(hk,''))):
                    q=Path(o[pk])
                    if q.is_absolute() and q.is_file() and q.is_relative_to(ROOT.parent) and not q.is_relative_to(ROOT):refs[str(q)]=o[hk].lower()
            for v in o.values():walk(v)
        elif isinstance(o,list):
            for v in o:walk(v)
    for p,d in alljson.items():
        if p.name.startswith(('REF2015_','REFWORLD_')):walk(d)
    for p,h in refs.items():
        if sha(Path(p))!=h:errors.append('Reference source hash changed '+p)
    checks['reference_source_hashes_checked']=len(refs)
    report={'captured_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'status':'PASS' if not errors else 'FAIL',
       'scope':'Artifact integrity, links and static coordinates only; no game/network/hook/Steam runtime tests',
       'checks':checks,'errors':errors}
    (E/'FINAL_VALIDATION.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps(report,ensure_ascii=True,indent=2))
    raise SystemExit(1 if errors else 0)

if __name__=='__main__':main()
