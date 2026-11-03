# PowerSlave Exhumed Together — Host Launcher Script
# Starts the host listen server and logs telemetry.

param(
    [string]$BindIp = "",
    [int]$Port = 27015,
    [string]$Token = ""
)

Write-Host "=========================================================" -ForegroundColor Cyan
Write-Host "  PowerSlave Exhumed Together - Host Server Launcher     " -ForegroundColor Cyan
Write-Host "=========================================================" -ForegroundColor Cyan
Write-Host ""

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir = Split-Path -Parent $scriptDir
$binPath = Join-Path $rootDir "bin\pse_host.exe"
$logsDir = Join-Path $rootDir "logs"

if (-not (Test-Path $binPath)) {
    Write-Host "ERROR: Host executable not found at $binPath!" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $logsDir)) {
    New-Item -ItemType Directory -Path $logsDir -Force | Out-Null
}

# 1. Detect Radmin VPN IP if not specified
if (-not $BindIp) {
    $radminAdapter = Get-NetAdapter | Where-Object { 
        $_.InterfaceDescription -like "*Radmin*" -or $_.Name -like "*Radmin*" 
    } | Select-Object -First 1

    if ($radminAdapter -and $radminAdapter.Status -eq "Up") {
        $ip = (Get-NetIPAddress -InterfaceIndex $radminAdapter.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue | Select-Object -First 1).IPAddress
        if ($ip) {
            $BindIp = $ip
        }
    }
}

if (-not $BindIp) {
    Write-Host "[WARNING] Radmin VPN adapter was not detected or is offline." -ForegroundColor Yellow
    Write-Host "Using fallback default 0.0.0.0 (all local interfaces)." -ForegroundColor Yellow
    $BindIp = "0.0.0.0"
}

Write-Host "---------------------------------------------------------" -ForegroundColor Green
Write-Host "  HOST IP ADDRESS : $BindIp" -ForegroundColor Green
Write-Host "  UDP PORT        : $Port" -ForegroundColor Green
if ($BindIp -like "26.*") {
    Write-Host "  --> GIVE THIS IP TO YOUR FRIEND: $BindIp" -ForegroundColor Yellow
}
Write-Host "---------------------------------------------------------" -ForegroundColor Green
Write-Host ""

# 2. Check Windows Firewall (Read-Only by default; prompts before any changes)
try {
    $rules = Get-NetFirewallRule -Direction Inbound -Enabled True -ErrorAction SilentlyContinue
    $hasUdpRule = $false
    foreach ($r in $rules) {
        $filter = Get-NetFirewallPortFilter -AssociatedNetFirewallRule $r -ErrorAction SilentlyContinue
        if ($filter -and $filter.Protocol -eq "UDP" -and ($filter.LocalPort -contains "$Port" -or $filter.LocalPort -contains "Any")) {
            $hasUdpRule = $true
            break
        }
    }

    if (-not $hasUdpRule) {
        Write-Host "[NOTICE] Windows Firewall does not have a dedicated inbound rule for UDP $Port." -ForegroundColor Yellow
        Write-Host "If your friend cannot connect, inbound UDP packets may be dropped by Windows." -ForegroundColor Gray
        $response = Read-Host "Would you like to add a firewall rule for UDP $Port now? (y/N)"
        if ($response -and $response.Trim().ToLower() -eq "y") {
            Write-Host "Adding Windows Firewall rule for UDP $Port..." -ForegroundColor Cyan
            Start-Process powershell -Verb RunAs -ArgumentList "-NoProfile -Command New-NetFirewallRule -DisplayName 'PowerSlave Exhumed Together UDP' -Direction Inbound -LocalPort $Port -Protocol UDP -Action Allow" -Wait
        } else {
            Write-Host "Continuing without modifying Windows Firewall." -ForegroundColor Gray
        }
        Write-Host ""
    }
} catch {
    Write-Host "Could not query firewall settings." -ForegroundColor Gray
}

# 3. Prepare logging
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$logFile = Join-Path $logsDir "host_$timestamp.log"
Write-Host "Session logging to: $logFile" -ForegroundColor Cyan
Write-Host "Starting server... Type 'quit' in the console to stop." -ForegroundColor Green
Write-Host ""

# 4. Launch Host Process
$argsList = @("--bind", $BindIp, "--port", $Port)
if ($Token) {
    $argsList += @("--token", $Token)
}

# Execute process and log output
& $binPath $argsList 2>&1 | Tee-Object -FilePath $logFile

Write-Host ""
Write-Host "Host server terminated. Log saved at $logFile" -ForegroundColor Green
