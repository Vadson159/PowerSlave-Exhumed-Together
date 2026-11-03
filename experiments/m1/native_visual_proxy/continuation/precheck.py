"""Freeze corrected harness and focused static evidence before the only fresh KEX run."""
from workflow import *
import struct

def fixture():
    bundle=load(OUT/'REA_A02_FIXTURE.json')
    r=next(r for r in bundle['records'] if r['evidence_id']=='ev_98486cf32e4bda8e9fc81b52a0dcd1015ef2f146448dcae8833763521087a9fa')
    n=r['normalized_result'];messages=n['messages']
    p=next(m['payload'] for m in messages if m.get('payload',{}).get('kind')=='vp57c_harness_fixture')
    assert p['passed'] and p['kex_calls']==0 and not p['kex'] and not n['messages_truncated']
    assert not [m for m in messages if m.get('type')=='error']
    src=load(OUT/'A02_FIXTURE_SOURCE.json')
    assert sha(LAB/'one_actor.js')==src['shared_state_sha256']
    assert sha(OUT/'A02_FIXTURE_BOUND.js')==src['bound_sha256']
    assert load(OUT/'A02_EXIT.json')['exit_code']==0
    dump(OUT/'A_HARNESS_GATE.json',dict(status='PASS',record=r,payload=p,source=src,fixture_process_exit=load(OUT/'A02_EXIT.json'),nested_listener_fix_verified=True))

def freeze():
    assert load(OUT/'A_HARNESS_GATE.json')['status']=='PASS'
    assert sha(EXE)==EXPECTED
    assert sha(KPF)=='d34b63551713bf46c4215632d9b5f92b452791023937fe7d22a196bff9ff4bbd'
    data=EXE.read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0];count=struct.unpack_from('<H',data,pe+6)[0];opt=struct.unpack_from('<H',data,pe+20)[0]
    sections=[struct.unpack_from('<IIII',data,pe+24+opt+i*40+8) for i in range(count)]
    def read(rva,n):
        vs,va,rs,raw=next(s for s in sections if s[1]<=rva<s[1]+max(s[0],s[2]));off=raw+rva-va
        assert rva-va+n<=rs;return data[off:off+n].hex()
    sites=load(PARENT/'sites.json')
    for row in sites.values():assert read(row['rva'],16)==row['prefix']
    for name,rva in dict(puppet_tick=0x1db8a0,damage=0x1a1370,scene=0x1eff40,sprite=0x1eb650,vertex=0x55f40).items():sites[name]=dict(rva=rva,prefix=read(rva,16))
    # Exact caller/entry stack-5 proof: factory prologue advances RSP by 0x58.
    assert read(0x1d4adf,7)=='8b942480000000'
    dump(OUT/'SITES.json',sites)
    exports=[]
    for p in sorted((OUT/'static').glob('G_*.json')):
        d=load(p);assert d['binary_sha256']==EXPECTED
        exports.append(dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)))
    base=ROOT/'docs/evidence/m1/HOST_BACKGROUND_53/static/G_FUNC_1401b7700.json'
    assert sha(base)=='6abd5c2b35c8cede0dc33a583f7671518a0baf4c00ab971e8c5b0d8971f74dae'
    exports.append(dict(path=base.relative_to(ROOT).as_posix(),sha256=sha(base)))
    dump(OUT/'B_STATIC_GATE.json',dict(status='PASS',binary_sha256=EXPECTED,sites=sites,exports=exports,
        verified_static=dict(scene='1eff40 RCX=prepared stack view*, RDX=Sector*; numeric copies only',collector='1e9a20 hidden+animation+type+translated AABB frustum gate',sprite='1eb650 ordinary sprite entry -> 55f40 vertices, not automap',factory='RCX factory, EDX type, R8 vec3*, XMM3 yaw, stack entry+28 int32 sector; RAX Actor*',remove='1a02b0 mark hidden/removed, unregister interpolants, enqueue native node reference; drain then later sweep OnRemove/pool-return'),
        old_position_visibility='INFERRED historical below-frustum AABB. Fresh native view must independently evaluate old point before Spawn.',
        fixed_candidate=dict(position=[-7424,-6880,1240],sector=259,bounds=[12,12,24],expected_sector_margins=[116,116,212,212,20,400,64],native_view_all_corners_inside_required=True,camera_writes=0)))
    syntax()
    sources=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in sorted(LAB.iterdir()) if p.suffix in ['.py','.js']]
    dump(OUT/'PRE_NATIVE_GATE.json',dict(status='PASS',utc=stamp(),binary_sha256=EXPECTED,sources=sources,harness_sha256=sha(OUT/'A_HARNESS_GATE.json'),static_sha256=sha(OUT/'B_STATIC_GATE.json'),one_new_kex_run_only=True,production_cpp_dll_changed=False,all_paths_on_s=True))
    print('A/B PRE_NATIVE PASS',len(sources),'frozen sources',flush=True)

if __name__=='__main__':{'fixture':fixture,'freeze':freeze}[sys.argv[1]]()
