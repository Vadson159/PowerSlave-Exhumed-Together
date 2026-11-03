"""Run a whitelisted existing OS/UI helper in the provider's desktop, retaining all output."""
import datetime,json,os,runpy,sys,traceback
from pathlib import Path

root=Path(__file__).resolve().parents[2];run=(root/'docs/evidence/m0'/sys.argv[1]).resolve()
if not run.is_relative_to(root/'docs/evidence/m0') or not (run/'launch.json').exists():raise ValueError('Unknown owned run')
action=sys.argv[2];helpers={'resume':'resume.py','window_state':'window_state.py','ui':'ui.py','ui_start':'ui_start.py'}
if action not in helpers:raise ValueError('Unsupported helper')
stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f');prefix=run/('provider_'+action+'_'+stamp)
record={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'helper_pid':os.getpid(),'run':run.name,'action':action,'arguments':sys.argv[3:],'stage':'starting'}
prefix.with_suffix('.json').write_text(json.dumps(record,indent=2),encoding='utf8')
with prefix.with_suffix('.log').open('w',encoding='utf8') as log:
    sys.stdout=log;sys.stderr=log;sys.argv=[str(root/'experiments/m0'/helpers[action]),run.name]+record['arguments']
    try:runpy.run_path(sys.argv[0],run_name='__main__');record.update(stage='completed',exit_code=0)
    except BaseException as e:
        traceback.print_exc();record.update(stage='failed',error=str(e),exit_code=1)
    log.flush();prefix.with_suffix('.json').write_text(json.dumps(record,indent=2),encoding='utf8')
