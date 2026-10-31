# Builds dist/ and the zip from build-win32 on Windows (no Wine), the same layout scripts/release.sh publishes.
# Usage: scripts\montar_pacote_windows.ps1 -Saida <folder>    (nothing is published)
param([string]$Saida = "$PSScriptRoot\..\release")
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..").Path
$b = "$root\build-win32"; $d = "$root\dist"
$ver = (Select-String -Path "$root\src\common\version.h" -Pattern '#define LBN_MOD_VERSION\s+"([0-9.]+)"').Matches[0].Groups[1].Value
if (-not $ver) { throw "LBN_MOD_VERSION nao encontrado em version.h" }
Copy-Item "$b\LbnCoopSA.asi", "$b\LbnCoopSA-Iniciar.exe" $d -Force
[IO.File]::WriteAllText("$d\LbnCoopSA.version", "$ver`n")
Copy-Item "$b\LbnCoopSA-Server.exe" "$d\extras\" -Force
Copy-Item "$b\lbn_testclient.exe" "$d\extras\LbnCoopSA-TesteRede.exe" -Force
# Same format as sha256sum (the launcher compares dinput8.dll against this file).
$files = "LbnCoopSA.asi", "LbnCoopSA-Iniciar.exe", "dinput8.dll", "extras/LbnCoopSA-Server.exe", "extras/LbnCoopSA-TesteRede.exe"
$lines = foreach ($f in $files) { (Get-FileHash "$d\$f" -Algorithm SHA256).Hash.ToLower() + "  " + $f }
[IO.File]::WriteAllText("$d\SHA256SUMS.txt", ($lines -join "`n") + "`n")
New-Item -ItemType Directory -Force $Saida | Out-Null
$zip = Join-Path (Resolve-Path $Saida).Path "LbnCoopSA-v$ver.zip"
python -I "$root\scripts\make_zip.py" $d $zip
if ($LASTEXITCODE -ne 0) { throw "make_zip.py falhou" }
$h = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
[IO.File]::WriteAllText("$zip.sha256", "$h  LbnCoopSA-v$ver.zip`n")
"$zip"
"sha256 $h"
