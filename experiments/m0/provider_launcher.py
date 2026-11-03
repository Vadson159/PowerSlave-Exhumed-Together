"""Run the same isolated launcher through the REA provider; retain startup failures outside run dir.
No desktop switch or privilege changes. Parent and child inherit the authorized provider context.
"""
import datetime,json,os,runpy,sys,traceback
from pathlib import Path

root=Path(__file__).resolve().parents[2];out=root/'docs/evidence/m0'
run=sys.argv[1]
if not run.replace('_','').isalnum():raise ValueError('Invalid run name')
prefix=out/('M0_'+run+'_provider')
prefix.with_suffix('.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'pid':os.getpid(),'run':run,'stage':'before launch.py','desktop_policy':'inherit existing REA provider desktop; no SwitchDesktop/SetThreadDesktop'}),encoding='utf8')
with prefix.with_suffix('.log').open('w',encoding='utf8') as log:
    sys.stdout=log;sys.stderr=log;sys.argv=[str(root/'experiments/m0/launch.py'),'--run',run,'--quiet']
    try:runpy.run_path(sys.argv[0],run_name='__main__')
    except BaseException:
        traceback.print_exc();log.flush();raise SystemExit(1)
