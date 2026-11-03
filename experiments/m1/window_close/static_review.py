"""Retain exact-build reused dossiers, new narrow review and pre-run gates."""
from pathlib import Path
import datetime, hashlib, json, sys, urllib.request
LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2]
OUT=ROOT/'docs/evidence/m1/M12B_WINDOW_CLOSE_48'
sys.path.insert(0,str(ROOT/'docs/research'))
from inventory import PE
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def dump(p,o):
    with p.open('x',encoding='utf8') as f:json.dump(o,f,indent=2)
exe=ROOT/'experiments/m0/game/anubis_Shipping_Steam_x64.exe';pe=PE(exe)
expected='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
assert sha(exe)==expected
dirs={
 0x100d80:'docs/evidence/m0_static_final_acceptance/G_FUNC_140100d80.json',
 0x100510:'docs/evidence/m0_static_final_acceptance/G_FUNC_140100510.json',
 0xed8a0:'docs/evidence/m0_static_final_acceptance/G_FUNC_1400ed8a0.json',
 0xa5800:'docs/evidence/m1/M12B_WINDOW_CLOSE_48/static/G_FUNC_1400a5800.json',
 0xa4580:'docs/evidence/m1/M12B_WINDOW_CLOSE_48/static/G_FUNC_1400a4580.json',
 0x1b0e20:'docs/evidence/ghidra_2022/G_FUNC_1401b0e20.json',
 0xfcc20:'docs/evidence/m0_static_host_death/G_FUNC_1400fcc20.json',
 0xa4c90:'docs/evidence/m0_static_final_acceptance/G_FUNC_1400a4c90.json',
 0xfc580:'docs/evidence/m0_static_final_acceptance/G_FUNC_1400fc580.json'}
coordinates=[]
for rva,f in dirs.items():
    p=ROOT/f;d=json.loads(p.read_text(encoding='utf8'));assert d['binary_sha256']==expected
    off=pe.off(rva)
    coordinates.append(dict(binary_sha256=expected,va=hex(pe.base+rva),rva=hex(rva),file_offset=hex(off),
         prefix16=pe.b[off:off+16].hex(),evidence=f,evidence_sha256=sha(p),
         function_name=d['function']['name'],names='FUN addresses are analysis names, not recovered C++ method names'))
tables=[]
for rva,slots in [(0x5189d0,[0x80,0x88,0x90]),(0x511158,[0x10,0x20]),(0x52ad08,[0x18])]:
    tables.append(dict(va=hex(pe.base+rva),rva=hex(rva),file_offset=hex(pe.off(rva)),
                  slots=[dict(slot=hex(s),value=hex(pe.u64(pe.off(rva)+s))) for s in slots]))
primary=[]
for rel in ['src/video/windows/SDL_windowsevents.c','src/events/SDL_windowevents.c']:
    url='https://raw.githubusercontent.com/libsdl-org/SDL/release-2.0.14/'+rel
    dest=OUT/'static'/Path(rel).name
    dest.write_bytes(urllib.request.urlopen(url).read())
    primary.append(dict(url=url,path=str(dest.relative_to(ROOT)),sha256=sha(dest),
        status='Upstream release2.0.14 source; matching resource version, NOT proof of byte-identical shipped SDL build'))
sources=[]
for relative in ['experiments/m1/native_bootstrap/src/bootstrap.cpp','experiments/m1/network_input/src/network_input.cpp',
                 'tools/enet-prototype/src/enet_worker.cpp','tools/enet-prototype/src/enet_host.cpp',
                 'tools/enet-prototype/src/thread_bridge.cpp']:
    p=ROOT/relative;sources.append(dict(path=relative,sha256=sha(p),line_count=len(p.read_text(encoding='utf8').splitlines())))
dump(OUT/'STATIC_REVIEW.json',dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),binary_sha256=expected,
 sdl_sha256=sha(ROOT/'experiments/m0/game/SDL2.dll'),sdl_resource_version='2.0.14.0',coordinates=coordinates,tables=tables,sources=sources,primary_sources=primary,
 static_path=['WM_CLOSE -> SDL window CLOSE -> SDL_QUIT (upstream SDL source inference; observe shipped DLL in one run)',
 '100d80 SDL_PollEvent -> 100510 dispatch type100/101 -> platform+88 -> ed8a0',
 'ed8a0 -> thread interface+28 (f1d60, semantics not promoted to a new API contract) -> engine+20; a4580 initializes engine vtable511158 -> a5800',
 'a5800 -> game+18 (table52ad08 -> 1b0e20: config/list shutdown) -> platform+90 (table5189d0 -> fcc20)',
 'hookShutdown -> unbind -> Session.stop -> Worker.stop/join -> restoreControls -> one Reset -> original fcc20',
 'fcc20 -> engine+10 (a4c90) -> SDL subsystem/window/Quit -> platform+80 (fc580) -> imported _exit(0)'],
 lifetime=['KEX only on attested main/simulation TID. Window dispatch uses main event pump; run must verify actual TID and live tables.',
 'Worker receives transport DTOs only; acquire/release running flag; join precedes node unique_ptr.reset.',
 'Worker callbacks do not acquire lifecycleMutex or call KEX. poll1 plus finite tested client load; terminal pending drain250ms. No hard realtime bound under flood/OS stalls claimed.',
 'Session stopped bool + native resetAttempted bound repeated cleanup. ENet host stop running guard/null host prevents double socket close.',
 'Control restoration follows network join and precedes Reset. Lifecycle hooks, pinned DLL/static Context, Session, trampolines retained until process reclaim (DOC43). No loader lock join or FreeLibrary.',
 'Game+18 precedes platform shutdown; accepted Direct Quit ABI/lifetime reused, early list cleanup not claimed to be world destruction. No arbitrary cleanup moved ahead of it.'],
 remaining_unknown=['Actual shipped SDL WM_CLOSE route, actual thread IDs, live network shutdown/OS wait/socket ordering require new runtime.',
 'Crash/forced exit/hot unload/malicious continuous traffic outside task; one WM_CLOSE run cannot establish all schedules.'],
 production_changes_required=False,static_gate='PASS for one bounded observed runtime; no unknown ABI invoked by diagnostics',
 P2_2='NOT FIXED; cadence, ENet throttle/channel and timeout byte-identical'))
print('STATIC_REVIEW saved')
