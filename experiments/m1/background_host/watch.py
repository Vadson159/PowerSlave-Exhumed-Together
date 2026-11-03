"""DOC53 retained same-kernel-process handles; snapshot BEFORE cleanup on terminal hang."""
import importlib.util,json,os,sys,time
from audit import ROOT, LAB, OUT
spec=importlib.util.spec_from_file_location('terminal_observer53',LAB.parent/'terminal_shutdown/observer.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);m.OUT=OUT
def watch(role):
    assert role in ['host','client'];run=ROOT/'docs/evidence/m1'/('N53_'+role.upper()+'_MAIN_01')
    d=json.loads((run/'launch.json').read_text(encoding='utf8'));h,i=m.open_checked(d['pid'],d['target']);dup=m.W.HANDLE()
    try:
        assert m.Duplicate(m.Current(),h,m.Current(),m.C.byref(dup),0,False,2)
        assert m.Compare(h,dup)
        m.write(run/'OS_HANDLE_IDENTITY.json',dict(utc=m.utc(),observer_pid=os.getpid(),access_requested=m.ACCESS,primary=i,duplicate=m.identity(dup.value),compare_same_kernel_object=True,initial=m.state(h)))
        begin=time.monotonic();seen=None;captured=False;last=None
        with (run/'PROCESS_WAIT.jsonl').open('x',encoding='utf8',buffering=1) as f:
            while time.monotonic()-begin<1200:
                s=m.state(h);ledger=m.ledger(h,run);key=(s['wait'],s['exit_code'],ledger)
                if key!=last:
                    f.write(json.dumps(dict(**s,ledger=ledger,duplicate_wait=m.Wait(dup,0)))+'\n');last=key
                if s['wait']==0:
                    m.write(run/'OS_SIGNALED.json',dict(**s,identity=i,duplicate_wait=m.Wait(dup,0),no_emergency_cleanup=not (run/'EMERGENCY_CLEANUP.json').exists()));break
                assert s['wait']==258 and s['exit_query_ok']
                p=run/'native.jsonl'
                if p.exists():
                    with p.open('rb') as jf:jf.seek(max(0,p.stat().st_size-16384));tail=jf.read()
                    if b'"native_final_exit"' in tail or b'"client_native_final_exit"' in tail:
                        if seen is None:seen=time.monotonic()
                if seen and not captured and time.monotonic()-seen>2:
                    m.write(run/'OS_HANG_STATE.json',dict(**s,identity_now=m.identity(h),identity_start=i,ledger=ledger,duplicate_wait=m.Wait(dup,0)))
                    m.snapshot(h,i,run/'TERMINAL_SNAPSHOT',True);captured=True
                time.sleep(.25)
            else:m.write(run/'OBSERVER_BUDGET_EXHAUSTED.json',m.state(h))
    finally:m.write(run/'OBSERVER_CLOSED.json',dict(utc=m.utc(),close_duplicate=bool(m.CloseHandle(dup)) if dup else None,close_primary=bool(m.CloseHandle(h))))
if __name__=='__main__':watch(sys.argv[1])
