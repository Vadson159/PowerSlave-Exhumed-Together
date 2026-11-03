"""Retain the one synthetic input helper's stdout, traceback and OS identity."""
from pathlib import Path
import json,os,sys,traceback
import ui_hold
root=Path(__file__).resolve().parents[3]
run=root/'docs/evidence/m1/N51_CLIENT_MAIN_01'
tag=sys.argv[1] if len(sys.argv)>1 else ''
assert tag in ['', 'ACTIVE_HOST']
suffix='_'+tag if tag else ''
with (run/('WINDOW_INPUT_HELPER'+suffix+'.log')).open('x',encoding='utf8') as log:
    sys.stdout=log;sys.stderr=log
    result=dict(helper_pid=os.getpid(),method='PostMessageW; existing Default desktop; no KEX calls')
    try:
        sys.argv=['ui_hold','N51_CLIENT_MAIN_01',tag];ui_hold.main();result['status']='COMPLETED'
    except BaseException as e:
        traceback.print_exc();result.update(status='FAILED',error=str(e))
    (run/('WINDOW_INPUT_HELPER'+suffix+'.json')).write_text(json.dumps(result,indent=2),encoding='utf8')
