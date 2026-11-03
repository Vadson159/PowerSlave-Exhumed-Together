"""Bounded headless client start/commands; records process completion separately."""
from pathlib import Path
import datetime, json, subprocess, sys, time
LAB=Path(__file__).resolve().parent
ROOT=LAB.parents[2]
OUT=ROOT/'docs/evidence/m1'
def main():
    name=sys.argv[2]; run=OUT/name
    assert name.replace('_','').isalnum() and (run/'launch.json').is_file()
    if sys.argv[1]=='start':
        with (run/'client_console.log').open('xb') as out:
            proc=subprocess.Popen([str(LAB/'build/Release/m12a_native_client.exe'),str(run)],cwd=run,stdout=out,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        (run/'client_process.json').write_text(json.dumps(dict(pid=proc.pid,started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())),encoding='utf8')
        code=proc.wait(timeout=190)
        (run/'client_exit.json').write_text(json.dumps(dict(pid=proc.pid,exit_code=code)),encoding='utf8')
    elif sys.argv[1]=='sequence':
        # Native host evidence continues independently throughout each phase.
        stages=[('hold',.4),('neutral',.7),('hold',.25),('silent',.6),('hold',.2),('disconnect',.3),('reconnect',.4),('neutral',.4)]
        for mode,seconds in stages:
            (run/'client_control.txt').write_text(mode,encoding='ascii')
            time.sleep(seconds)
        print(json.dumps(dict(completed=stages)))
    else:
        command=sys.argv[1]
        assert command in ['hold','neutral','silent','disconnect','reconnect','quit']
        (run/'client_control.txt').write_text(command,encoding='ascii')
if __name__=='__main__': main()
