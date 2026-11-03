"""Reuse the accepted actual-foreground helper with one owned DOC57 window.
Only OS focus and posted bounded W/S; no synthetic activation or engine writes.
"""
from pathlib import Path
import datetime,json,sys,time,types,traceback
from workflow import ROOT,LAB,OUT,dump,stamp

name,action=sys.argv[1:3];assert name.startswith('N58A_') and action in ['focus','W','S']
ui=types.ModuleType('vp57_ui53')
s=(ROOT/'experiments/m1/background_host/ui53.py').read_text(encoding='utf8')
s=s.replace('from audit import ROOT, OUT, dump, stamp','from workflow import ROOT, OUT, dump, stamp')
s=s.replace("ROOT/'docs/evidence/m1'/('N53_'+role.upper()+'_MAIN_01')",'OUT/'+repr(name))
exec(compile(s,'accepted_ui53_for_doc57','exec'),ui.__dict__)
source=(ROOT/'experiments/m1/background_host/foreground_fixture.py').read_text(encoding='utf8')
start=source.index('u=ui53.u;k=ui53.k');end=source.index('tag=sys.argv[2]')
ns={'ui53':ui,'C':ui.C,'W':ui.W}
exec(compile(source[start:end],'accepted_actual_foreground','exec'),ns)
records=[];result={}
try:
    d,h=ui.window('host');ui.u.ShowWindow(h,9);ok=ui.u.SetForegroundWindow(h);time.sleep(.25)
    assert ui.foreground()['pid']==d['pid'],'Actual foreground required'
    records.append(dict(event='actual_foreground',target_pid=d['pid'],result=bool(ok),foreground=ui.foreground()))
    if action in ['W','S']:
        vk=ord(action);scan=ui.u.MapVirtualKeyW(vk,0);hold=float(sys.argv[3]) if len(sys.argv)>3 else .25;assert 0<hold<=.4
        begin=time.monotonic();assert ui.u.PostMessageW(h,0x100,vk,1|(scan<<16));records.append(dict(event='key_down',key=action,utc=datetime.datetime.now(datetime.timezone.utc).isoformat()))
        try:
            while time.monotonic()-begin<hold:
                assert ui.foreground()['pid']==d['pid'];time.sleep(.04)
                assert ui.u.PostMessageW(h,0x100,vk,1|(scan<<16)|(1<<30))
        finally:
            assert ui.u.PostMessageW(h,0x101,vk,1|(scan<<16)|(1<<30)|(1<<31));records.append(dict(event='key_up',key=action,utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),elapsed_s=time.monotonic()-begin))
        time.sleep(.4)
    result=dict(status='PASS',records=records,foreground_attempts=ns['attempts'],synthetic_focus_messages=0,game_writes=0,kex_calls=0)
except BaseException as e:
    result=dict(status='FAILED',error=str(e),traceback=traceback.format_exc(),records=records,foreground_attempts=ns['attempts']);raise
finally:dump(OUT/name/('INPUT_'+action+'_'+stamp()+'.json'),result)
