"""Actual compiled clients -> reproducible UDP impairment -> actual Session.
All data saved under DOC49; does not launch/load/instrument a game.
"""
from pathlib import Path
import collections,ctypes,datetime,hashlib,heapq,json,os,random,select,socket,struct,subprocess,sys,threading,time,traceback
from ctypes import wintypes as W
LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2]
OUT=ROOT/'docs/evidence/m1/M12B_CADENCE_49';BIN=LAB/'build/Release'
def us():return time.perf_counter_ns()//1000
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def dump(p,o):p.write_text(json.dumps(o,indent=2),encoding='utf8')
def rows(p):return [json.loads(s) for s in p.read_text(encoding='utf8').splitlines()] if p.exists() else []
def commands(b):
    # Match exact plaintext v1.10 INPUT payloads; identities are also reconciled
    # with enet_peer_send and host decode logs, not inferred from datagram counts.
    out=[];start=0
    while True:
        i=b.find(b'XESP\x10\x01\x10',start)
        if i<0:break
        start=i+1
        if len(b)-i<45:continue
        header_seq=struct.unpack_from('<I',b,i+7)[0];slot=b[i+11]
        gen,seq=struct.unpack_from('<II',b,i+12);forward=struct.unpack_from('<f',b,i+32)[0]
        if header_seq==seq and slot==1 and gen>0 and forward in [0.,1.]:out.append(dict(generation=gen,seq=seq,forward=forward))
    return out
class Proxy(threading.Thread):
    def __init__(self,folder,loss,jitter,lag,seed,burst=False,release_drop=False):
        super().__init__();self.folder=folder;self.loss=loss;self.jitter=jitter;self.lag=lag;self.rng=random.Random(seed)
        self.burst=burst;self.release_drop=release_drop;self.released=False;self.held=False;self.index=0;self.pending=[];self.log=[];self.stopping=False;self.error=None
        self.sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);self.sock.bind(('127.0.0.1',28745));self.sock.setblocking(False);self.client=None
    def run(self):
      try:
        order=0
        while not self.stopping or self.pending:
            now=us()
            while self.pending and self.pending[0][0]<=now:
                due,_,direction,data,dest=heapq.heappop(self.pending);self.sock.sendto(data,dest)
                self.log.append(dict(kind='forward',direction=direction,us=us(),due_us=due,data=data.hex(),inputs=commands(data)))
            if self.stopping and not self.pending:break
            timeout=min(.002,max(0,(self.pending[0][0]-us())/1e6)) if self.pending else .002
            readable,_,_=select.select([self.sock],[],[],timeout)
            if not readable:continue
            for _ in range(256):
                try:data,source=self.sock.recvfrom(65536)
                except BlockingIOError:break
                except ConnectionResetError:
                    # WinSock reports a prior UDP ICMP-unreachable on recvfrom,
                    # commonly after the owned client closes its socket.
                    self.log.append(dict(kind='udp_icmp_unreachable',us=us()));continue
                t=us();direction='s2c' if source[1]==28746 else 'c2s';inp=commands(data)
                if direction=='c2s':self.client=source;dest=('127.0.0.1',28746)
                else:
                    if not self.client:continue
                    dest=self.client
                dropped=False;reason=''
                if direction=='c2s' and inp:
                    self.index+=1
                    ishold=any(c['forward']>0 for c in inp)
                    if ishold:self.held=True
                    if self.release_drop and self.held and not ishold and not self.released:
                        dropped=True;reason='first_release';self.released=True
                    elif self.burst and self.index%90 in range(45,50):dropped=True;reason='burst5'
                    elif self.rng.random()<self.loss:dropped=True;reason='seeded_loss'
                self.log.append(dict(kind='observed',direction=direction,us=t,data=data.hex(),inputs=inp,dropped=dropped,reason=reason))
                if dropped:continue
                delay=max(0,self.lag+self.rng.uniform(-self.jitter,self.jitter));order+=1
                heapq.heappush(self.pending,(t+int(delay*1000),order,direction,data,dest))
      except BaseException:self.error=traceback.format_exc()
      finally:
        self.sock.close()
        with (self.folder/'proxy.jsonl').open('x') as f:
            for r in self.log:f.write(json.dumps(r,separators=(',',':'))+'\n')
