"""Start the unchanged current client from DOC48's fresh build; no game launch."""
from pathlib import Path
import datetime, json, subprocess
LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2]
run=ROOT/'docs/evidence/m1/M12B_WINDOW_CLOSE_01'
assert (run/'ISOLATION_GATE.json').exists()
with (run/'client_console.log').open('xb') as f:
    p=subprocess.Popen([str(LAB/'build/Release/m12a_native_client.exe'),str(run)],cwd=run,
                       stdout=f,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
with (run/'client_process.json').open('x') as f:json.dump(dict(pid=p.pid,utc=datetime.datetime.now(datetime.timezone.utc).isoformat()),f)
code=p.wait(timeout=190)
with (run/'client_exit.json').open('x') as f:json.dump(dict(pid=p.pid,exit_code=code),f)
