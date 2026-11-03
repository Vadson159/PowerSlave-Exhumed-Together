"""Read-only OS process/UDP journal; no target manipulation."""
import json, subprocess, sys
from audit import OUT, dump, stamp
tag=sys.argv[1];assert tag.replace('_','').isalnum()
cmd=r'''$ErrorActionPreference='Stop'
$p=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(anubis_Shipping_Steam_x64|m12a_native_client|pse_host|pse_client|background_contract|native_client_contract|sequence_live|sequence_contract|r1_live|r2_fixed|routing_contract|admission_contract|timeout_contract|pse_test_suite|pse_test_suite_timer_fixture|pse_stress_test|pse_fault_injection|pse_bridge_acceptance)\.exe$'} | Select-Object Name,ProcessId,ParentProcessId,CreationDate,ExecutablePath,CommandLine)
$u=@(Get-NetUDPEndpoint -ErrorAction SilentlyContinue | Where-Object {$_.LocalPort -in @(28745,28853,27777,27780,28990,28995) -or $_.OwningProcess -in $p.ProcessId} | Select-Object LocalAddress,LocalPort,OwningProcess)
$t=@();foreach($v in $p){$g=Get-Process -Id $v.ProcessId -ErrorAction SilentlyContinue;if($g){$t+=@($g.Threads | Select-Object @{Name='Pid';Expression={$v.ProcessId}},Id,ThreadState)}}
@{utc=[DateTime]::UtcNow.ToString('o');processes=$p;udp=$u;threads=$t} | ConvertTo-Json -Depth 6
'''
r=subprocess.run(['powershell.exe','-NoProfile','-Command',cmd],capture_output=True,encoding='utf8',timeout=30)
assert r.returncode==0,r.stderr
o=json.loads(r.stdout);o['command']=cmd
dump(OUT/('OS_'+tag+'_'+stamp()+'.json'),o);print('OS:',len(o['processes']),'processes,',len(o['udp']),'checked endpoints')
