"""Read-only original77/protected-profile audit before/after the P4 rework run."""
import json,sys
import integrity as i
OUT=i.ROOT/'docs/evidence/m1/PSE_TOGETHER_P4_REWORK_72'
tag=sys.argv[1]
assert tag in ['BEFORE','AFTER']
game=i.games()
result=dict(game=game,host_sha256=i.sha(i.HOST_EXE),client_sha256=i.sha(i.CLIENT_EXE))
ok=not game['errors'] and not game['client_copy_errors'] and game['original_full_snapshot_equal'] and result['host_sha256']==result['client_sha256']==i.EXPECTED
if tag=='AFTER':
    before=json.loads((OUT/'INTEGRITY_BEFORE.json').read_text(encoding='utf8'))
    ok &= result=={k:v for k,v in before.items() if k!='status'}
result['status']='PASS' if ok else 'FAIL'
with (OUT/f'INTEGRITY_{tag}.json').open('x',encoding='utf8') as f:json.dump(result,f,indent=2)
print('INTEGRITY',tag,result['status'])
raise SystemExit(0 if ok else 1)
