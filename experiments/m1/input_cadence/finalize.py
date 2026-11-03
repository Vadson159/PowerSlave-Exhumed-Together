"""File-only final manifests, source commitment, decisions and report link QA."""
from pathlib import Path
import ast,hashlib,json,re,shutil
LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2];OUT=ROOT/'docs/evidence/m1/M12B_CADENCE_49'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def dump(p,o):p.write_text(json.dumps(o,indent=2,ensure_ascii=False),encoding='utf8')
def main():
    build=OUT/'BUILD_MANIFEST_20261102_100611_477963.json';m=json.loads(build.read_text(encoding='utf8'))
    compiled=[r for r in m['sources'] if Path(r['path']).suffix in ['.c','.cpp','.h','.hpp'] or Path(r['path']).name=='CMakeLists.txt']
    assert all(sha(ROOT/r['path'])==r['sha256'] for r in compiled)
    digest=hashlib.sha256(json.dumps(sorted(compiled,key=lambda r:r['path']),sort_keys=True,separators=(',',':')).encode()).hexdigest()
    dump(OUT/'SOURCE_COMMITMENT.json',dict(compiled_sources=compiled,sha256=digest,algorithm='SHA256 UTF8 canonical JSON sorted path records; sort_keys=True, separators=(comma,colon)',build_manifest_sha256=sha(build)))
    dump(OUT/'DECISIONS.json',dict(overall_verdict='PARTIAL',scheduler='bounded PASS; actual compiled before/after client and one native run',
        default_timeout=dict(ticks=6,wall_ms=150,operator='OR',changed=False),
        timeout_reason='Corrected client main matrix gives 0 false dropouts with all three candidates. No observed improvement from widening; no LAN/VPN calibration.',
        throttle='UNCHANGED',explicit_flush='UNCHANGED; ordinary ENet service after dispatch, QA immediate-flush comparison has no material delivery benefit',
        native_run='M12B_CADENCE_NATIVE_01 only; closed',
        boundaries='No KEX ABI/hooks, Worker, routing, admission, wire v1.10, Gemini QA reports, second game DLL or replication changes'))
    harness=[]
    for p in sorted(LAB.rglob('*')):
        if not p.is_file() or {'build','profile','__pycache__'}.intersection(p.parts):continue
        if p.suffix not in ['.py','.cpp','.hpp','.c','.txt','.md','.ps1','.json','.patch']:continue
        if p.suffix=='.py':ast.parse(p.read_text(encoding='utf8'))
        dest=OUT/'final_harness'/p.relative_to(LAB);dest.parent.mkdir(exist_ok=True,parents=True);shutil.copy2(p,dest)
        harness.append(dict(path=str(p.relative_to(ROOT)),sha256=sha(p)))
    dump(OUT/'HARNESS_MANIFEST.json',dict(files=harness,compiled_inputs_unchanged=True))
    docs=[ROOT/'docs/49_M12B_INPUT_CADENCE_REMEDIATION.md',ROOT/'docs/PROGRESS.md',ROOT/'docs/EVIDENCE_INDEX.md',LAB/'README.md']
    missing=[]
    for p in docs:
        text=p.read_text(encoding='utf8')
        if p.name in ['PROGRESS.md','EVIDENCE_INDEX.md']:text=text.split('## DOC-48')[0]
        for t in re.findall(r'\]\(([^)]+)\)',text):
            if not t.startswith(('http:','https:')) and not (p.parent/t.split('#')[0]).exists():missing.append(dict(path=str(p.relative_to(ROOT)),target=t))
    dump(OUT/'DOCUMENT_CHECK.json',dict(status='PASS' if not missing else 'FAIL',missing=missing,python_parse='PASS'))
    paths=[*OUT.rglob('*'),*(ROOT/'docs/evidence/m1/M12B_CADENCE_NATIVE_01').rglob('*'),*docs,*LAB.glob('*')]
    files=[dict(path=str(p.relative_to(ROOT)),sha256=sha(p),size=p.stat().st_size) for p in sorted(set(paths)) if p.is_file() and p.name!='FILES.json']
    dump(OUT/'FILES.json',dict(files=files,scope='DOC49 current artifact/source/document files; no historical file overwritten; self excluded'))
    print(json.dumps(dict(source_commitment=digest,document_links_missing=len(missing),manifest_files=len(files))));assert not missing
if __name__=='__main__':main()
