#Requires -RunAsAdministrator
<#
  Richtet eine Windows-VM als eigenen GitHub-Runner für den EulenSlicer ein.

  Installiert über winget, was „EulenSlicer bauen“, „Kompilieren“ und „MSI neu packen“
  brauchen (schon Vorhandenes wird übersprungen, das Skript kann also mehrmals laufen):
    - Visual Studio 2022 Build Tools mit C++ und Windows SDK (wie windows-latest bei GitHub)
    - Git, Python 3.12 (für alle Konten), PowerShell 7
    - .NET SDK 8 (für WiX, das msi.ps1 beim ersten Lauf holt)
    - Strawberry Perl (OpenSSL in den Abhängigkeiten von OrcaSlicer)
  Danach: lange Pfade an, Git mit langen Pfaden, Runner-Dienst neu starten (damit er die
  Programme im PATH findet), zum Schluss runner-pruefen.ps1.

  Ausführen in einer PowerShell als Administrator, im Ordner skripte dieses Repos:
    Set-ExecutionPolicy -Scope Process Bypass -Force
    .\runner-einrichten.ps1
  Mit -DefenderAusnahme C:\a nimmt Microsoft Defender den Runner-Ordner vom Echtzeitschutz
  aus. Das Bauen geht damit deutlich schneller – nur auf einer VM, die nichts anderes tut.

  Den Runner selbst registriert GitHub: Repo → Settings → Actions → Runners → New
  self-hosted runner → Windows x64. Am besten nach C:\a entpacken (kurze Pfade) und bei
  config.cmd „als Dienst“ wählen. Das Skript läuft auch vorher; dann danach einfach noch
  einmal starten, damit der Dienst neu startet.

  Für Windows PowerShell 5.1 geschrieben (das ist auf einer frischen VM da): keine Syntax
  von PowerShell 7, in Zeichenketten keine typografischen Anführungszeichen.
#>
param(
    [string]$DefenderAusnahme
)
# Nicht 'Stop': Windows PowerShell 5.1 bricht sonst ab, sobald ein Programm etwas nach stderr schreibt.
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

function Pfad-Neu {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}

function Da([string]$befehl) {
    $c = Get-Command $befehl -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $c -or $c.Source -like '*\WindowsApps\*') { return $false }
    # Der Runner-Dienst läuft unter einem anderen Konto: es zählt nur, was im PATH für alle steht.
    $ordner = (Split-Path $c.Source -Parent).TrimEnd('\')
    $alle = [Environment]::GetEnvironmentVariable('Path', 'Machine') -split ';' |
        Where-Object { $_ } | ForEach-Object { [Environment]::ExpandEnvironmentVariables($_).TrimEnd('\') }
    return ($alle -contains $ordner)
}

function Installieren([string]$id, [string]$name, [string]$override) {
    Write-Host ''
    Write-Host "== $name ($id)" -ForegroundColor Cyan
    $a = @('install', '--id', $id, '--exact', '--source', 'winget', '--silent', '--accept-package-agreements', '--accept-source-agreements')
    if ($override) { $a += @('--override', $override) }
    & winget @a
    if ($LASTEXITCODE -ne 0) {
        # Auch "Neustart nötig" kommt als Fehlercode – ob es wirklich fehlt, zeigt die Prüfung am Ende.
        Write-Warning "winget meldet $LASTEXITCODE für $name."
    }
    Pfad-Neu
}

if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    throw 'winget fehlt. Unter Windows 10/11 und Server 2025 ist es dabei (App-Installer im Microsoft Store aktualisieren); unter Server 2022 von https://github.com/microsoft/winget-cli/releases installieren.'
}

# Visual Studio: zählt jede Ausgabe (Build Tools, Community, ...) mit C++-Werkzeugen.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = ''
if (Test-Path $vswhere) {
    $vs = & $vswhere -products * -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.Component.MSBuild -property installationVersion
}
if ($vs) {
    Write-Host "Visual Studio mit C++ ist da ($vs)."
} else {
    Installieren 'Microsoft.VisualStudio.2022.BuildTools' 'Visual Studio 2022 Build Tools mit C++ (dauert eine Weile)' `
        '--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
}

if (Da 'git') { Write-Host 'Git ist da.' } else { Installieren 'Git.Git' 'Git' '' }

if (Da 'python') {
    Write-Host 'Python ist da.'
} else {
    Installieren 'Python.Python.3.12' 'Python 3.12 für alle Konten' '/quiet InstallAllUsers=1 PrependPath=1 Include_test=0'
}

if (Da 'pwsh') { Write-Host 'PowerShell 7 ist da.' } else { Installieren 'Microsoft.PowerShell' 'PowerShell 7' '' }

$sdk = $false
if (Da 'dotnet') { $sdk = [bool](& dotnet --list-sdks 2>$null | Where-Object { $_ -match '^([6-9]|\d\d)\.' }) }
if ($sdk) { Write-Host '.NET SDK ist da.' } else { Installieren 'Microsoft.DotNet.SDK.8' '.NET SDK 8' '' }

$perlOs = ''
if (Da 'perl') { $perlOs = (& perl -e 'print $^O' 2>$null | Out-String).Trim() }
if ($perlOs -eq 'MSWin32') { Write-Host 'Perl ist da.' } else { Installieren 'StrawberryPerl.StrawberryPerl' 'Strawberry Perl' '' }

Write-Host ''
Write-Host '== Lange Pfade' -ForegroundColor Cyan
Set-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -Name 'LongPathsEnabled' -Value 1 -Type DWord -ErrorAction Stop
if (Da 'git') { & git config --system core.longpaths true }
Write-Host 'LongPathsEnabled = 1, git core.longpaths = true'

if ($DefenderAusnahme) {
    Write-Host ''
    Write-Host "== Defender: Ausnahme für $DefenderAusnahme" -ForegroundColor Cyan
    Add-MpPreference -ExclusionPath $DefenderAusnahme -ErrorAction Stop
}

Write-Host ''
Write-Host '== Runner-Dienst' -ForegroundColor Cyan
$dienste = @(Get-Service -Name 'actions.runner.*' -ErrorAction SilentlyContinue)
if ($dienste.Count -gt 0) {
    $dienste | ForEach-Object {
        Write-Host "Starte $($_.Name) neu ..."
        Restart-Service -InputObject $_ -Force
    }
} else {
    Write-Host 'Noch kein Runner-Dienst. Runner registrieren (siehe oben im Skript), als Dienst einrichten'
    Write-Host 'und dieses Skript danach noch einmal starten.'
}

Write-Host ''
Write-Host '== Prüfung' -ForegroundColor Cyan
& (Join-Path $PSScriptRoot 'runner-pruefen.ps1') -Fuer bauen
