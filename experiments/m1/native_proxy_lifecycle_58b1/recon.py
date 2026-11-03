"""No-proxy launcher/binder. Reuses M0 isolation and owned-window input, never loads a gameplay DLL."""
from workflow import *
import importlib.util, shutil
sys.path.insert(0,str(ROOT/'experiments/m0'))
import prepare
prepare.OUT=OUT;prepare.PROFILE=LAB/'profile'
RUN='N58B1_TRACE_01'
def launch():
    assert sha(prepare.COPY/'anubis_Shipping_Steam_x64.exe')==EXPECTED
    shutil.copy2(ROOT/'docs/evidence/m0/M0_001_PREPARATION.json',OUT/'M0_001_PREPARATION.json')
    # Keep accepted launch behavior except forbidden forced termination on a failed precondition.
    src=(ROOT/'experiments/m0/launch.py').read_text(encoding='utf8').replace("proc.terminate();raise RuntimeError", "raise RuntimeError")
    dump(OUT/'LAUNCH_SOURCE.json',dict(original_sha256=sha(ROOT/'experiments/m0/launch.py'),change='Remove proc.terminate() from ambiguous-thread failure branch; never force terminate'))
    (LAB/'launch_bound.py').write_text(src,encoding='utf8')
    with (OUT/'launcher.log').open('x',encoding='utf8') as f:
        sys.stdout=f;sys.stderr=f;sys.argv=['launch', '--run',RUN,'--quiet']
        exec(compile(src,str(LAB/'launch_bound.py'),'exec'),{'__name__':'__main__','__file__':str(LAB/'launch_bound.py')})
def bind():
    import generate_observer,bind_probe
    sys.argv=['generate_observer',RUN];generate_observer.main()
    sys.argv=['bind_probe',RUN,'isolation'];bind_probe.main()
    d=load(OUT/RUN/'launch.json')
    sys.path.insert(0,str(ROOT/'docs/research'))
    from inventory import PE
    pe=PE(d['target'])
    sites={'state_tick':0x1b6ec0,'player_update':0x1dfe30,'state_exit':0x1b6670,'unload':0x1c90e0,'shutdown':0xfcc20,'arena_cleanup':0xc58b0,'factory':0x1d4780,'remove':0x1a02b0,'drain':0x1b61d0}
    # Additional sites are selected from saved exact-build assembly, not guessed offsets.
    unload=load(ROOT/'docs/evidence/m1/NATIVE_MAP_READY_56/ghidra/G_FUNC_1401c90e0.json')
    asm=unload['assembly'];excerpts=[]
    for i,line in enumerate(asm):
        if '[RBX + 0x28]' in line or '[RBX + 0x30]' in line:excerpts+=asm[max(0,i-2):i+4]
    dump(OUT/'UNLOAD_ASSEMBLY_EXCERPTS.json',dict(source='docs/evidence/m1/NATIVE_MAP_READY_56/ghidra/G_FUNC_1401c90e0.json',source_sha256=sha(ROOT/'docs/evidence/m1/NATIVE_MAP_READY_56/ghidra/G_FUNC_1401c90e0.json'),assembly=excerpts))
    cfg=dict(pid=d['pid'],target=d['target'],simulation_tid=d['primary_thread_id'],log=str(OUT/RUN/'lifecycle.jsonl'),sites={n:dict(rva=r,prefix=pe.b[pe.off(r):pe.off(r)+16].hex()) for n,r in sites.items()})
    dump(OUT/'TRACE_CONFIG.json',cfg)
    (OUT/RUN/'trace.js').write_text('const cfg='+json.dumps(cfg)+';\n'+(LAB/'trace.js').read_text(encoding='utf8'),encoding='utf8')
if __name__=='__main__':
    if sys.argv[1]=='launch':launch()
    elif sys.argv[1]=='bind':bind()
    elif sys.argv[1]=='ui':
        import ui
        sys.argv=['ui',RUN]+sys.argv[2:];ui.main()
    elif sys.argv[1]=='start':
        import runpy
        sys.argv=['ui_start',RUN];runpy.run_path(str(ROOT/'experiments/m0/ui_start.py'),run_name='__main__')
    elif sys.argv[1]=='resume':
        import resume
        sys.argv=['resume',RUN];resume.main()
