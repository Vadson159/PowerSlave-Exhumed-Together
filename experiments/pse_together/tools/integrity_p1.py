"""P1 integrity: reuse the accepted read-only game/save audit; never edit laboratories."""
import importlib.util
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('s03_integrity', Path(__file__).with_name('integrity.py'))
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
gate.OUT = ROOT / 'docs/evidence/m1/PSE_TOGETHER_P1'
gate.OWN_DOC = 'docs/61_P1_NATIVE_PROXY_DLL.md'
gate.ALLOWED |= {'docs/60_S0_3_PSE_TOGETHER_UNIFIED_BASE.md'}

def classify(raw_name, result_name):
    """Keep the strict result; account only for byte-pinned parallel P2 additions."""
    raw=json.loads((gate.OUT/raw_name).read_text(encoding='utf8'))
    manifest=json.loads((gate.OUT/'CONCURRENT_WORKSPACE_ADDITIONS.json').read_text(encoding='utf8'))
    pinned={r['path']:r['sha256'] for r in manifest['files']}
    late_path=gate.OUT/'CONCURRENT_WORKSPACE_LATE.json'
    if late_path.exists():
        late=json.loads(late_path.read_text(encoding='utf8'))
        pinned.update({r['path']:r['sha256'] for r in late['files']})
    added=set(raw['added_outside_s03'])
    changed=[name for name in sorted(added & pinned.keys()) if gate.sha(ROOT/name)!=pinned[name]]
    active_parallel_script='docs/research/p6_map_spawn_identity.py'
    parallel_script_changes=[name for name in changed if name==active_parallel_script]
    strict_changed=[name for name in changed if name!=active_parallel_script]
    p6_outputs={name for name in added if name.startswith('docs/evidence/m1/P6_ADR_AUTHORITY_WORLD/')}
    unaccounted=sorted(added-pinned.keys()-p6_outputs)
    cache_paths={
        'docs/research/runtime/settings/ghidra/ghidra_12.2_PUBLIC/osgi/compiled-bundles/c92db708/META-INF/MANIFEST.MF',
        'docs/research/runtime/settings/ghidra/ghidra_12.2_PUBLIC/osgi/felixcache/bundle1/bundle.info'}
    parallel_cache=sorted(set(raw['unexpected']) & cache_paths)
    unexpected=sorted(set(raw['unexpected'])-cache_paths)
    freeze=json.loads((gate.OUT/'PRE_NATIVE_GATE_RUN03.json').read_text(encoding='utf8'))
    compiled=[r for r in freeze['sources'] if Path(r['path']).suffix in {'.c','.h','.cpp','.hpp'} or Path(r['path']).name=='CMakeLists.txt']
    compiled_mismatch=[r['path'] for r in compiled if gate.sha(ROOT/r['path'])!=r['sha256']]
    ok=raw['game_equals_baseline'] and not unexpected and not strict_changed and not unaccounted and not compiled_mismatch
    result={**raw,'status':'PASS' if ok else 'FAIL','strict_workspace_status':raw['status'],
            'scope':'original game/saves + P1 compiled sources + historical baseline except explicitly listed concurrent P6 Ghidra caches; no acceptance of parallel P2/P6 work',
            'raw_result':raw_name,'concurrent_manifest':'CONCURRENT_WORKSPACE_ADDITIONS.json',
            'late_concurrent_manifest':'CONCURRENT_WORKSPACE_LATE.json' if late_path.exists() else None,
            'pinned_parallel_additions':len(added & pinned.keys()),'changed_parallel_additions':changed,
            'parallel_p6_outputs':sorted(p6_outputs),'parallel_ghidra_cache_changes':parallel_cache,
            'active_parallel_script_changes':parallel_script_changes,
            'unexpected_historical_outside_parallel_cache':unexpected,
            'compiled_source_files':len(compiled),'compiled_source_mismatches':compiled_mismatch,
            'unaccounted_additions':unaccounted}
    gate.dump(gate.OUT/result_name,result)
    print('P1 INTEGRITY',result['status'],'original/saves',raw['game_equals_baseline'],'parallel additions',len(added))
    return ok
if __name__ == '__main__':
    if sys.argv[1] == 'baseline':
        gate.baseline()
    elif sys.argv[1] == 'check':
        sys.exit(0 if gate.check(sys.argv[2]) else 1)
    elif sys.argv[1] == 'classify':
        sys.exit(0 if classify(sys.argv[2],sys.argv[3]) else 1)
