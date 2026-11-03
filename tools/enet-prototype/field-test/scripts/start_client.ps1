# PowerSlave Exhumed Together — Client Launcher Script
# Connects to host via Radmin VPN and logs telemetry.

param(
    [string]$HostIp = "",
    [int]$Port = 27015,
    [string]$PlayerName = "",
    [string]$Token = ""
)

Write-Host "=========================================================" -ForegroundColor Cyan
Write-Host "  PowerSlave Exhumed Together - Client Node Launcher     " -ForegroundColor Cyan
Write-Host "=========================================================" -ForegroundColor Cyan
Write-Host ""

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir = Split-Path -Parent $scriptDir
$binPath = Join-Path $rootDir "bin\pse_client.exe"
$logsDir = Join-Path $rootDir "logs"

if (-not (Test-Path $binPath)) {
    Write-Host "ERROR: Client executable not found at $binPath!" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $logsDir)) {
    New-Item -ItemType Directory -Path $logsDir -Force | Out-Null
}

# 1. Prompt for Host IP if not provided
if (-not $HostIp) {
    Write-Host "Enter the Host IP provided by your friend (usually starts with 26.):" -ForegroundColor Yellow
    $inputHost = Read-Host "Host IP"
    if ($inputHost -and $inputHost.Trim()) {
        $HostIp = $inputHost.Trim()
    } else {
        Write-Host "Defaulting to 127.0.0.1 for local test." -ForegroundColor Gray
        $HostIp = "127.0.0.1"
    }
}

# 2. Prompt for Player Name if not provided
if (-not $PlayerName) {
    $defaultName = if ($env:USERNAME) { "Player_" + $env:USERNAME } else { "Player_Client" }
    $inputName = Read-Host "Enter your player name [default: $defaultName]"
    if ($inputName -and $inputName.Trim()) {
        $PlayerName = $inputName.Trim()
    } else {
        $PlayerName = $defaultName
    }
}

Write-Host ""
Write-Host "---------------------------------------------------------" -ForegroundColor Green
Write-Host "  TARGET HOST   : $HostIp : $Port" -ForegroundColor Green
Write-Host "  PLAYER NAME   : $PlayerName" -ForegroundColor Green
Write-Host "---------------------------------------------------------" -ForegroundColor Green
Write-Host ""

# 3. Test Host Ping (Informative only)
Write-Host "Checking connection to $HostIp..." -NoNewline
$ping = Test-Connection -ComputerName $HostIp -Count 2 -ErrorAction SilentlyContinue
if ($ping) {
    $avgMs = ($ping | Measure-Object -Property ResponseTime -Average).Average
    Write-Host " REACHABLE (Ping: $([math]::Round($avgMs, 1)) ms)" -ForegroundColor Green
} else {
    Write-Host " NO PING RESPONSE (Host offline or ICMP blocked)" -ForegroundColor Yellow
    Write-Host " Tip: Ensure both computers are in the SAME Radmin VPN room." -ForegroundColor Gray
    Write-Host " Attempting UDP connection anyway..." -ForegroundColor Gray
}
Write-Host ""

# 4. Prepare logging
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$logFile = Join-Path $logsDir "client_$timestamp.log"
Write-Host "Session logging to: $logFile" -ForegroundColor Cyan
Write-Host "Connecting to host..." -ForegroundColor Green
Write-Host "Interactive commands: 'status', 'move 1 0', 'shoot', 'respawn', 'stat', 'quit'" -ForegroundColor Gray
Write-Host ""

# 5. Launch Client Process
$argsList = @(
    "--connect", "$($HostIp):$Port",
    "--name", $PlayerName,
    "--id", "client_$($PlayerName)_$timestamp"
)
if ($Token) {
    $argsList += @("--token", $Token)
}

& $binPath $argsList 2>&1 | Tee-Object -FilePath $logFile

Write-Host ""
Write-Host "Client session terminated. Log saved at $logFile" -ForegroundColor Green
