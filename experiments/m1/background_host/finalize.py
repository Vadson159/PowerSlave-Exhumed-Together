"""Final DOC53 coordinates, source/evidence manifest and read-only validation."""
from pathlib import Path
import datetime,difflib,hashlib,json,re,struct
from audit import ROOT,LAB,OUT,dump,sha,game_check

def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def coordinates():
    exe=ROOT/'experiments/m0/game/anubis_Shipping_Steam_x64.exe';b=exe.read_bytes();pe=struct.unpack_from('<I',b,0x3c)[0]
    assert b[pe:pe+4]==b'PE\0\0' and struct.unpack_from('<H',b,pe+4)[0]==0x8664
    n=struct.unpack_from('<H',b,pe+6)[0];sz=struct.unpack_from('<H',b,pe+20)[0];base=struct.unpack_from('<Q',b,pe+24+24)[0]
    sections=[]
    for i in range(n):
        at=pe+24+sz+40*i;vsize,rva,rawsize,raw=struct.unpack_from('<IIII',b,at+8)
        sections.append(dict(name=b[at:at+8].rstrip(b'\0').decode('ascii'),rva=rva,virtual_size=vsize,raw_size=rawsize,file_offset=raw))
    def offset(r):
        for s in sections:
            if s['rva']<=r<s['rva']+s['raw_size']:return s['file_offset']+r-s['rva']
        return None
    funcs={}
    for d in ['static','input_static','decision_static','menu_static','travel_static']:
        for p in (OUT/d).glob('G_FUNC_*.json'):
            o=load(p);va=int(o['function']['entry'],16);r=va-base;f=offset(r)
            funcs[va]=dict(analyst_name=o['function']['name'],preferred_va=hex(va),rva=hex(r),file_offset=hex(f) if f is not None else None,prefix16=b[f:f+16].hex() if f is not None else None,evidence=p.relative_to(ROOT).as_posix(),binary_sha256=o['binary_sha256'],abi='See DOC53 for used seams; other Ghidra signatures/arguments not independently ABI-validated; no manual runtime call',semantics_status='static observation; interpretation separately qualified in DOC53')
    data={name:r for name,r in [('GameInstance',0xaf33a0),('PrimaryPlayer',0xaf33e0),('NativeFocusByte',0xaf36b9),('TravelScreenByte',0xaf35ac),('InventoryObject',0xa43f30),('InventoryActiveByte',0xa440e0),('InventoryVtable',0x52b420),('TravelScreenVtable',0x52ae90)]}
    rows=[]
    for name,r in data.items():
        f=offset(r);rows.append(dict(analyst_name=name,preferred_va=hex(base+r),rva=hex(r),file_offset=hex(f) if f is not None else None,file_offset_absence='uninitialized/no raw file mapping' if f is None else None,runtime_address_derived_for_this_run=hex(base+r)))
    dump(OUT/'COORDINATES_SUPPLEMENT.json',dict(binary_sha256=sha(exe),preferred_base=hex(base),runtime_base_derived_from_attested_GameInstance='0x140000000',runtime_address_formula='actual module base + RVA; data runtime qword values are not invented from static values',functions=list(funcs.values()),data=rows,sections=sections))

def manifest():
    paths=set()
    for root in [OUT,ROOT/'docs/evidence/m1/N53_HOST_MAIN_01',ROOT/'docs/evidence/m1/N53_CLIENT_MAIN_01',LAB]:
        for p in root.rglob('*'):
            if p.is_file() and not {'build','__pycache__'}.intersection(p.relative_to(root).parts) and p.name not in ['FILES.json','FINAL_VALIDATION.json']:paths.add(p)
    paths.update(ROOT/'docs'/n for n in ['53_HOST_BACKGROUND_SIMULATION.md','PROGRESS.md','EVIDENCE_INDEX.md'])
    paths.update((ROOT/'experiments/m1/native_bootstrap/src').glob('*.hpp'));paths.add(ROOT/'experiments/m1/native_bootstrap/src/bootstrap.cpp')
    records=[dict(path=p.relative_to(ROOT).as_posix(),size=p.stat().st_size,sha256=sha(p)) for p in sorted(paths)]
    dump(OUT/'FILES.json',dict(captured_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),self_and_final_validation_excluded=True,files=records))
    return records

def validate(records):
    errors=[];links=[]
    for p in [ROOT/'docs/53_HOST_BACKGROUND_SIMULATION.md',LAB/'README.md']:
        for match in re.finditer(r'\]\(([^\n)]+)\)',p.read_text(encoding='utf8')):
            href=match.group(1).strip('<>');href=href.split('#')[0]
            if not href or '://' in href:continue
            target=(p.parent/href).resolve();links.append(dict(source=p.relative_to(ROOT).as_posix(),href=href,exists=target.exists()))
            if not target.exists():errors.append('Missing link '+str(p)+': '+href)
    base=load(OUT/'BASELINE.json');changed=[r['path'] for r in base['files'] if not (ROOT/r['path']).is_file() or sha(ROOT/r['path'])!=r['sha256']]
    allowed=['experiments/m1/native_bootstrap/src/bootstrap.cpp'];errors+=['Unexpected historical change '+p for p in changed if p not in allowed]
    compiled=load(OUT/'BUILD_MANIFEST.json');drift=[r['path'] for r in compiled['sources'] if sha(ROOT/r['path'])!=r['sha256']]
    errors+=['Compiled input source drift '+p for p in drift]
    g=game_check();errors+=g['errors']+g['client_copy_errors']
    if not g['original_full_snapshot_equal'] or g['protected']!=base['game']['protected']:errors.append('Original/protected snapshot mismatch')
    for run in ['N53_HOST_MAIN_01','N53_CLIENT_MAIN_01']:
        r=ROOT/'docs/evidence/m1'/run;sig=load(r/'OS_SIGNALED.json');art=load(r/'NATIVE_ARTIFACT.json')['config']
        if not(sig['wait']==0 and sig['duplicate_wait']==0 and sig['exit_code']==0 and sig['no_emergency_cleanup']):errors.append('No normal same-object exit: '+run)
        if sha(Path(art['dll']))!=art['dll_sha256']:errors.append('Runtime DLL hash drift: '+run)
    sessions=load(OUT/'SESSIONS_CLOSED.json')
    if len(sessions)!=25 or any(r['result'].get('isError') for r in sessions):errors.append('Session cleanup failure')
    finalos=load(sorted(OUT.glob('OS_after_normal_exit_*.json'))[-1])
    if finalos['processes'] or finalos['udp']:errors.append('Final OS resources remain')
    o=dict(status='PASS' if not errors else 'FAIL',scope='artifact/integrity/normal-process-exit verification; DOC53 overall remains PARTIAL',errors=errors,historical_checked=len(base['files']),existing_changed=changed,compiled_source_drift=drift,manifested_files=len(records),links=links,game=g,sessions_closed=len(sessions),result=load(OUT/'RESULT_SUMMARY.json')['overall'],files_manifest_sha256=sha(OUT/'FILES.json'))
    dump(OUT/'FINAL_VALIDATION.json',o);print(json.dumps({k:o[k] for k in ['status','errors','historical_checked','existing_changed','compiled_source_drift','manifested_files','sessions_closed','result']},indent=2));assert not errors
if __name__=='__main__':coordinates();validate(manifest())
