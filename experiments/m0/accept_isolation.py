"""Accept only a recorded, bounded isolation observation for the current owned run.

Does not attach to a PID, call game functions or assert a complete OS filesystem audit.
Save raw REA load/status JSON before invoking. Required before generating gameplay probes.
"""
from pathlib import Path
import argparse, datetime, hashlib, json, ntpath
from prepare import ROOT, OUT, COPY, EXPECTED, digest

def within(path,root):
    try:return ntpath.commonpath([ntpath.normcase(path.removeprefix('\\\\?\\')),ntpath.normcase(str(root))])==ntpath.normcase(str(root))
    except ValueError:return False

def main():
    parser=argparse.ArgumentParser();parser.add_argument('run');parser.add_argument('--follow-up',action='store_true',help='Retain an early gate missing only simulation ticks; evaluate later saved startup observations separately');args=parser.parse_args()
    run=(OUT/args.run).resolve()
    if not run.is_relative_to(OUT.resolve()):raise ValueError('Outside owned evidence root')
    original=run/'ISOLATION_GATE.json'
    target=run/('ISOLATION_GATE_FOLLOWUP.json' if args.follow_up else 'ISOLATION_GATE.json')
    if target.exists():raise ValueError('Retain existing gate; use a new run for changed conditions')
    prior=None
    if args.follow_up:
        old=json.loads(original.read_text(encoding='utf8'))
        if old.get('status')!='FAIL' or old.get('simulation_tick_observed') or not old.get('identity_observed') or any(old.get(k) for k in ['successful_outside_write_opens','script_errors','native_faults','messages_truncated']):
            raise ValueError('Follow-up is limited to an incomplete startup sample missing simulation ticks')
        guard=old.get('guard_summary',{})
        if not guard.get('allowed_disk') or guard.get('errors') or guard.get('unique_overflow'):raise ValueError('Prior guard evidence is not acceptable for a startup follow-up')
        prior={'path':original.name,'sha256':digest(original),'reason':'Initial sample preceded first native simulation tick; retained unchanged'}
    launch=json.loads((run/'launch.json').read_text(encoding='utf8'))
    if Path(launch['target']).resolve()!=(COPY/'anubis_Shipping_Steam_x64.exe').resolve() or digest(Path(launch['target']))!=EXPECTED:raise ValueError('Target identity mismatch')
    records=[];sources=[];truncated=False;errors=[]
    for file in sorted(run.glob('*.json')):
        obj=json.loads(file.read_text(encoding='utf-8-sig'))
        value=obj.get('structuredContent',{}).get('normalized_result',{}) if isinstance(obj,dict) else {}
        messages=value.get('messages',[])
        if not messages:continue
        sources.append({'path':file.name,'sha256':hashlib.sha256(file.read_bytes()).hexdigest()})
        truncated|=value.get('messages_truncated',False)
        for msg in messages:
            if msg.get('type')=='error':errors.append(msg)
            if isinstance(msg.get('payload'),dict):records.append(msg['payload'])
    identities=[r for r in records if r.get('kind')=='observer_identity' and r.get('pid')==launch['pid'] and r.get('binary_sha256_host_verified')==EXPECTED]
    loaded=[r for r in records if r.get('kind')=='isolation_write_guard_loaded' and within(r.get('workspace',''),ROOT)]
    summaries=[r for r in records if r.get('kind')=='isolation_write_guard_summary']
    summary=max(summaries,key=lambda r:r.get('write_opens',0),default={})
    ticks=[r for r in records if r.get('kind')=='tick_sample' and r.get('tick_delta',0)>0 and r.get('leave_count',0)>0 and r.get('simulation_tid')==launch['primary_thread_id']]
    outside=[r for r in records if r.get('kind')=='file_write_open_observed' and r.get('succeeded') and not within(r.get('path',''),ROOT)]
    faults=[r for r in records if r.get('kind') in ['native_exception','C_native_exception']]
    passed=bool(identities and loaded and summary and ticks and summary.get('allowed_disk',0)>0 and not outside and not faults and not errors and not truncated and not summary.get('errors') and not summary.get('unique_overflow'))
    result={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'status':'PASS' if passed else 'FAIL',
      'pid':launch['pid'],'binary_sha256':EXPECTED,'sources':sources,'guard_summary':summary,'successful_outside_write_opens':outside,
      'script_errors':errors,'native_faults':faults,'messages_truncated':truncated,'identity_observed':bool(identities),'simulation_tick_observed':bool(ticks),
      'environment_path_fix_without_denials_observed':passed and summary.get('denied')==0,
      'scope':'Observed NtCreateFile/NtOpenFile guard and CreateFileW paths for this startup only. Not a complete filesystem audit or gameplay acceptance.'}
    if prior:result['follow_up_to']=prior
    with target.open('x',encoding='utf8') as stream:stream.write(json.dumps(result,indent=2))
    print(json.dumps(result));raise SystemExit(not passed)

if __name__=='__main__':main()
