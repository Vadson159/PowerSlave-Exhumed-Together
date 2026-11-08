"""Read-only exact-PE metadata for P7 native pins; no runtime or analysis calls."""
import hashlib
import json
import re
import struct
from pathlib import Path

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'docs/evidence/m1/PSE_TOGETHER_P7'
source=ROOT/'experiments/pse_together/src/native/self_native_p7.cpp'
exe=ROOT/'experiments/m0/game/anubis_Shipping_Steam_x64.exe'
data=exe.read_bytes()
digest=hashlib.sha256(data).hexdigest()
assert digest=='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
pe=struct.unpack_from('<I',data,0x3c)[0]
sections=[]
offset=pe+24+struct.unpack_from('<H',data,pe+20)[0]
for _ in range(struct.unpack_from('<H',data,pe+6)[0]):
    size,rva,raw_size,raw=struct.unpack_from('<IIII',data,offset+8)
    sections.append((rva,max(size,raw_size),raw))
    offset+=40
paths=list((ROOT/'docs/evidence').rglob('G_FUNC_140*.json'))
rows=[]
for name,rva_text,bytes_text in re.findall(r'\{"([A-Za-z]+)",(0x[0-9a-f]+),\{([^}]+)\}\}',source.read_text()):
    rva=int(rva_text,16)
    raw=next(raw+rva-start for start,size,raw in sections if start<=rva<start+size)
    expected=bytes(int(x.strip(),16) for x in bytes_text.split(','))
    evidence=[p.relative_to(ROOT).as_posix() for p in paths if p.name==f'G_FUNC_{0x140000000+rva:x}.json']
    if name=='MutableKeys':
        evidence.append('docs/evidence/ghidra_2022/G_INSPECT_1401ae0c0.json')
    rows.append(dict(analyst_name=name,original_cpp_name='UNKNOWN',
                     binary_sha256=digest,preferred_va=hex(0x140000000+rva),rva=hex(rva),file_offset=hex(raw),
                     prefix16=expected.hex(),exact_prefix_match=data[raw:raw+len(expected)]==expected,
                     evidence=evidence,
                     runtime_coverage='Prefix gated/install witnessed; execution coverage is per-operation in DOC-80/RUN03, not implied for every hook.'))
assert len(rows)==23
result=dict(status='PASS' if all(x['exact_prefix_match'] for x in rows) else 'FAIL',
            scope='static exact pin metadata; not a runtime receipt',
            source=source.relative_to(ROOT).as_posix(),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
            count=len(rows),sites=rows)
(OUT/'FINAL_NATIVE_PINS.json').write_text(json.dumps(result,indent=2),encoding='utf8')
print(result['status'],len(rows),'exact prefixes, VA/RVA/PE-derived file offsets')
