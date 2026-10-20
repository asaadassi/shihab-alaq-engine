# Makes the engine package Shihab Alaq's installer unpacks:
#   ShihabAlaqEngine.exe, LICENSE, NOTICE.md,
#   engine\<32|64|arm64>\shihab-alaq-engine.dll (+ .lib for compiling mods),
#   engine\<arch>\symsrv_shihab_alaq.dll and msdia140_shihab_alaq.dll (symbol loading, Microsoft redistributables)
# then ShihabAlaqEngine-<version>.zip and SHA256SUMS.txt in -Out.
# Usage (after build): pwsh scripts\package.ps1 -Out dist
param([string]$Out = "dist")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

# The version string, as the engine reports it: major.minor.revision + pre-release tag
$v = Get-Content (Join-Path $root "shared\version.h") -Raw
$num = foreach ($k in "MAJOR", "MINOR", "REVISION") { [regex]::Match($v, "VERSION_$k\s+(\d+)").Groups[1].Value }
$pre = [regex]::Match($v, 'VERSION_PRERELEASE\s+"([^"]*)"').Groups[1].Value
$version = ($num -join ".") + $pre

$stage = Join-Path $root "$Out\stage"
if (Test-Path (Join-Path $root $Out)) { Remove-Item (Join-Path $root $Out) -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
# The app's x64 build lands in x64\Release (the default) or Release, depending on the project settings
$exe = @("x64\Release\ShihabAlaqEngine.exe", "Release\ShihabAlaqEngine.exe") | ForEach-Object { Join-Path $root $_ } |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) { throw "ShihabAlaqEngine.exe not built" }
Copy-Item $exe $stage
Copy-Item (Join-Path $root "LICENSE") $stage
Copy-Item (Join-Path $root "NOTICE.md") $stage

# Microsoft's symbol server client and DIA, renamed so they never clash with another copy loaded in the same process
$kits = "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers"
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$archs = @{
    "32"    = @{ Debuggers = "x86";   Dia = "" }
    "64"    = @{ Debuggers = "x64";   Dia = "amd64" }
    "arm64" = @{ Debuggers = "arm64"; Dia = "arm64" }
}
foreach ($arch in $archs.Keys) {
    $dir = Join-Path $stage "engine\$arch"
    New-Item -ItemType Directory -Force $dir | Out-Null
    foreach ($f in "shihab-alaq-engine.dll", "shihab-alaq-engine.lib") {
        Copy-Item (Join-Path $root "Release\$arch\$f") $dir
    }
    $symsrv = Join-Path $kits "$($archs[$arch].Debuggers)\symsrv.dll"
    $msdia = Join-Path $vs ("DIA SDK\bin\" + $(if ($archs[$arch].Dia) { $archs[$arch].Dia + "\" } else { "" }) + "msdia140.dll")
    foreach ($pair in @(@($symsrv, "symsrv_shihab_alaq.dll"), @($msdia, "msdia140_shihab_alaq.dll"))) {
        if (-not (Test-Path $pair[0])) { throw "Missing $($pair[0])" }
        Copy-Item $pair[0] (Join-Path $dir $pair[1])
    }
}

$zip = Join-Path $root "$Out\ShihabAlaqEngine-$version.zip"
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
Remove-Item $stage -Recurse -Force
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  ShihabAlaqEngine-$version.zip" | Set-Content (Join-Path $root "$Out\SHA256SUMS.txt") -Encoding ascii
Write-Host "ShihabAlaqEngine-$version.zip  $hash"
