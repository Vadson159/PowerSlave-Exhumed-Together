from workflow import *
import struct
assert sha(EXE)==EXPECTED
assert sha(KPF)=='d34b63551713bf46c4215632d9b5f92b452791023937fe7d22a196bff9ff4bbd'
b=EXE.read_bytes();pe=struct.unpack_from('<I',b,60)[0];n=struct.unpack_from('<H',b,pe+6)[0];opt=struct.unpack_from('<H',b,pe+20)[0]
sections=[struct.unpack_from('<IIII',b,pe+24+opt+40*i+8) for i in range(n)]
def read(rva,size):
    vs,va,sz,raw=next(s for s in sections if s[1]<=rva<s[1]+max(s[0],s[2]));off=raw+rva-va
    assert rva-va+size<=sz
    return off,b[off:off+size]
def u64(rva):return struct.unpack('<Q',read(rva,8)[1])[0]
sites=load(HISTORICAL_58A/'SITES.json')
# Called read-only queries; never hooked. Assembly in COMPLETION/static and 58A/static.
for k,rva in dict(point_in_sector=0x1cf540,floor_height=0x1a3eb0,ceiling_height=0x1a4060).items():
    off,raw=read(rva,16);sites[k]=dict(rva=rva,prefix=raw.hex(),preferred_va=hex(0x140000000+rva),file_offset=hex(off))
for k,v in sites.items():assert read(v['rva'],16)[1].hex()==v['prefix'],k
vtables=dict(level_vtable_0x52b478_slot38=hex(u64(0x52b478+0x38)),actor1007_vtable_0x528608_slot30=hex(u64(0x528608+0x30)),actor1007_vtable_0x528608_slot38=hex(u64(0x528608+0x38)))
assert vtables==dict(level_vtable_0x52b478_slot38='0x1401c98f0',actor1007_vtable_0x528608_slot30='0x1401a02b0',actor1007_vtable_0x528608_slot38='0x1401a02f0'),vtables
dump(GATE/'SITES.json',sites)
exports=[]
for folder in [HISTORICAL_58A/'static',OUT/'static']:
    for p in sorted(folder.glob('G_*.json')):
        d=load(p);assert d['binary_sha256']==EXPECTED;exports.append(dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)))
dump(GATE/'STATIC_AUDIT.json',dict(binary_sha256=EXPECTED,exports=exports,sites=sites,vtables=vtables,
    link_sector=dict(rva='0x1d3d70',abi='void(Actor* RCX,int32 EDX)',
        instructions=['1401d3d7f MOVSXD RDI,EDX; 1401d3d85 CMP RDI,[a45970]; JC ok else CALL assert7b3c0 (return not proven; caller must bound 0<=i<count)',
            '1401d3daa R8=[Actor+10] node; JNZ existing-node path 1401d3e14',
            '1401d3e14 CMP [Actor+3c],EDI; JZ 1401d3e48 (same sector: no link writes)',
            '1401d3e19..e2f unlink: [prev+8]=next; [next]=prev',
            '1401d3e32..e44 insert after head=[sector+78]: node.next=head.next; node.prev=head; head.next.prev=node; head.next=node',
            '1401d3e4d MOV [Actor+3c],EDI; RET'],
        callees=['assert7b3c0','node alloc1d5820 only when node==null'],side_effects='node links + sectorIndex only; no geometry, floor/ceiling, interpolant, callback or render update'),
    factory_placement=dict(rva='0x1d4780',order='origin+18/1c/20 stores -> LinkSector1d3d70(actor,sector) -> vtable+38 (1007: 1a02f0 -> Spawn1d34d0 interpolant init, then ceiling1a4060->+120, floor1a3eb0->+11c)',evidence='COMPLETION/static/G_FUNC_1401a02f0.json 1401a08d6/1401a08df/1401a091e/1401a092c'),
    actor_tick_gate=dict(rva='0x19f2d0',instructions=['14019f3b1 MOV EDI,[RBX+16c]','14019f537 TEST RCX,RCX ([+170] definition); JZ 14019fdd1','14019f540 TEST DIL,DIL; 14019f543 JNS 14019fdd1'],
        meaning='flags bit7 clear (stock 1007 flags0x40) skips the whole physics/sector-transition block 14019f549..14019fdd0, including every +11c/+120 store and its LinkSector call'),
    floor_ceiling=dict(floor='0x1a3eb0 float(float* point RCX, Sector* RDX, char chain R8B): face last+2',ceiling='0x1a4060 same ABI: face last+1',purity='assembly: only home-space RBX/RSI saves and assert calls; no other stores',
        contract='1007 caches +11c/+120 only at spawn. Cross-sector allowed only when native floor/ceiling at target in destination equal the cached values bit-for-bit and sector flags14&0x100 (water) match; otherwise refuse (no cache writes).'),
    point_in_sector=dict(rva='0x1cf540',abi='bool AL(Sector* RCX, float* point RDX, float eps XMM2)',scope='faces first..last (walls only)',purity='assembly: no stores beyond home space'),
    find_sector=dict(rva='0x1c98f0',slot='level vtable+38',not_called='writes visit marks sector+34 and global work stack 140a48258 (may grow allocation); mirrored read-only in cross_sector.js findSector (BFS through faces flags0x2000, adjoin s16 face+6, tests faces first..last+2)'),
    portal_layout=dict(face_flags_portal=0x2000,adjoin='s16 face+6',sector_self_index='s32 sector+80',sector_flags='u16 sector+14',evidence='N58A_01 movement_plan raw faces: 259.face2087 adjoin123 flags0x12000; 123.face886 adjoin259 flags0x12000'),
    renderer=dict(collector='0x1e9a20 walks [sector+78] node list of a visible sector; world AABB from render output +6c..+74 (+128 z) and local bounds',sprite_entry='0x58 bytes: +0 frame, +8 scale(+124), +10 origin(render output), +28 angles(+94/+98/+9c), +4c flags, +50 sector'),
    production_changed=False))
command(['C:/Program Files/nodejs/node.exe',LAB/'offline_test.js'],'OFFLINE_GATE')
syntax()
sources=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in sorted(LAB.iterdir()) if p.suffix in ['.py','.js']]
dump(GATE/'PRE_NATIVE_GATE.json',dict(status='PASS',utc=stamp(),sources=sources,static_sha256=sha(GATE/'STATIC_AUDIT.json'),binary_sha256=EXPECTED,scope='DOC58A interpolation follow-up N58A_C02: spawn, same-sector smoke, one native_step (farthest approved point >=64) with identity-correlated sprite observer, return teleport, Remove',one_kex_run=True))
print('PRE_NATIVE PASS')
