"""Static build-equivalence of the unified DLL variants against the former laboratory sources (S0.3).

    python -X utf8 -B verify_dll_equivalence.py [--out DIR]

Why not compare with the frozen DLL bytes: the compiler on this machine has moved since some frozen DLLs were built
(see `toolchain_drift`), so even the unchanged lab source rebuilds to different bytes. The meaningful comparison is the
SAME toolchain over (a) the original lab source and (b) the unified source, with the PDB path normalised
(/PDBALTPATH:%_PDB%, otherwise the PDB path length shifts .rdata and every rip-relative displacement):
  bootstrap    native_bootstrap                    vs pse_dll_host_bootstrap
  host         native_map_ready  (pse_m12a)        vs pse_dll_host
  client       native_map_ready  (pse_native_client) vs pse_dll_client
  client_proxy native_network_visual_proxy_58b     vs pse_dll_client_proxy         (unified header = DOC-58B.2 automaton)
  client_proxy CONTROL: unified tree with the DOC-58B (not 58B.2) automaton header -> isolates the merge from the 58B.2 delta.
Nothing here runs a DLL or the game. Scratch build trees b61..b67 (gitignored), control copy experiments/pse_together_ctl (removed)."""
from pathlib import Path
import argparse, datetime, importlib.util, json, re, shutil, subprocess, sys

ROOT = Path(__file__).resolve().parents[3]
EXP = ROOT / 'experiments'
CM = 'C:/Program Files/CMake/bin/cmake.exe'
GEN = ['-G', 'Visual Studio 17 2022', '-A', 'x64', '-DCMAKE_SYSTEM_VERSION=10.0.26100.0']
ALT = '-DCMAKE_SHARED_LINKER_FLAGS=/PDBALTPATH:%_PDB%'
spec = importlib.util.spec_from_file_location('dll_compare', Path(__file__).with_name('dll_compare.py'))
dc = importlib.util.module_from_spec(spec); spec.loader.exec_module(dc)

def build(src, tree, targets, log, alt=True):
    t = ROOT / tree
    if t.exists(): shutil.rmtree(t)
    with open(log, 'wb') as f:
        a = subprocess.run([CM, '-S', str(src), '-B', str(t), *GEN, *([ALT] if alt else [])], stdout=f, stderr=subprocess.STDOUT).returncode
        b = subprocess.run([CM, '--build', str(t), '--config', 'Release', '--parallel', '4', '--target', *targets], stdout=f, stderr=subprocess.STDOUT).returncode if a == 0 else a
    assert a == 0 and b == 0, (src, tree, a, b)

def brief(r): return {k: r[k] for k in ('all_sections_equal_masked', 'equal_sections', 'differing_sections', 'exports_equal')}

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--out'); a = ap.parse_args()
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S')
    out = Path(a.out) if a.out else ROOT / 'docs/evidence/m1/PSE_TOGETHER_S03' / f'DLL_EQUIVALENCE_{stamp}'
    out.mkdir(parents=True, exist_ok=False)
    m1 = EXP / 'm1'
    build(m1 / 'native_bootstrap', 'b61', ['pse_m11'], out / 'b61.log')
    build(m1 / 'native_map_ready', 'b62', ['pse_m12a', 'pse_native_client'], out / 'b62.log')
    build(m1 / 'native_network_visual_proxy_58b', 'b63', ['pse_native_client58b'], out / 'b63.log')
    build(EXP / 'pse_together', 'b64', ['pse_dlls'], out / 'b64.log')
    res = {}
    pairs = dict(
        bootstrap=('b61/Release/pse_m11_bootstrap.dll', 'b64/dll/pse_dll_host_bootstrap/pse_m11_bootstrap.dll'),
        host=('b62/Release/pse_m11_bootstrap.dll', 'b64/dll/pse_dll_host/pse_m11_bootstrap.dll'),
        client=('b62/Release/pse_native_client.dll', 'b64/dll/pse_dll_client/pse_native_client.dll'),
        client_proxy=('b63/Release/pse_native_client58b.dll', 'b64/dll/pse_dll_client_proxy/pse_native_client58b.dll'))
    for k, (x, y) in pairs.items():
        r = dc.compare(ROOT / x, ROOT / y); (out / f'DLL_EQUIV_{k}.json').write_text(json.dumps(r, indent=2), encoding='utf8'); res[k] = brief(r)
    # control: unified tree with the accepted DOC-58B automaton header
    ctl = EXP / 'pse_together_ctl'
    try:
        if ctl.exists(): shutil.rmtree(ctl)
        ctl.mkdir()
        for n in ['CMakeLists.txt', 'include', 'src', 'third_party', 'tests']:
            s = EXP / 'pse_together' / n; (shutil.copytree if s.is_dir() else shutil.copy2)(s, ctl / n)
        h = (m1 / 'native_network_visual_proxy_58b/src/visual_proxy_58b.hpp').read_text(encoding='utf-8')
        h = h.replace('namespace pse::proxy::doc58b {', 'namespace pse::proxy {').replace('// namespace pse::proxy::doc58b', '// namespace pse::proxy')
        (ctl / 'src/proxy/visual_proxy.hpp').write_text(h, encoding='utf-8', newline='')
        build(ctl, 'b65', ['pse_dll_client_proxy'], out / 'b65.log')
        r = dc.compare(ROOT / 'b63/Release/pse_native_client58b.dll', ROOT / 'b65/dll/pse_dll_client_proxy/pse_native_client58b.dll')
        (out / 'DLL_EQUIV_client_proxy_CONTROL_58b_header.json').write_text(json.dumps(r, indent=2), encoding='utf8'); res['client_proxy_CONTROL_58b_header'] = brief(r)
    finally:
        shutil.rmtree(ctl, ignore_errors=True)
    # toolchain drift: frozen DLL vs the same lab source rebuilt today with default flags (no PDBALTPATH)
    build(m1 / 'native_map_ready', 'b66', ['pse_m12a'], out / 'b66.log', alt=False)
    build(m1 / 'native_network_visual_proxy_58b', 'b67', ['pse_native_client58b'], out / 'b67.log', alt=False)
    drift = dict(
        host_frozen_vs_rebuilt=brief(dc.compare(m1 / 'native_map_ready/build/Release/pse_m11_bootstrap.dll', ROOT / 'b66/Release/pse_m11_bootstrap.dll')),
        client58b_frozen_vs_rebuilt=brief(dc.compare(ROOT / 'b58/Release/pse_native_client58b.dll', ROOT / 'b67/Release/pse_native_client58b.dll')))
    res['toolchain_drift'] = drift
    res['frozen_hashes'] = dict(host=dc.parse(m1 / 'native_map_ready/build/Release/pse_m11_bootstrap.dll')['raw_sha256'], client58b=dc.parse(ROOT / 'b58/Release/pse_native_client58b.dll')['raw_sha256'])
    ok = all(res[k]['all_sections_equal_masked'] for k in ('bootstrap', 'host', 'client')) and res['client_proxy_CONTROL_58b_header']['equal_sections'].count('.text') == 1 \
        and '.pdata' in res['client_proxy_CONTROL_58b_header']['equal_sections']
    res['verdict'] = 'PASS' if ok else 'FAIL'
    (out / 'DLL_EQUIVALENCE.json').write_text(json.dumps(res, indent=2), encoding='utf8')
    print(json.dumps(res, indent=1)); return 0 if ok else 1

if __name__ == '__main__':
    sys.exit(main())
