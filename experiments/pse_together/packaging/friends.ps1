param(
    [ValidateSet('verify','host','client','collect')][string]$Action = 'verify',
    [string]$Config = 'friends.json',
    [switch]$IncludeDumps
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$kitRoot = $PSScriptRoot

function Assert-RegularTree([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point refused: $Path" }
}
function File-SHA256([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $stream.Dispose(); $algorithm.Dispose() }
}
function Verify-Kit {
    Assert-RegularTree $kitRoot
    $manifest = Get-Content -LiteralPath (Join-Path $kitRoot 'manifest.json') -Raw | ConvertFrom-Json
    if ($manifest.package_id -ne 'PSE-P25-DIAGNOSTIC-1') { throw 'Unknown package ID' }
    foreach ($entry in $manifest.files) {
        if ($entry.path -match '(^/|\\|:|(^|/)\.\.(/|$))') { throw 'Invalid manifest path' }
        $path = Join-Path $kitRoot $entry.path
        $parent = Split-Path $path
        while ($parent.Length -gt $kitRoot.Length) { Assert-RegularTree $parent; $parent = Split-Path $parent }
        Assert-RegularTree $path
        if ((Get-Item -LiteralPath $path).Length -ne $entry.size -or
            (File-SHA256 $path) -ne $entry.sha256) {
            throw "Package hash mismatch: $($entry.path)"
        }
    }
    Write-Host "Package verified: $($manifest.files.Count) files"
}
function Read-Config {
    # Config may only be a plain filename inside the kit. No shell interpolation.
    if ($Config -notmatch '^[A-Za-z0-9_-]+\.json$') { throw 'Config must be a local JSON filename' }
    Assert-RegularTree (Join-Path $kitRoot $Config)
    $c = Get-Content -LiteralPath (Join-Path $kitRoot $Config) -Raw | ConvertFrom-Json
    $required = @('schema','bind','host','port','name','participant_id','token','max_clients','crash_dumps')
    $names = @($c.PSObject.Properties.Name)
    if (@(Compare-Object $required $names).Count) { throw 'Unknown or missing config keys' }
    if ($c.schema -isnot [int] -or $c.schema -ne 1) { throw 'schema must be 1' }
    foreach ($key in @('bind','host')) {
        $address = $null
        if ($c.$key -isnot [string] -or $c.$key -notmatch '^\d{1,3}(\.\d{1,3}){3}$' -or
            ![Net.IPAddress]::TryParse($c.$key, [ref]$address) -or
            $address.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork) { throw "Invalid IPv4: $key" }
        $octets = $address.GetAddressBytes()
        if (!(($octets[0] -eq 127) -or ($octets[0] -eq 10) -or ($octets[0] -eq 26) -or
            ($octets[0] -eq 192 -and $octets[1] -eq 168) -or
            ($octets[0] -eq 172 -and $octets[1] -ge 16 -and $octets[1] -le 31))) {
            throw 'Use a loopback, LAN or Radmin IPv4 address'
        }
    }
    if ($c.port -isnot [int] -or $c.port -lt 1024 -or $c.port -gt 65535) { throw 'port must be 1024..65535' }
    if ($c.max_clients -isnot [int] -or $c.max_clients -lt 1 -or $c.max_clients -gt 7) { throw 'max_clients must be 1..7' }
    foreach ($key in @('name','participant_id','token')) {
        if ($c.$key -isnot [string] -or $c.$key -notmatch '^[A-Za-z0-9_-]{1,32}$') {
            throw "$key must contain 1..32 ASCII letters, digits, underscores or hyphens"
        }
    }
    if ($c.crash_dumps -isnot [bool]) { throw 'crash_dumps must be boolean' }
    return $c
}
try {
    Verify-Kit
    if ($Action -eq 'verify') { $null = Read-Config; Write-Host 'Config valid'; exit 0 }
    if ($Action -ne 'collect') { $c = Read-Config }
    $logRoot = Join-Path $kitRoot 'logs'
    if (!(Test-Path -LiteralPath $logRoot)) { $null = New-Item -ItemType Directory -Path $logRoot }
    Assert-RegularTree $logRoot
    if ($Action -eq 'collect') {
        Add-Type -AssemblyName System.IO.Compression
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $archive = Join-Path $kitRoot ('results_' + [Guid]::NewGuid().ToString('N') + '.zip')
        $zip = [IO.Compression.ZipFile]::Open($archive, [IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($dir in Get-ChildItem -LiteralPath $logRoot -Directory) {
                Assert-RegularTree $dir.FullName
                foreach ($file in Get-ChildItem -LiteralPath $dir.FullName -File) {
                    Assert-RegularTree $file.FullName
                    $entryName = $dir.Name + '/' + $file.Name
                    if ($file.Name -eq 'crash.dmp') {
                        if ($IncludeDumps) { $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $entryName) }
                    } elseif ($file.Name -in @('console.log','diagnostics.log','session.json')) {
                        $raw = [IO.File]::ReadAllText($file.FullName)
                        # These logs are diagnostics, not anonymized telemetry. Redact known paths and MACs.
                        foreach ($private in @($kitRoot, $env:USERPROFILE)) {
                            if ($private) {
                                $raw = [regex]::Replace($raw, [regex]::Escape($private.Replace('\','\\')), '<LOCAL_PATH>', 'IgnoreCase')
                                $raw = [regex]::Replace($raw, [regex]::Escape($private), '<LOCAL_PATH>', 'IgnoreCase')
                            }
                        }
                        $raw = $raw -replace '(?i)\b[0-9a-f]{2}([-:][0-9a-f]{2}){5}\b','<MAC>'
                        $entry = $zip.CreateEntry($entryName)
                        $writer = New-Object IO.StreamWriter($entry.Open())
                        try { $writer.Write($raw) } finally { $writer.Dispose() }
                    }
                }
            }
        } finally { $zip.Dispose() }
        Write-Host "Results: $archive (dumps included: $IncludeDumps)"
        exit 0
    }
    $session = Join-Path $logRoot ($Action + '_' + [DateTime]::UtcNow.ToString('yyyyMMdd_HHmmss_fff') + '_' + [Guid]::NewGuid().ToString('N'))
    if ($Action -eq 'host') {
        $cli = @('--bind', $c.bind, '--port', [string]$c.port, '--max-clients', [string]$c.max_clients, '--token', $c.token)
    } else {
        $cli = @('--connect', ($c.host + ':' + $c.port), '--id', $c.participant_id, '--name', $c.name, '--token', $c.token)
    }
    Write-Host 'Standalone ENet diagnostics. Commands: status, stat, quit. This does not start PowerSlave.'
    if ($c.crash_dumps) {
        & (Join-Path $kitRoot 'bin/pse-diagnostics.exe') $session $Action @cli
    } else {
        $null = New-Item -ItemType Directory -Path $session
        & (Join-Path $kitRoot ('bin/pse_' + $Action + '.exe')) @cli 2>&1 | Tee-Object -FilePath (Join-Path $session 'console.log')
    }
    $code = $LASTEXITCODE
    # Never persist token or complete command line in receipts.
    @{role=$Action; exit_code=$code; crash_capture=$c.crash_dumps; package_id='PSE-P25-DIAGNOSTIC-1'; utc=[DateTime]::UtcNow.ToString('o')} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $session 'session.json') -Encoding UTF8
    Write-Host "Exit: $code. Logs: $session"
    exit $code
} catch {
    Write-Error -ErrorAction Continue $_.Exception.Message
    exit 2
}
