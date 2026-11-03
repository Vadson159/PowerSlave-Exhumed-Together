"""Read-only original/save/source audit and final build/source manifest."""
from pathlib import Path
import hashlib, json, importlib.util, sys, shutil, datetime, subprocess
LAB=Path(__file__).resolve().parent
ROOT=LAB.parents[2]
OUT=ROOT/'docs/evidence/m1/M12A_IMPLEMENTATION_20261102'
sys.path.insert(0,str(ROOT/'experiments/m0'))
import prepare
def load(p): return json.loads(p.read_text(encoding='utf-8-sig'))
def digest(p): return prepare.digest(p)
def main():
    prep=load(ROOT/'docs/evidence/m0/M0_001_PREPARATION.json')
    game_errors=[]
    for r in prep['independent_copy_files']:
        src=prepare.SOURCE/r['relative_path']; copy=prepare.COPY/r['relative_path']
        import os
        if digest(src)!=r['sha256'] or digest(copy)!=r['sha256'] or os.path.samefile(src,copy): game_errors.append(r['relative_path'])
    original=prepare.snapshot(prepare.SOURCE)
    source_baseline=load(ROOT/'docs/evidence/m1/M11_IMPLEMENTATION_20261101/BASELINE.json')['files']
    unchanged=[]; deviations=[]
    for r in source_baseline:
        p=ROOT/r['path']
        if p.is_relative_to(ROOT/'experiments/m0') or p.is_relative_to(ROOT/'tools/enet-prototype'):
            current=digest(p)
            if current!=r['sha256']: deviations.append(dict(path=r['path'],before=r['sha256'],after=current))
            else: unchanged.append(r['path'])
    saves=[]
    for name in ['M12A_NATIVE_01','M12A_DIRECT_QUIT_01']:
        run=ROOT/'docs/evidence/m1'/name
        before=load(run/'protected_before.json'); after=load(run/'protected_after.json')
        saves.append(dict(run=name,before_after_equal=before==after,current_equal=after==[prepare.snapshot(Path(r['path'])) for r in after],profiles=len(before)))
    old_build=load(ROOT/'docs/evidence/m1/M11_IMPLEMENTATION_20261101/BUILD_20261101_202721_800243.json')
    build_deviations=[r['path'] for r in old_build['sources'] if digest(ROOT/r['path'])!=r['sha256']]
    # DOC-41 explicitly froze diagnostic harness updates after the DLL build.
    # Compare those files to that final snapshot instead of attributing their
    # older build-manifest differences to this M1.2a change.
    final_harness=ROOT/'docs/evidence/m1/M11_IMPLEMENTATION_20261101/final_harness'
    historical_harness=[p for p in build_deviations if (final_harness/Path(p).name).is_file() and digest(ROOT/p)==digest(final_harness/Path(p).name)]
    m11_changes=[p for p in build_deviations if p not in historical_harness]
    accepted_dll=ROOT/'experiments/m1/native_bootstrap/build/Release/pse_m11_bootstrap.dll'
    accepted_m11_unchanged=digest(accepted_dll)=='0cb84521c47dddd20bdebf3a2f42dda79f2f50ef1cfc3c280c14878812648629'
    report=dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),game_files_checked=len(prep['independent_copy_files']),game_errors=game_errors,full_original_snapshot_equal=original==prep['original_installation_before'],saves=saves,unchanged_M0_and_transport_files=unchanged,source_deviations=deviations,accepted_M11_dll_unchanged=accepted_m11_unchanged,M11_source_changes=m11_changes,preexisting_DOC41_harness_changes=historical_harness)
    report['status']='PASS' if not game_errors and report['full_original_snapshot_equal'] and all(r['before_after_equal'] and r['current_equal'] for r in saves) and accepted_m11_unchanged and len(deviations)==1 and deviations[0]['path'].replace('\\','/')=='tools/enet-prototype/src/enet_worker.cpp' and m11_changes==['experiments\\m1\\native_bootstrap\\src\\bootstrap.cpp'] else 'REVIEW'
    (OUT/'INTEGRITY.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    files=[]
    roots=[LAB,ROOT/'experiments/m1/native_bootstrap/src',ROOT/'experiments/m1/native_bootstrap/third_party',ROOT/'tools/enet-prototype']
    frozen=OUT/'final_sources'
    for root in roots:
        for p in sorted(root.rglob('*')):
            if not p.is_file() or {'build','profile','field-test','__pycache__'}.intersection(p.parts): continue
            if p.suffix not in ['.cpp','.c','.h','.hpp','.py','.js','.txt','.md']: continue
            r=dict(path=str(p.relative_to(ROOT)),sha256=digest(p),size=p.stat().st_size)
            files.append(r)
            dest=frozen/p.relative_to(ROOT); dest.parent.mkdir(exist_ok=True,parents=True); shutil.copy2(p,dest)
    artifacts=[]
    for p in sorted((LAB/'build/Release').glob('*')):
        if p.suffix in ['.dll','.exe','.pdb']: artifacts.append(dict(path=str(p.relative_to(ROOT)),sha256=digest(p),size=p.stat().st_size))
    manifest=dict(exe_sha256=prepare.EXPECTED,configuration='Release x64, MSVC19.44, SDK10.0.26100.0, C++20, /MT; PSE_M12A; one loaded DLL',sources=files,artifacts=artifacts,reused_native_coordinates='docs/evidence/m1/M11_IMPLEMENTATION_20261101/NATIVE_COORDINATES.json',dependencies='Unchanged vendored MinHook1.3.4 and ENet sources in manifest; accepted DOC-40 core except proven generation filter')
    (OUT/'BUILD_MANIFEST.json').write_text(json.dumps(manifest,indent=2),encoding='utf8')
    print(json.dumps({k:v for k,v in report.items() if k not in ['unchanged_M0_and_transport_files']}))
if __name__=='__main__': main()
