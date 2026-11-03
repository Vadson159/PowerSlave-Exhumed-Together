"""Fresh PID/prefix binding of unchanged DOC48 passive OS shutdown observer."""
from pathlib import Path
import hashlib,json,sys
LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2]
sys.path.insert(0,str(ROOT/'docs/research'));from inventory import PE
run=ROOT/'docs/evidence/m1/M12B_CADENCE_NATIVE_01'
launch=json.loads((run/'launch.json').read_text());artifact=json.loads((run/'NATIVE_ARTIFACT.json').read_text())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
exe=Path(launch['target']);assert sha(exe)==launch['binary_sha256']=='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
assert sha(Path(artifact['config']['dll']))==artifact['config']['dll_sha256']
p=PE(exe);sites=[]
for role,rva in [('dispatch',0x100510),('quit_request',0xed8a0),('engine_quit',0xa5800),('game_pre_shutdown',0x1b0e20)]:
    off=p.off(rva);sites.append(dict(name=role,rva=rva,va=hex(p.base+rva),file_offset=hex(off),prefix16=p.b[off:off+16].hex()))
cfg=dict(pid=launch['pid'],target=launch['target'],simulation_tid=launch['primary_thread_id'],sites=sites)
template=ROOT/'experiments/m1/window_close/close_probe.js'
dest=run/'close_probe_bound.js';dest.write_text('const cfg = '+json.dumps(cfg)+';\n'+template.read_text(),encoding='utf8')
(run/'CLOSE_PROBE.json').write_text(json.dumps(dict(config=cfg,source_sha256=sha(dest),unchanged_DOC48_template_sha256=sha(template)),indent=2))
print(json.dumps(cfg))
