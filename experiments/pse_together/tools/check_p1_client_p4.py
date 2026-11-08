"""Rebuild only P1 client in a private tree and compare all masked PE sections."""
from pathlib import Path
import json,re,subprocess
import dll_compare as compare
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'docs/evidence/m1/PSE_TOGETHER_P4_REWORK_72'
ref=ROOT/'docs/evidence/m1/PSE_TOGETHER_P1/P1_CLIENT_03/native_artifact/pse_native_client58b.dll'
alt=re.search(rb'RSDS.{20}([^\x00]+)\x00',ref.read_bytes(),re.S).group(1).decode()
cm='C:/Program Files/CMake/bin/cmake.exe'
with (OUT/'P1_CLIENT_BUILD.log').open('wb') as f:
    subprocess.run([cm,'-S',str(ROOT/'experiments/pse_together'),'-B',str(ROOT/'b72c'),'-G','Visual Studio 17 2022','-A','x64','-DCMAKE_SYSTEM_VERSION=10.0.26100.0','-DCMAKE_SHARED_LINKER_FLAGS=/PDBALTPATH:'+alt],stdout=f,stderr=subprocess.STDOUT,check=True)
    subprocess.run([cm,'--build',str(ROOT/'b72c'),'--config','Release','--parallel','4','--target','pse_dll_client_proxy'],stdout=f,stderr=subprocess.STDOUT,check=True)
result=compare.compare(ref,ROOT/'b72c/dll/pse_dll_client_proxy/pse_native_client58b.dll')
(OUT/'P1_CLIENT_EQUIVALENCE.json').write_text(json.dumps(result,indent=2),encoding='utf8')
print('P1 client PE sections:',len(result['equal_sections']),'equal;',result['differing_sections'])
raise SystemExit(0 if result['all_sections_equal_masked'] else 1)
