"""Read exact-build GI virtual targets for the bounded host-death investigation."""
import json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'docs/research'))
from inventory import PE
from prepare import COPY,EXPECTED,digest
target=COPY/'anubis_Shipping_Steam_x64.exe'
if digest(target)!=EXPECTED:raise ValueError('Build mismatch')
pe=PE(target)
result={'binary_sha256':EXPECTED,'game_instance_vtable':{'va':'0x14052ad08','rva':'0x52ad08','file_offset':hex(pe.off(0x52ad08))},'slots':[]}
for slot in [0x110,0x120]:
    va=struct.unpack_from('<Q',pe.b,pe.off(0x52ad08+slot))[0];rva=va-0x140000000
    result['slots'].append({'slot':hex(slot),'va':hex(va),'rva':hex(rva),'file_offset':hex(pe.off(rva)),'prefix':pe.b[pe.off(rva):pe.off(rva)+16].hex()})
print(json.dumps(result,indent=2))
