# PowerSlave Exhumed Together — Result Collection & Sanitization Tool
# Packages session logs into an anonymized ZIP archive for sharing.

Write-Host "=========================================================" -ForegroundColor Cyan
Write-Host "  PowerSlave Exhumed Together - Results Collector        " -ForegroundColor Cyan
Write-Host "=========================================================" -ForegroundColor Cyan
Write-Host ""

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir = Split-Path -Parent $scriptDir
$logsDir = Join-Path $rootDir "logs"
$tempStaging = Join-Path $rootDir "temp_staging"

if (-not (Test-Path $logsDir)) {
    Write-Host "No logs directory found at $logsDir." -ForegroundColor Yellow
    exit 0
}

$logFiles = Get-ChildItem -Path $logsDir -Filter "*.log"
if ($logFiles.Count -eq 0) {
    Write-Host "No session logs found in $logsDir." -ForegroundColor Yellow
    Write-Host "Run 'start_host.bat' or 'start_client.bat' first to generate test session data." -ForegroundColor Gray
    exit 0
}

Write-Host "Found $($logFiles.Count) session log file(s)." -ForegroundColor Green

# Prepare clean staging folder
if (Test-Path $tempStaging) {
    Remove-Item -Recurse -Force $tempStaging
}
New-Item -ItemType Directory -Path $tempStaging -Force | Out-Null

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$zipPath = Join-Path $rootDir "field_test_results_$timestamp.zip"

# Sanitize and copy each log file
$userName = $env:USERNAME
foreach ($file in $logFiles) {
    Write-Host "  Sanitizing $($file.Name)..." -ForegroundColor Gray
    $content = Get-Content -Path $file.FullName -Raw -ErrorAction SilentlyContinue
    if ($content) {
        # 1. Redact Windows username path if present
        if ($userName) {
            $content = $content -replace [regex]::Escape($userName), "[USER]"
        }
        # 2. Redact MAC addresses (XX-XX-XX-XX-XX-XX or XX:XX:XX:XX:XX:XX)
        $content = $content -replace "([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})", "[REDACTED_MAC]"
        # 3. Redact potential secret join tokens if logged
        $content = $content -replace "token\s*=\s*\S+", "token=[REDACTED]"
        
        $destFile = Join-Path $tempStaging $file.Name
        Set-Content -Path $destFile -Value $content -Encoding UTF8
    }
}

# Also attach benchmark TEST_RESULTS.md if available
$repoDocResults = Join-Path $rootDir "..\docs\TEST_RESULTS.md"
if (Test-Path $repoDocResults) {
    Copy-Item $repoDocResults (Join-Path $tempStaging "BENCHMARK_SUMMARY.md") -Force
}

# Create ZIP archive using .NET ZipFile
try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory($tempStaging, $zipPath)
    Write-Host ""
    Write-Host "=========================================================" -ForegroundColor Green
    Write-Host "  COLLECTION SUCCESSFUL!" -ForegroundColor Green
    Write-Host "  Results archive created at:" -ForegroundColor Cyan
    Write-Host "  $zipPath" -ForegroundColor Yellow
    Write-Host "=========================================================" -ForegroundColor Green
    Write-Host "You can now send this ZIP file to the development team." -ForegroundColor Gray
} catch {
    Write-Host "Failed to create ZIP archive: $($_.Exception.Message)" -ForegroundColor Red
} finally {
    if (Test-Path $tempStaging) {
        Remove-Item -Recurse -Force $tempStaging
    }
}

Write-Host ""
try {
    if ([Environment]::UserInteractive -and -not [Console]::IsInputRedirected) {
        Write-Host "Press any key to close..."
        $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
    }
} catch {}
