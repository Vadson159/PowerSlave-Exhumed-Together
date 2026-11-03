"""Exact-file coordinates for DOC53; PE parsing needs no external packages."""
import json, struct
from audit import ROOT, OUT, sha, dump

p=ROOT/'experiments/m0/game/anubis_Shipping_Steam_x64.exe'
expected='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
assert sha(p)==expected
b=p.read_bytes(); pe=struct.unpack_from('<I',b,0x3c)[0]
assert b[pe:pe+4]==b'PE\0\0'
n=struct.unpack_from('<H',b,pe+6)[0]; sz=struct.unpack_from('<H',b,pe+20)[0]
sections=[]
for i in range(n):
    s=pe+24+sz+40*i
    vsize,rva,rawsize,raw=struct.unpack_from('<IIII',b,s+8)
    sections.append((rva,vsize,rawsize,raw))
def offset(r):
    for a,v,n,f in sections:
        if a<=r<a+n:return f+r-a
    return None
sites={'FocusResponder':0x1b1640,'ResetBindings':0x95f20,'FocusEarlyGate':0x1b6eee,'FocusLossStore':0x1b1689,'FocusGainStore':0x1b1691,'InventoryObject':0xa43f30,'InventoryOpenGetter':0x1c34a0,'MenuOpen':0x1b19d0,'MenuClose':0x1b1a40,'PlayerNativeCall':0x1b7013}
rows=[]
for name,r in sites.items():
    f=offset(r); rows.append(dict(name=name,preferred_va=hex(0x140000000+r),rva=hex(r),file_offset=hex(f) if f is not None else None,prefix16=b[f:f+16].hex() if f is not None else None))
dump(OUT/'NATIVE_COORDINATES.json',dict(binary_sha256=expected,runtime_address='actual module base + RVA',sites=rows))
for r in rows:print(r)
