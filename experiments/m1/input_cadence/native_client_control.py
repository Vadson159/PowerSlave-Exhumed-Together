"""One current headless client plus bounded native Hold/Release/Silence script."""
from pathlib import Path
import datetime,json,subprocess,sys,time
LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2];RUN=ROOT/'docs/evidence/m1/M12B_CADENCE_NATIVE_01'
def main():
    assert json.loads((RUN/'ISOLATION_GATE.json').read_text())['status']=='PASS'
    if sys.argv[1]=='start':
        with (RUN/'client_console.log').open('xb') as f:p=subprocess.Popen([str(LAB/'build/Release/m12a_native_client.exe'),str(RUN)],cwd=RUN,stdout=f,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        (RUN/'client_process.json').write_text(json.dumps(dict(pid=p.pid,utc=datetime.datetime.now(datetime.timezone.utc).isoformat())))
        code=p.wait(timeout=190);(RUN/'client_exit.json').write_text(json.dumps(dict(pid=p.pid,exit_code=code)))
    elif sys.argv[1]=='sequence':
        deadline=time.monotonic()+12
        while time.monotonic()<deadline:
            raw=(RUN/'native.jsonl').read_text();r=[json.loads(s) for s in raw.splitlines() if s.endswith('}')]
            if sum(x['event']=='network_native_update' for x in r)>=60:break
            time.sleep(.05)
        else:raise RuntimeError('Native P2 did not reach 60 real updates')
        stages=[]
        for mode,duration in [('hold',.30),('neutral',.50),('hold',.30),('silent',.45),('neutral',.30)]:
            (RUN/'client_control.txt').write_text(mode,encoding='ascii');stages.append(dict(mode=mode,duration=duration,utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),steady_us=time.perf_counter_ns()//1000));time.sleep(duration)
        (RUN/'CLIENT_SEQUENCE.json').write_text(json.dumps(stages,indent=2))
    elif sys.argv[1]=='quit':(RUN/'client_control.txt').write_text('quit',encoding='ascii')
    else:raise ValueError(sys.argv[1])
if __name__=='__main__':main()