def start_client(exe,folder):
    assert exe.resolve().is_relative_to(ROOT) and folder.resolve().is_relative_to(OUT)
    log=(folder/'console.log').open('xb')
    proc=subprocess.Popen([str(exe),str(folder)],cwd=folder,stdout=log,stderr=subprocess.STDOUT,creationflags=4)
    k=ctypes.WinDLL('kernel32',use_last_error=True)
    class TE(ctypes.Structure):_fields_=[('size',W.DWORD),('usage',W.DWORD),('tid',W.DWORD),('pid',W.DWORD),('base',W.LONG),('delta',W.LONG),('flags',W.DWORD)]
    k.CreateToolhelp32Snapshot.argtypes=[W.DWORD,W.DWORD];k.CreateToolhelp32Snapshot.restype=W.HANDLE
    k.Thread32First.argtypes=[W.HANDLE,ctypes.POINTER(TE)];k.Thread32Next.argtypes=k.Thread32First.argtypes;k.CloseHandle.argtypes=[W.HANDLE]
    snap=k.CreateToolhelp32Snapshot(4,0);te=TE();te.size=ctypes.sizeof(te);tids=[]
    if k.Thread32First(snap,ctypes.byref(te)):
        while True:
            if te.pid==proc.pid:tids.append(te.tid)
            if not k.Thread32Next(snap,ctypes.byref(te)):break
    k.CloseHandle(snap);assert len(tids)==1,tids
    k.OpenThread.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenThread.restype=W.HANDLE
    handle=k.OpenThread(0x0802,False,tids[0]);assert handle
    k.ResumeThread.argtypes=[W.HANDLE];k.SuspendThread.argtypes=[W.HANDLE]
    assert k.ResumeThread(handle)==1
    return proc,handle,k,log,tids[0]
