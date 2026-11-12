# Finds the PowerSlave Exhumed folder of the local Steam installation (prints the path, nothing if not found).
$ErrorActionPreference = 'SilentlyContinue'
$libs = New-Object System.Collections.Generic.List[string]
function Add-Lib([string]$p) { if ($p) { $p = ($p -replace '/', [string][char]92); while ($p.Contains([string][char]92 + [string][char]92) -and -not $p.StartsWith([string][char]92 + [string][char]92)) { $p = $p.Replace([string][char]92 + [string][char]92, [string][char]92) }; $p = $p.TrimEnd([char]92); if (-not $libs.Contains($p)) { $libs.Add($p) } } }
# 1) Steam folder from the registry, and every library listed in libraryfolders.vdf
foreach ($key in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam') {
    $v = Get-ItemProperty $key
    $steam = $v.SteamPath; if (-not $steam) { $steam = $v.InstallPath }
    if ($steam) {
        Add-Lib $steam
        $vdf = Join-Path ($steam -replace '/', '\') 'steamapps\libraryfolders.vdf'
        if (Test-Path $vdf) { Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"' | ForEach-Object { Add-Lib $_.Matches[0].Groups[1].Value } }
    }
}
# 2) usual places on every drive
foreach ($d in (Get-PSDrive -PSProvider FileSystem | ForEach-Object { $_.Root })) {
    foreach ($rel in 'SteamLibrary', 'Steam', 'Program Files (x86)\Steam', 'Program Files\Steam', 'Games\Steam', 'Games\SteamLibrary') { Add-Lib (Join-Path $d $rel) }
}
foreach ($lib in $libs) {
    $apps = Join-Path $lib 'steamapps'
    $names = New-Object System.Collections.Generic.List[string]
    $acf = Join-Path $apps 'appmanifest_1678430.acf'
    if (Test-Path $acf) { $m = Select-String -Path $acf -Pattern '"installdir"\s+"([^"]+)"'; if ($m) { $names.Add($m.Matches[0].Groups[1].Value) } }
    foreach ($n in 'PowerSlave Exhumed', 'Powerslave Exhumed', 'PowerslaveEX') { if (-not $names.Contains($n)) { $names.Add($n) } }
    foreach ($n in $names) {
        $p = Join-Path (Join-Path $apps 'common') $n
        if (Test-Path (Join-Path $p 'anubis_Shipping_Steam_x64.exe')) { Write-Output $p; exit 0 }
    }
}
exit 1
