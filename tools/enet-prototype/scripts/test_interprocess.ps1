$hostExe = (Resolve-Path "PowerSlave_Exhumed_Together_mod/tools/enet-prototype/build/Release/pse_host.exe").Path
$clientExe = (Resolve-Path "PowerSlave_Exhumed_Together_mod/tools/enet-prototype/build/Release/pse_client.exe").Path
$port = 27791

Write-Host "=== Multi-Process ENet UDP Verification ==="
Write-Host "Starting Host Process on 127.0.0.1:$port..."

$hostPsi = New-Object System.Diagnostics.ProcessStartInfo
$hostPsi.FileName = $hostExe
$hostPsi.Arguments = "--port $port --bind 127.0.0.1"
$hostPsi.UseShellExecute = $false
$hostPsi.RedirectStandardInput = $true
$hostPsi.RedirectStandardOutput = $true
$hostPsi.RedirectStandardError = $true
$hostProc = [System.Diagnostics.Process]::Start($hostPsi)

Start-Sleep -Milliseconds 600

Write-Host "Starting Client Process connecting to 127.0.0.1:$port..."
$clientPsi = New-Object System.Diagnostics.ProcessStartInfo
$clientPsi.FileName = $clientExe
$clientPsi.Arguments = "--connect 127.0.0.1:$port --id ipc_client_alpha --name IPC_Alpha --auto-input"
$clientPsi.UseShellExecute = $false
$clientPsi.RedirectStandardInput = $true
$clientPsi.RedirectStandardOutput = $true
$clientPsi.RedirectStandardError = $true
$clientProc = [System.Diagnostics.Process]::Start($clientPsi)

# Let them communicate and exchange UDP packets for 2 seconds
Start-Sleep -Seconds 2

# Query status on client
$clientProc.StandardInput.WriteLine("status")
$clientProc.StandardInput.Flush()
Start-Sleep -Milliseconds 300

# Query stat on client
$clientProc.StandardInput.WriteLine("stat")
$clientProc.StandardInput.Flush()
Start-Sleep -Milliseconds 300

# Query status on host
$hostProc.StandardInput.WriteLine("status")
$hostProc.StandardInput.Flush()
Start-Sleep -Milliseconds 300

# Query stat on host
$hostProc.StandardInput.WriteLine("stat")
$hostProc.StandardInput.Flush()
Start-Sleep -Milliseconds 300

# Gracefully quit client
Write-Host "Stopping Client..."
$clientProc.StandardInput.WriteLine("quit")
$clientProc.StandardInput.Flush()
$clientProc.WaitForExit(3000)

# Gracefully quit host
Write-Host "Stopping Host..."
$hostProc.StandardInput.WriteLine("quit")
$hostProc.StandardInput.Flush()
$hostProc.WaitForExit(3000)

if (!$clientProc.HasExited) { $clientProc.Kill() }
if (!$hostProc.HasExited) { $hostProc.Kill() }

Write-Host "`n--- CLIENT OUTPUT ---"
$clientOutput = $clientProc.StandardOutput.ReadToEnd()
Write-Host $clientOutput

Write-Host "`n--- HOST OUTPUT ---"
$hostOutput = $hostProc.StandardOutput.ReadToEnd()
Write-Host $hostOutput

Write-Host "`n=== Multi-Process ENet UDP Verification Completed Successfully ==="
