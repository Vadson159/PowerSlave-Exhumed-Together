"""DOC58B.2 copy (run names/evidence only). Ordinary OS input for the two owned DOC58B windows: actual foreground (accepted DOC53 fixture), posted
bounded W/S keys and bounded relative mouse to the HOST only; the client only ever receives focus.
Usage: input58b.py focus <host|client> | key <W|S|A|D> <hold<=0.4> [handoff] | mouse <dx> | hold <seconds>
handoff: release the host key only after real foreground moved to the client (host decelerates in background)."""
from pathlib import Path
import datetime,json,sys,time,types,traceback
from workflow import ROOT,OUT,dump,stamp
# Run 01 (N58B_HOST_01/N58B_CLIENT_01) ended BLOCKED before Spawn: host P1 left the attested area over a one-way ledge.
RUNS={'host':'N58B2_HOST_01','client':'N58B2_CLIENT_01'}
ui=types.ModuleType('accepted_ui53_58b')
s=(ROOT/'experiments/m1/background_host/ui53.py').read_text(encoding='utf8')
s=s.replace('from audit import ROOT, OUT, dump, stamp','from workflow import ROOT, OUT, dump, stamp')
s=s.replace("ROOT/'docs/evidence/m1'/('N53_'+role.upper()+'_MAIN_01')","OUT/"+repr(RUNS)+"[role]")
exec(compile(s,'accepted_ui53_for_doc58b','exec'),ui.__dict__)
source=(ROOT/'experiments/m1/background_host/foreground_fixture.py').read_text(encoding='utf8')
start=source.index('u=ui53.u;k=ui53.k');end=source.index('tag=sys.argv[2]')
ns={'ui53':ui,'C':ui.C,'W':ui.W};exec(compile(source[start:end],'accepted_actual_foreground','exec'),ns)
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def focus(role,records):
    d,h=ui.window(role);ui.u.ShowWindow(h,9);ok=ui.u.SetForegroundWindow(h);time.sleep(.25)
    fg=ui.foreground();records.append(dict(event='actual_foreground',role=role,target_pid=d['pid'],result=bool(ok),foreground=fg,utc=now()))
    assert fg['pid']==d['pid'],'Actual foreground required: '+role
    return d,h
def main():
    action=sys.argv[1];records=[];result={}
    try:
        if action=='focus':focus(sys.argv[2],records)
        elif action=='hold':t=float(sys.argv[2]);assert 0<t<=30;time.sleep(t);records.append(dict(event='hold',seconds=t,utc=now()))
        elif action=='mouse':
            dx=int(sys.argv[2]);assert abs(dx)<=400;d,h=focus('host',records)
            ui.u.mouse_event.argtypes=[ui.W.DWORD,ui.W.DWORD,ui.W.DWORD,ui.W.DWORD,ui.C.c_size_t]
            for _ in range(4):
                assert ui.foreground()['pid']==d['pid'];ui.u.mouse_event(1,(dx//4)&0xffffffff,0,0,0);records.append(dict(event='relative_mouse',dx=dx//4,utc=now()));time.sleep(.05)
            time.sleep(.4)
        elif action=='key':
            key=sys.argv[2];hold=float(sys.argv[3]);handoff=len(sys.argv)>4 and sys.argv[4]=='handoff'
            assert key in ['W','S','A','D'] and 0<hold<=.4
            d,h=focus('host',records);vk=ord(key);scan=ui.u.MapVirtualKeyW(vk,0);begin=time.monotonic()
            assert ui.u.PostMessageW(h,0x100,vk,1|(scan<<16));records.append(dict(event='key_down',key=key,utc=now()))
            try:
                while time.monotonic()-begin<hold:
                    assert ui.foreground()['pid']==d['pid'];time.sleep(.04);assert ui.u.PostMessageW(h,0x100,vk,1|(scan<<16)|(1<<30))
                if handoff:focus('client',records)
            finally:
                assert ui.u.PostMessageW(h,0x101,vk,1|(scan<<16)|(1<<30)|(1<<31));records.append(dict(event='key_up',key=key,utc=now(),elapsed_s=time.monotonic()-begin,handoff=handoff))
            time.sleep(.3)
        else:raise ValueError(action)
        result=dict(status='PASS',action=sys.argv[1:],records=records,foreground_attempts=ns['attempts'],synthetic_focus_messages=0,game_writes=0,kex_calls=0)
    except BaseException as e:
        result=dict(status='FAILED',action=sys.argv[1:],error=str(e),traceback=traceback.format_exc(),records=records,foreground_attempts=ns['attempts']);raise
    finally:dump(OUT/'inputs'/('INPUT_'+'_'.join(sys.argv[1:3])+'_'+stamp()+'.json'),result)
    print(json.dumps(dict(status=result['status'],records=records[-3:])))
if __name__=='__main__':main()
