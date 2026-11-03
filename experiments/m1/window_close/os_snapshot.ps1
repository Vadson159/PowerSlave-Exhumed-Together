param([Parameter(Mandatory=$true)][string]$Label)
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$outRoot = Join-Path $taskRoot 'docs/evidence/m1/M12B_WINDOW_CLOSE_48'
if ($Label -notmatch '^[a-z_]+$') { throw 'Bad label' }
$processes = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -match '^(anubis_Shipping_Steam_x64|m12a_native_client|pse_host|pse_client)\.exe$'
} | Select-Object Name,ProcessId,ParentProcessId,CreationDate,ExecutablePath,CommandLine)
$udp = @(Get-NetUDPEndpoint -ErrorAction SilentlyContinue | Where-Object {
    $_.LocalPort -eq 28745 -or $_.OwningProcess -in $processes.ProcessId
} | Select-Object LocalAddress,LocalPort,OwningProcess)
$threads = @()
foreach ($item in $processes) {
    $proc = Get-Process -Id $item.ProcessId -ErrorAction SilentlyContinue
    if ($proc) {
        $threads += @($proc.Threads | Select-Object @{Name='Pid';Expression={$item.ProcessId}},Id,ThreadState)
    }
}
$record = @{utc=[DateTime]::UtcNow.ToString('o');processes=$processes;udp=$udp;threads=$threads}
$path = Join-Path $outRoot "OS_$Label.json"
if (Test-Path -LiteralPath $path) { throw 'Evidence exists' }
$record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $path -Encoding utf8
$record | ConvertTo-Json -Depth 6 -Compress
