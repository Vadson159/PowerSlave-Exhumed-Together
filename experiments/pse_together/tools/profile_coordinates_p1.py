"""Coordinates for reused exact-build functions, verified against saved sites and PE file bytes."""
import hashlib
import json
import struct
from pathlib import Path
from pre_native_p1 import ROOT, OUT, offset
exe=ROOT/'experiments/m0/game/anubis_Shipping_Steam_x64.exe';binary=exe.read_bytes()
pe=struct.unpack_from('<I',binary,0x3c)[0];image=struct.unpack_from('<Q',binary,pe+24+24)[0]
source=ROOT/'docs/evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/GATE_N58A_C02/SITES.json'
sites=json.loads(source.read_text());names=['factory','remove','interpolant_reset','point_in_sector','floor_height',
 'ceiling_height','puppet_tick','actor_tick','on_remove','pool_return','sweep','drain']
functions=[]
for name in names:
    s=sites[name];rva=s['rva'];file=offset(binary,rva)
    assert binary[file:file+16].hex()==s['prefix'],name
    functions.append(dict(analyst_role=name,restored_symbol=None,rva=hex(rva),preferred_va=hex(image+rva),
                          file_offset=hex(file),prefix=s['prefix'],evidence=source.relative_to(ROOT).as_posix()))
result=dict(binary_sha256=hashlib.sha256(binary).hexdigest(),image_base=hex(image),functions=functions,
            new_reverse_analysis=False,scope='reuse of DOC57/58A contracts; coordinates verified in file bytes')
with (OUT/'RUNTIME_COORDINATES_P1.json').open('x',encoding='utf8') as f:json.dump(result,f,indent=2)
print('exact coordinates',len(functions),'/',len(names))
