"""Generate passive route/OS probe from a fresh launch, exact PE bytes and PID."""
from pathlib import Path
import hashlib, json, sys
LAB=Path(__file__).resolve().parent; ROOT=LAB.parents[2]
sys.path.insert(0,str(ROOT/'docs/research'))
from inventory import PE
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
name=sys.argv[1];assert name=='M12B_WINDOW_CLOSE_01'
run=ROOT/'docs/evidence/m1'/name
launch=json.loads((run/'launch.json').read_text(encoding='utf8'))
artifact=json.loads((run/'NATIVE_ARTIFACT.json').read_text(encoding='utf8'))
exe=ROOT/'experiments/m0/game/anubis_Shipping_Steam_x64.exe'
assert sha(exe)==launch['binary_sha256']=='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
assert sha(Path(artifact['config']['dll']))==artifact['config']['dll_sha256']
pe=PE(exe);sites=[]
for role,rva in [('dispatch',0x100510),('quit_request',0xed8a0),('engine_quit',0xa5800),('game_pre_shutdown',0x1b0e20)]:
    off=pe.off(rva);sites.append(dict(name=role,rva=rva,va=hex(pe.base+rva),file_offset=hex(off),prefix16=pe.b[off:off+16].hex()))
cfg=dict(pid=launch['pid'],target=launch['target'],simulation_tid=launch['primary_thread_id'],sites=sites)
source='const cfg = '+json.dumps(cfg)+';\n'+(LAB/'close_probe.js').read_text(encoding='utf8')
with (run/'close_probe_bound.js').open('x',encoding='utf8') as f:f.write(source)
with (run/'CLOSE_PROBE.json').open('x',encoding='utf8') as f:json.dump(dict(config=cfg,source_sha256=sha(run/'close_probe_bound.js')),f,indent=2)
print(json.dumps(cfg))
