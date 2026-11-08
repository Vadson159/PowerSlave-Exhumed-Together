"""Read-only final P23 raw evidence audit. Does not launch games or REA."""
import argparse,hashlib,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'docs/evidence/m1/P23_NATIVE'
def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def journal(p):
    data=Path(p).read_bytes()
    assert data.endswith(b'\n'),str(p)+' incomplete journal'
    return [json.loads(line) for line in data.splitlines() if line.strip()]
def bits(x):return struct.pack('<f',x)
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--run',default='05');ap.add_argument('--out',required=True);a=ap.parse_args()
    destination=(ROOT/a.out).resolve();assert destination.is_relative_to(ROOT) and not destination.exists()
    checks=[];details={}
    def check(name,ok,**kw):checks.append(dict(name=name,passed=bool(ok),**kw))
    gate=read(OUT/f'PRE_NATIVE{a.run}.json')
    check('frozen pre-native gate',gate['status']=='PASS')
    check('all final sources and their frozen copies',all(sha(ROOT/r['source'])==sha(ROOT/r['path'])==r['sha256'] for r in gate['source_copies']))
    check('frozen DLL/PDB',all(sha(ROOT/r['path'])==r['sha256'] for r in gate['frozen']))
    offline=read(ROOT/gate['offline'])
    check('full CTest and suite summaries',offline['status']=='PASS' and offline['passed']==offline['total_expected']==35)
    source=read(ROOT/'docs/evidence/m1/P23_UI/FINAL_CHECKS02/SOURCE_CHECKS.json')
    for row in source['checks']:
        if row['name'].startswith('wire111 equals') or row['name'].startswith('wire112 equals'):
            files=row['files'];check(row['name'],all(sha(ROOT/f['path'])==sha(ROOT/f['baseline'])==f['baseline_sha256'] for f in files),files=len(files))
    native={};observations={}
    for role in ['host','client']:
        run=OUT/f'P23_{role.upper()}_{a.run}';launch=read(run/'launch.json');native[role]=rows=journal(run/'native.jsonl')
        pid,tid=launch['pid'],launch['primary_thread_id']
        check(role+' exact EXE and immutable run artifact',sha(launch['target'])==launch['binary_sha256']=='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d' and sha(read(run/'NATIVE_ARTIFACT.json')['config']['dll'])==sha(ROOT/gate['artifacts'][role]))
        isolation=read(run/'ISOLATION_GATE.json')
        check(role+' full isolation before native admission',isolation['status']=='PASS' and isolation['pid']==pid and isolation['native_simulation_tid']==tid and isolation['native_gameplay_tick_observed'] and not isolation['truncated'] and not isolation['successful_outside_write_opens'])
        final=read(run/'REA_FINAL.json')['structuredContent']['normalized_result'];messages=final['messages']
        check(role+' full REA final capture',not final['messages_truncated'])
        payload=[m.get('payload',{}) for m in messages if m.get('type')=='send']
        ui=[p for p in payload if p.get('kind')=='p23_observation'];observations[role]=ui
        check(role+' real UI observations',any(p['result']==0 and int(p['presents'])>0 and int(p['overlays'])>0 and int(p['markers'])>0 and int(p['menus'])>0 and int(p['submitted'])==1 and int(p['chats'])>=1 and int(p['errors'])==0 and p['render_tid']==p['sim_tid']==tid for p in ui))
        hooks=[r for r in rows if r['event']=='p23_hook_restore']
        check(role+' five exact40 UI hook restores',{r['site'] for r in hooks}==set(range(5)) and all(r['exact40'] for r in hooks))
        controls=[r for r in rows if r['event'] in ['control_restore','p4_control_restore','client_control_restored','client_update_restored']]
        check(role+' native control restores',len(controls)>=(4 if role=='host' else 3) and all(r['exact16'] for r in controls))
        endings=[r for r in rows if r['event'] in ['native_final_exit','client_native_final_exit']]
        check(role+' native orderly final exit',len(endings)==1 and not endings[0]['failed'] and endings[0]['foreign_calls']==0)
        faults=[r for r in rows if r['event'] in ['native_fault','client_native_fault','native_proxy_fault','blocked']]
        check(role+' no native faults/blocks',not faults,faults=faults)
        identity=read(run/'OS_HANDLE_IDENTITY.json');end=read(run/'OS_SIGNALED.json');exit=read(run/'exit.json')
        check(role+' retained OS exact process exit0',end['pid']==pid==identity['primary']['pid'] and end['identity']==identity['primary'] and identity['compare_same_kernel_object'] and end['identity']['path'].lower()==launch['target'].lower() and end['wait']==end['duplicate_wait']==0 and end['exit_query_ok'] and end['exit_code']==exit['exit_code']==0 and end['no_emergency_cleanup'])
        check(role+' observer closed',(run/'OBSERVER_CLOSED.json').is_file())
        restore=read(run/'RHI_RESTORED.json');stage=read(run/'RHI_STAGE.json');cfg=Path(stage['path'])
        check(role+' exact renderer config restoration',restore['status']=='PASS' and (sha(cfg)==stage['before_sha256'] if stage.get('before_exists',True) else not cfg.exists()))
        arrows=[r for r in rows if r['event']=='p23_ally_arrow']
        check(role+' native Arrow on exact Sim/call seam',bool(arrows) and all(r['tid']==tid and r['caller_rva']==0x1a3271 and r['abi_yaw_xmm2'] for r in arrows))
        chats={r['host_sequence']:(r['slot'],r['generation'],r['name'],r['text']) for r in rows if r['event']=='p23_chat_received'}
        check(role+' exact bidirectional Unicode chat without opening key',any(v==(1,1,'Друг23','Привет от Друг23') for v in chats.values()) and any(v==(0,1,'Хост23','Ответ от Хост23') for v in chats.values()))
        details[role]=dict(pid=pid,tid=tid,arrows=len(arrows),chat=chats,ui=ui,screenshots=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in sorted(run.glob('window*.png'))])
    host=native['host'];captures=[r for r in host if r['event']=='native_snapshot_captured' and r['queued']]
    values={(r['slot'],r['map_epoch'],bits(r['pos'][0]),bits(r['pos'][1]),bits(r['yaw'])) for r in captures}
    for role,rows in native.items():
        arrows=[r for r in rows if r['event']=='p23_ally_arrow']
        check(role+' every logged ally arrow matches exact host native float bits',all((r['slot'],r['epoch'],bits(r['x']),bits(r['y']),bits(r['yaw'])) in values for r in arrows),count=len(arrows))
    client=native['client'];gates=[r for r in client if r['event']=='p23_ui_input_gate']
    check('client capture and release observed',any(r['capture'] and r['scoped_native_neutral'] for r in gates) and any(not r['capture'] and r['scoped_native_neutral'] for r in gates))
    capture=False;neutral=[]
    for r in client:
        if r['event']=='p23_ui_input_gate':capture=r['capture']
        elif capture and r['event']=='client_input_sampled':neutral.append(r)
    check('client gameplay input neutral during composer',bool(neutral) and all(not r['valid'] and r['kex_buttons']==0 and r['forward']==0 for r in neutral),samples=len(neutral))
    result=dict(status='PASS' if all(r['passed'] for r in checks) else 'FAIL',checks=checks,passed=sum(r['passed'] for r in checks),total=len(checks),details=details,scope='Bounded P23 native two-process D3D11/TOMB evidence; independent acceptance required',integrity='Run packaging/check_integrity separately after teardown',screenshots_require_independent_visual_review=True)
    destination.write_text(json.dumps(result,indent=2,ensure_ascii=False),'utf8')
    print(result['status'],result['passed'],'/',result['total'])
    for r in checks:
        if not r['passed']:print('FAIL',r['name'])
    return int(result['status']!='PASS')
if __name__=='__main__':raise SystemExit(main())