def main():
    # Retained real v1.10 INPUT: pitch0/yaw0/forward1. Catches offset confusion.
    assert commands(bytes.fromhex('58455350100110010000000101000000010000000100000000000000000000000000803f000000000000000000'))==[dict(generation=1,seq=1,forward=1.)]
    mode=sys.argv[1];runs=[]
    if mode=='smoke':runs=[('before_smoke','before',0,0,0,1337,False,False),('after_smoke','after',0,0,0,1337,False,False)]
    elif mode=='matrix':
        for client in ['before','after']:
            runs.append((client+'_clean',client,0,0,0,1337,False,False))
            for seed in [1337,2026,4242]:
                for label,loss,lag,jitter,burst in [('loss5',.05,0,0,False),('loss20',.2,0,0,False),('jitter',0,20,15,False),('combined',.1,20,15,False),('burst5',0,0,0,True)]:
                    runs.append((f'{client}_{label}_{seed}',client,loss,lag,jitter,seed,burst,False))
    elif mode=='functional':
        runs=[(c+'_functional',c,0,0,0,1337,False,True) for c in ['before','after']]
    elif mode=='stock':runs=[('stock_before_clean','stock_before',0,0,0,1337,False,False),('stock_after_clean','stock_after',0,0,0,1337,False,False)]
    elif mode=='flush':runs=[(f'after_flush_{seed}','after',.1,20,15,seed,False,False) for seed in [1337,2026,4242]]
    else:raise ValueError(mode)
    wm=ctypes.WinDLL('winmm');wm.timeBeginPeriod(1)
    try:
      for spec in runs:
        name,client,loss,lag,jitter,seed,burst,release_drop=spec
        if len(sys.argv)>2:name=sys.argv[2]+name
        folder=OUT/('NET_'+name)
        if folder.exists():
            prior=json.loads((folder/'META.json').read_text())
            if prior.get('status')=='COMPLETE':print('REUSED_COMPLETE',name,flush=True);continue
            folder=OUT/('NET_'+name+'_icmpfix')
        folder.mkdir()
        hostdir=folder/'host';clientdir=folder/'client';hostdir.mkdir();clientdir.mkdir()
        (clientdir/'client_control.txt').write_text('hold')
        exe=OUT/'stock_before.exe' if client=='stock_before' else BIN/('m12a_native_client.exe' if client=='stock_after' else client+'_qa_client.exe')
        hostexe=BIN/'cadence_host.exe'
        with (hostdir/'console.log').open('xb') as f:host=subprocess.Popen([str(hostexe),'28746'],cwd=hostdir,stdout=f,stderr=subprocess.STDOUT)
        proxy=Proxy(folder,loss,jitter,lag,seed,burst,release_drop);proxy.start()
        timeline=[];proc=None;handle=None;k=None;log=None;meta=dict(spec=spec,client_sha256=sha(exe),host_sha256=sha(hostexe),python_timer_period_ms=1)
        def mark(stage):timeline.append(dict(stage=stage,us=us()))
        def control(cmd):
            (clientdir/'client_control.txt').write_text(cmd,encoding='ascii');mark('control_'+cmd)
        try:
            if mode=='flush':os.environ['PSE_QA_FLUSH']='1'
            else:os.environ.pop('PSE_QA_FLUSH',None)
            proc,handle,k,log,tid=start_client(exe,clientdir);meta.update(pid=proc.pid,primary_tid=tid)
            deadline=time.monotonic()+8
            while time.monotonic()<deadline:
                if proc.poll() is not None or host.poll() is not None:raise RuntimeError('Early exit')
                if (hostdir/'ready.flag').exists():break
                time.sleep(.02)
            else:raise RuntimeError('No ready')
            time.sleep(.3);mark('measure_begin');cpu_begin=cpu_seconds(proc.pid)
            time.sleep(4);mark('measure_end');cpu_end=cpu_seconds(proc.pid);meta['measure_cpu_seconds']=cpu_end-cpu_begin
            control('neutral');time.sleep(.45);control('hold');time.sleep(.3);control('silent');time.sleep(.4);control('neutral');time.sleep(.25)
            if mode=='functional':
                control('hold');time.sleep(.3);control('disconnect');time.sleep(.35);control('reconnect');time.sleep(.6)
                control('hold');time.sleep(.35);mark('stall_begin');assert k.SuspendThread(handle)==0
                time.sleep(.4);mark('stall_end');assert k.ResumeThread(handle)==1;time.sleep(.45)
                mark('release_stall_begin');assert k.SuspendThread(handle)==0
                time.sleep(.12);control('neutral');time.sleep(.23);mark('release_stall_end');assert k.ResumeThread(handle)==1;time.sleep(.5)
            control('quit');proc.wait(timeout=4);meta['client_exit']=proc.returncode;assert proc.returncode==0
            k.CloseHandle(handle);handle=None;log.close();log=None
            if mode=='functional':
                # A fresh process lost its secret key. The existing admission
                # contract must reject it, not inherit the former held input.
                restart=folder/'restart';restart.mkdir();(restart/'client_control.txt').write_text('hold')
                p,h,kk,ll,tt=start_client(exe,restart);mark('restart_no_key');time.sleep(.8)
                (restart/'client_control.txt').write_text('quit');p.wait(timeout=4);kk.CloseHandle(h);ll.close();meta['restart_exit']=p.returncode
            time.sleep(.25);proxy.stopping=True;proxy.join(timeout=3);assert not proxy.is_alive() and not proxy.error,proxy.error
            (hostdir/'host_control.txt').write_text('quit');host.wait(timeout=4);meta['host_exit']=host.returncode;assert host.returncode==0
            meta.update(status='COMPLETE',timeline=timeline)
        except BaseException:
            meta.update(status='ERROR',error=traceback.format_exc(),timeline=timeline)
            if handle and k:
                try:k.ResumeThread(handle)
                except BaseException:pass
            if proc and proc.poll() is None:
                (clientdir/'client_control.txt').write_text('quit')
                try:proc.wait(timeout=4)
                except subprocess.TimeoutExpired:proc.terminate();proc.wait()
            proxy.stopping=True;proxy.join(timeout=4)
            if host.poll() is None:
                (hostdir/'host_control.txt').write_text('quit')
                try:host.wait(timeout=4)
                except subprocess.TimeoutExpired:host.terminate();host.wait()
            dump(folder/'META.json',meta);raise
        dump(folder/'META.json',meta);print(json.dumps(dict(run=name,status=meta['status'],cpu_seconds=meta['measure_cpu_seconds'])),flush=True)
    finally:wm.timeEndPeriod(1);os.environ.pop('PSE_QA_FLUSH',None)
def cpu_seconds(pid):
    k=ctypes.WinDLL('kernel32');k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenProcess.restype=W.HANDLE;k.CloseHandle.argtypes=[W.HANDLE]
    h=k.OpenProcess(0x1000,False,pid);a,b,c,d=W.FILETIME(),W.FILETIME(),W.FILETIME(),W.FILETIME()
    k.GetProcessTimes.argtypes=[W.HANDLE,ctypes.POINTER(W.FILETIME),ctypes.POINTER(W.FILETIME),ctypes.POINTER(W.FILETIME),ctypes.POINTER(W.FILETIME)]
    assert h and k.GetProcessTimes(h,ctypes.byref(a),ctypes.byref(b),ctypes.byref(c),ctypes.byref(d));k.CloseHandle(h)
    return (((c.dwHighDateTime<<32)|c.dwLowDateTime)+((d.dwHighDateTime<<32)|d.dwLowDateTime))/1e7
if __name__=='__main__':main()
