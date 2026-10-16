<#
  MSI des EulenSlicers bauen (WiX Toolset 5, Oberfläche Deutsch).

    pwsh skripte/msi.ps1 -Quelle orca/build/OrcaSlicer -Version 2.4.2 -Build 17

  -Quelle   Ordner, den „cmake install“ von OrcaSlicer füllt (mit orca-slicer.exe)
  -Version  Version von OrcaSlicer (2.4.2)
  -Build    fortlaufende Build-Nummer (dritte Stelle der MSI-Version)
  -Ziel     Ordner für die MSI (Standard: aktueller Ordner)

  Alle Pfade gehen absolut an WiX – WiX sucht relative Pfade vom Ordner der .wxs aus
  (mit einem relativen Pfad entstand eine MSI ohne Dateien).
  In GitHub Actions stehen Pfad und Name der MSI danach in $env:GITHUB_ENV (MSI, MSI_NAME).
#>
param(
    [Parameter(Mandatory = $true)] [string] $Quelle,
    [Parameter(Mandatory = $true)] [string] $Version,
    [Parameter(Mandatory = $true)] [int] $Build,
    [string] $Ziel = "."
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

$quelle = (Resolve-Path $Quelle).Path
$dateien = @(Get-ChildItem $quelle -Recurse -File)
$mb = [math]::Round(($dateien | Measure-Object Length -Sum).Sum / 1MB)
Write-Output "Quelle: $quelle – $($dateien.Count) Dateien, $mb MB"
if ($dateien.Count -lt 1000 -or -not (Test-Path (Join-Path $quelle 'orca-slicer.exe'))) {
    Write-Output "::error::Im Ordner $quelle fehlt der gebaute EulenSlicer."
    exit 1
}

if ($Build -lt 1 -or $Build -gt 65535) {
    Write-Output "::error::Build muss zwischen 1 und 65535 liegen."
    exit 1
}
$teile = $Version.Split('-')[0].Split('.')
$msiVersion = "$([int]$teile[0]).$([int]$teile[1]).$Build"

$icon = Join-Path $quelle 'resources\images\OrcaSlicer.ico'
$lizenz = Join-Path $repo 'installer\Lizenz.rtf'
$wxs = Join-Path $repo 'installer\EulenSlicer.wxs'
foreach ($datei in @($icon, $lizenz, $wxs)) {
    if (-not (Test-Path $datei)) { Write-Output "::error::$datei fehlt."; exit 1 }
}

if (-not (Get-Command wix -ErrorAction SilentlyContinue)) {
    dotnet tool install --global wix --version 5.0.2
    $env:PATH = "$env:PATH;$env:USERPROFILE\.dotnet\tools"
}
wix extension add -g WixToolset.UI.wixext/5.0.2

New-Item -ItemType Directory -Force -Path $Ziel | Out-Null
$msi = Join-Path (Resolve-Path $Ziel).Path "EulenSlicer-$Version-b$Build.msi"
wix build $wxs -arch x64 -ext WixToolset.UI.wixext -culture de-DE `
    -d "Version=$msiVersion" `
    -d "Icon=$icon" `
    -d "Lizenz=$lizenz" `
    -bindpath "Quelle=$quelle" `
    -o $msi
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$groesse = (Get-Item $msi).Length
Write-Output "MSI: $msi, Version $msiVersion, $([math]::Round($groesse / 1MB)) MB"
if ($groesse -lt 30MB) {
    Write-Output "::error::Die MSI ist nur $groesse Byte groß – die Dateien des EulenSlicers fehlen darin."
    exit 1
}
if ($env:GITHUB_ENV) {
    "MSI=$msi" | Out-File -Append -FilePath $env:GITHUB_ENV -Encoding utf8
    "MSI_NAME=$(Split-Path -Leaf $msi)" | Out-File -Append -FilePath $env:GITHUB_ENV -Encoding utf8
}
