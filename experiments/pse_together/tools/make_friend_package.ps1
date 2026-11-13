# Assembles PSE_Together_friend.zip from the CURRENT build (called by make_friend_package.bat).
# Package = pe-play.exe + install.bat/find_game.ps1/README (experiments/pse_together/release) + overlay of the mod files for the friend's own game copy.
param([Parameter(Mandatory = $true)][string]$Root, [Parameter(Mandatory = $true)][string]$Build)
$ErrorActionPreference = 'Stop'
$rel = Join-Path $Root 'experiments\pse_together\release'
$kpf = Join-Path $Root 'experiments\pse_together\assets\coop_sprites_john.kpf'
$out = Join-Path $Root 'friend_pkg\PSE_Together'
$zip = Join-Path $Root 'PSE_Together_friend.zip'

$need = @("$Build\dll\pse_together.dll", "$Build\p27\SDL2.dll", "$Build\p27\pe-together.dll", "$Build\p27\pe-play.exe", $kpf,
          "$rel\install.bat", "$rel\find_game.ps1", "$rel\README.txt", "$rel\host.bat", "$rel\join.bat")
foreach ($f in $need) { if (-not (Test-Path $f)) { throw "Missing: $f" } }

if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force "$out\overlay\game\mods" | Out-Null
Copy-Item "$rel\*" $out
Copy-Item "$Build\p27\pe-play.exe" $out
Copy-Item "$Build\dll\pse_together.dll", "$Build\p27\SDL2.dll", "$Build\p27\pe-together.dll" "$out\overlay\game"
Copy-Item $kpf "$out\overlay\game\mods"

# The loader only starts a payload whose SHA-256 is in pe-together.ini (UTF-16, like the installer of the lab copy).
$hash = (Get-FileHash "$out\overlay\game\pse_together.dll" -Algorithm SHA256).Hash.ToLower()
[IO.File]::WriteAllText("$out\overlay\game\pe-together.ini", "[payload]`r`nsha256=$hash`r`n", [Text.Encoding]::Unicode)

# Build stamp: both players can compare it (the mod must be the same build on both PCs).
$stamp = "Exhumed Together version 1.0`r`nBuild {0:yyyy-MM-dd HH:mm}`r`npse_together.dll sha256 {1}`r`n" -f (Get-Item "$Build\dll\pse_together.dll").LastWriteTime, $hash.Substring(0, 16)
[IO.File]::WriteAllText("$out\VERSION.txt", $stamp, (New-Object Text.UTF8Encoding($false)))

if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $out -DestinationPath $zip
$z = Get-Item $zip
Write-Host ""
Write-Host ("Archive : {0}" -f $z.FullName)
Write-Host ("Size    : {0:N1} MB" -f ($z.Length / 1MB))
Write-Host ("Build   : {0}" -f $stamp.Split("`r`n")[0])
Write-Host ("DLL hash: {0}" -f $hash.Substring(0, 16))
