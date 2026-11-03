# PowerSlave Exhumed Together — Network Diagnostics (Read-Only)
# Strictly informative: Does NOT alter firewall, adapter, or OS configuration.

param(
    [string]$TargetHostIp = ""
)

Write-Host "=========================================================" -ForegroundColor Cyan
Write-Host "  PowerSlave Exhumed Together - Network Diagnostic Tool  " -ForegroundColor Cyan
Write-Host "=========================================================" -ForegroundColor Cyan
Write-Host ""

# 1. Inspect Radmin VPN Network Adapter
Write-Host "[1/4] Inspecting Radmin VPN Adapter..." -ForegroundColor Yellow
$radminAdapter = Get-NetAdapter | Where-Object { 
    $_.InterfaceDescription -like "*Radmin*" -or $_.Name -like "*Radmin*" 
} | Select-Object -First 1

$LocalRadminIp = $null

if ($radminAdapter) {
    $statusColor = if ($radminAdapter.Status -eq "Up") { "Green" } else { "Red" }
    Write-Host "  Adapter Name        : $($radminAdapter.Name)" -ForegroundColor Green
    Write-Host "  Description         : $($radminAdapter.InterfaceDescription)"
    Write-Host "  Operational Status  : $($radminAdapter.Status)" -ForegroundColor $statusColor
    Write-Host "  Link Speed          : $($radminAdapter.LinkSpeed)"
    
    $ipInfo = Get-NetIPAddress -InterfaceIndex $radminAdapter.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($ipInfo) {
        Write-Host "  Radmin IPv4 Address : $($ipInfo.IPAddress)" -ForegroundColor Green
        $LocalRadminIp = $ipInfo.IPAddress
    } else {
        Write-Host "  Radmin IPv4 Address : NOT ASSIGNED (Check Radmin VPN connection status)" -ForegroundColor Red
    }
} else {
    Write-Host "  WARNING: Radmin VPN adapter was not detected in Windows Network Connections!" -ForegroundColor Red
    Write-Host "  Please install Radmin VPN (https://www.radmin-vpn.com/) and join the network." -ForegroundColor Yellow
}
Write-Host ""

# 2. Check Windows Firewall for UDP port 27015 (Read-Only)
Write-Host "[2/4] Checking Windows Firewall Inbound Rules (UDP 27015)..." -ForegroundColor Yellow
try {
    $rules = Get-NetFirewallRule -Direction Inbound -Enabled True -ErrorAction SilentlyContinue
    $matchingRule = $null
    foreach ($r in $rules) {
        $filter = Get-NetFirewallPortFilter -AssociatedNetFirewallRule $r -ErrorAction SilentlyContinue
        if ($filter -and $filter.Protocol -eq "UDP" -and ($filter.LocalPort -contains "27015" -or $filter.LocalPort -contains "Any")) {
            $matchingRule = $r
            break
        }
    }

    if ($matchingRule) {
        Write-Host "  Firewall Status     : ALLOWED (Rule: '$($matchingRule.DisplayName)')" -ForegroundColor Green
    } else {
        Write-Host "  Firewall Status     : NO INBOUND RULE FOUND for UDP 27015" -ForegroundColor Yellow
        Write-Host "  Note for HOST       : If remote clients cannot connect, host may need to allow UDP 27015." -ForegroundColor Gray
        Write-Host "  Note for CLIENTS    : Clients do NOT need inbound firewall rules." -ForegroundColor Gray
    }
} catch {
    Write-Host "  Could not query firewall status directly (requires elevated privileges)." -ForegroundColor Gray
}
Write-Host ""

# 3. Check Target Host Reachability (if provided)
if (-not $TargetHostIp) {
    if ([Environment]::UserInteractive) {
        $inputIp = Read-Host "Enter Target Host IP to test (press Enter to skip)"
        if ($inputIp -and $inputIp.Trim()) {
            $TargetHostIp = $inputIp.Trim()
        }
    }
}

if ($TargetHostIp) {
    Write-Host "[3/4] Testing Connectivity to Host: $TargetHostIp..." -ForegroundColor Yellow
    
    # Ping test
    Write-Host "  Pinging $TargetHostIp (3 packets)... " -NoNewline
    $pingResult = Test-Connection -ComputerName $TargetHostIp -Count 3 -ErrorAction SilentlyContinue
    if ($pingResult) {
        $avgRtt = ($pingResult | Measure-Object -Property ResponseTime -Average).Average
        Write-Host "REACHABLE! (Avg RTT: $([math]::Round($avgRtt, 1)) ms)" -ForegroundColor Green
    } else {
        Write-Host "TIMEOUT / UNREACHABLE" -ForegroundColor Red
        Write-Host "  Tip: Ensure both computers are connected to the SAME Radmin VPN network." -ForegroundColor Gray
        Write-Host "  Tip: In Radmin VPN window, verify the indicator next to the peer is GREEN." -ForegroundColor Gray
    }

    # Route lookup
    try {
        $route = Find-NetRoute -RemoteIPAddress $TargetHostIp -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($route) {
            Write-Host "  Routing Interface   : $($route.InterfaceAlias) (Metric: $($route.RouteMetric))" -ForegroundColor Gray
        }
    } catch {}
} else {
    Write-Host "[3/4] Remote Host IP ping test skipped." -ForegroundColor Gray
}
Write-Host ""

# 4. Diagnostics Summary & Next Steps
Write-Host "[4/4] Diagnostic Summary" -ForegroundColor Yellow
if ($radminAdapter -and $radminAdapter.Status -eq "Up" -and $LocalRadminIp) {
    Write-Host "  [OK] Local Radmin VPN interface is ready at $LocalRadminIp" -ForegroundColor Green
    Write-Host "  - To HOST a game   : Run 'start_host.bat' and give $LocalRadminIp to your friend." -ForegroundColor Cyan
    Write-Host "  - To JOIN a game   : Run 'start_client.bat' and enter your friend's 26.x.y.z IP." -ForegroundColor Cyan
} else {
    Write-Host "  [ATTENTION] Radmin VPN needs attention before testing over virtual network." -ForegroundColor Red
    Write-Host "  1. Start the Radmin VPN application on your desktop." -ForegroundColor Yellow
    Write-Host "  2. Ensure the big power button is ON (green indicator)." -ForegroundColor Yellow
    Write-Host "  3. Join the agreed network room with your partner." -ForegroundColor Yellow
}

Write-Host ""
try {
    if ([Environment]::UserInteractive -and -not [Console]::IsInputRedirected) {
        Write-Host "Press any key to close..."
        $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
    }
} catch {}
