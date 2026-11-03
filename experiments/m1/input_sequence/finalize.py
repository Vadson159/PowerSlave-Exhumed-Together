"""Generate evidence summaries from actual run state/logs, never PASS markers."""
from pathlib import Path
import hashlib,json,re,sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
import workflow as w
O=w.OUT;R=w.ROOT
latest=lambda pattern:sorted(O.glob(pattern))[-1]
def main():
    manifest=latest('BUILD_MANIFEST_after_*.json');m=json.loads(manifest.read_text(encoding='utf8'))
    for row in m['sources']:
        if Path(row['path']).suffix.lower() in ['.cpp','.c','.h','.hpp','.txt']:
            assert w.sha(R/row['path'])==row['sha256'],row['path']
    tests=latest('TESTS_after_*.json');rows=json.loads(tests.read_text(encoding='utf8'));assert len(rows)==11
    assert all(r['exit_code']==0 for r in rows)
    by={Path(row['argv'][0]).stem:row for row in rows}
    def raw(n):return (O/by[n]['log']).read_text(encoding='utf8')
    matrix=raw('sequence_contract');live=raw('sequence_live')
    # Parse numerical observations, retain those rows verbatim alongside summary.
    contract=re.search(r'CONTRACT checks=(\d+) failures=(\d+)',matrix);assert contract and int(contract[2])==0
    counts=re.search(r'LIVE_STRESS accepted=(\d+) received=(\d+) stale=(\d+) foreign=(\d+) coalesced=(\d+) pending_peak=(\d+) gen=(\d+) seq=(\d+)',live)
    assert counts and int(counts[1])>400 and int(counts[6])==0 and int(counts[7])==2 and int(counts[8])==723
    states=[line for line in matrix.splitlines() if line.startswith('STATE ')]
    windows=[line for line in live.splitlines() if line.startswith('STRESS_WINDOW ')]
    assert len(windows)==12
    baseline=json.loads((O/'BASELINE.json').read_text(encoding='utf8'));files={r['path']:r['sha256'] for r in baseline['files']}
    unchanged=['experiments/m1/network_input/src/native_client.cpp','experiments/m1/network_input/src/network_input.hpp','experiments/m1/native_bootstrap/src/bootstrap.cpp',
       'tools/enet-prototype/include/net_protocol.h','tools/enet-prototype/src/net_protocol.cpp','tools/enet-prototype/include/replication_protocol.h','tools/enet-prototype/src/replication_protocol.cpp',
       'tools/enet-prototype/src/thread_bridge.cpp','tools/enet-prototype/include/thread_bridge_dto.h','tools/enet-prototype/src/enet_host.cpp','tools/enet-prototype/src/simulation.cpp']
    assert all(w.sha(R/n)==files[n] for n in unchanged)
    hpp=(R/unchanged[1]).read_text(encoding='utf8');assert 'ticks=6;' in hpp and 'wallMs=150;' in hpp
    before=json.loads(latest('TESTS_before_*.json').read_text(encoding='utf8'))
    product=w.PRODUCT+['tools/enet-prototype/include/input_sequence.h']
    summary=dict(verdict='PASS',scope='P2-5 only; bounded current sender contract; no native run',build_manifest=manifest.name,final_tests=tests.name,before_tests=before,
      contract=dict(checks=int(contract[1]),failures=int(contract[2]),states=states),live=dict(accepted=int(counts[1]),received=int(counts[2]),stale=int(counts[3]),foreign=int(counts[4]),coalesced=int(counts[5]),pending_peak=int(counts[6]),generation=int(counts[7]),sequence=int(counts[8]),windows=windows,
         impairment=next(x for x in live.splitlines() if x.startswith('IMPAIR '))),
      source_hashes={n:w.sha(R/n) for n in product},unchanged_hashes={n:w.sha(R/n) for n in unchanged},native_compile_only=True,kex_runs=0,
      policy=dict(max_advance=1800,max_initial=10800,wrap='nonmodular fail closed; new generation plus low counter to resume'),tests=rows)
    w.dump(O/('RESULT_SUMMARY_'+w.stamp()+'.json'),summary)
    (O/'BOUNDARY_STATES.txt').write_text('\n'.join(states)+'\n',encoding='utf8')
    # New harness files are separate from the compiled source manifest.
    w.dump(O/('HARNESS_MANIFEST_'+w.stamp()+'.json'),[dict(path=p.relative_to(R).as_posix(),sha256=w.sha(p)) for p in w.LAB.glob('*') if p.is_file()])
    print(json.dumps({k:summary[k] for k in ['verdict','build_manifest','final_tests','source_hashes','unchanged_hashes']},indent=2))
if __name__=='__main__':main()
