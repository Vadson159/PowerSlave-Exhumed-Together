"""Preserve the interrupted run without loading its large journals into memory."""
from pathlib import Path
import json
import re
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'docs/evidence/m1/PSE_TOGETHER_P1'
keep={'native_factory_call','factory_return','proxy_spawned','native_remove_request','native_remove_postcondition',
      'deferred_drain_entry','deferred_drain_return','native_on_remove_entry','native_on_remove_return',
      'native_pool_return_entry','deferred_retired','native_proxy_fault','blocked','native_final_exit',
      'client_native_final_exit','world_exit_observed','native_proxy_hook_restored'}
result={'status':'INTERRUPTED_NOT_ACCEPTED','reason':'REA sessions unavailable after continuation; processes absent; no saved exit.json',
        'runs':{},'runtime_acceptance':False}
event=re.compile(r'^\{"event":"([^"]+)"')
for name in ['P1_HOST_02','P1_CLIENT_02']:
    run=OUT/name;events=[];last_stable=None;last_post=None;lines=0
    with (run/'native.jsonl').open(encoding='utf8') as f:
        for lines,line in enumerate(f,1):
            m=event.match(line)
            if not m:continue
            kind=m.group(1)
            if kind in keep:events.append({'line':lines,**json.loads(line)})
            elif kind=='native_proxy_stable':last_stable=json.loads(line)
            elif kind=='post_retire':last_post=json.loads(line)
    result['runs'][name]={'bytes':(run/'native.jsonl').stat().st_size,'lines':lines,'events':events,
                         'last_live_sample':last_stable,'last_post_retire':last_post,
                         'exit_json_exists':(run/'exit.json').is_file(),
                         'observer_budget_exhausted':(run/'OBSERVER_BUDGET_EXHAUSTED.json').is_file()}
with (OUT/'RUN02_INTERRUPTION.json').open('x',encoding='utf8') as f:json.dump(result,f,indent=2,ensure_ascii=False)
print(result['status'],{n:len(r['events']) for n,r in result['runs'].items()})
