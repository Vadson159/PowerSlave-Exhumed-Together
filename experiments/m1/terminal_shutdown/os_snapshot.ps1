param([Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference='Stop'
$rootPath=(Resolve-Path "$PSScriptRoot/../../..").Path
$resolvedOutput=[IO.Path]::GetFullPath($OutputPath)
if (!$resolvedOutput.StartsWith($rootPath+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $resolvedOutput)) { throw 'Output path must be new and inside workspace' }
$processRows=@(Get-CimInstance Win32_Process -Filter "Name='anubis_Shipping_Steam_x64.exe'" | Select-Object ProcessId,ParentProcessId,CreationDate,ExecutablePath,CommandLine)
$dotnetRows=@(); $threadRows=@()
foreach ($row in $processRows) {
    try {
        $processObject=[Diagnostics.Process]::GetProcessById($row.ProcessId)
        $dotnetRows+= [pscustomobject]@{Pid=$row.ProcessId;HasExited=$processObject.HasExited;StartTimeUtc=$processObject.StartTime.ToUniversalTime().ToString('o');Path=$processObject.MainModule.FileName}
        foreach ($threadObject in $processObject.Threads) {
            try {
                $threadState=$threadObject.ThreadState
                $threadRows+= [pscustomobject]@{Pid=$row.ProcessId;Tid=$threadObject.Id;State=[string]$threadState;WaitReason=$(if($threadState -eq [Diagnostics.ThreadState]::Wait){[string]$threadObject.WaitReason}else{$null});StartAddress=('0x{0:x}' -f $threadObject.StartAddress.ToInt64());UserTimeTicks=$threadObject.UserProcessorTime.Ticks;KernelTimeTicks=$threadObject.PrivilegedProcessorTime.Ticks}
            } catch { $threadRows+= [pscustomobject]@{Pid=$row.ProcessId;Error=$_.Exception.Message} }
        }
        $processObject.Dispose()
    } catch { $dotnetRows+= [pscustomobject]@{Pid=$row.ProcessId;Error=$_.Exception.Message} }
}
$udpRows=@(Get-NetUDPEndpoint -ErrorAction SilentlyContinue | Where-Object {$_.LocalPort -eq 28745 -or $_.OwningProcess -in @($processRows.ProcessId)} | Select-Object LocalAddress,LocalPort,OwningProcess)
[pscustomobject]@{Utc=[DateTime]::UtcNow.ToString('o');ObserverPid=$PID;Processes=$processRows;Dotnet=$dotnetRows;Threads=$threadRows;Udp=$udpRows} | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $resolvedOutput -Encoding UTF8
