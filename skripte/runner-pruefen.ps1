<#
  Prüft, ob ein eigener Windows-Runner alles hat, was die Workflows des EulenSlicers brauchen.

  Läuft als erster Schritt von „EulenSlicer bauen“, „Kompilieren“ und „MSI neu packen“, wenn
  der Job auf einem eigenen Runner läuft (auf windows-latest von GitHub ist alles da) – über
  cmd und Windows PowerShell 5.1, damit es auch dann alles meldet, wenn PowerShell 7 fehlt.
  Auch von Hand auf der VM aufrufbar; runner-einrichten.ps1 ruft es am Ende auf.

    -Fuer bauen        alles (Visual Studio, Windows SDK, Python, Git, .NET SDK, Perl)
    -Fuer kompilieren  ohne .NET SDK und Perl
    -Fuer msi          nur Python, Git und .NET SDK

  Fehlt etwas Nötiges: je Punkt eine Fehlermeldung (steht in GitHub oben im Lauf), Rückgabe 1.
  Empfehlungen (lange Pfade, Platz, Pfadlänge): Warnungen, der Lauf geht weiter.

  Läuft auch in Windows PowerShell 5.1 – deshalb keine Syntax von PowerShell 7 und in
  Zeichenketten keine typografischen Anführungszeichen (PowerShell hält sie für echte).
#>
param(
    [ValidateSet('bauen', 'kompilieren', 'msi')]
    [string]$Fuer = 'bauen'
)
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

$imLauf = [bool]$env:GITHUB_ACTIONS
$script:fehlen = 0
$einrichten = 'auf der VM skripte\runner-einrichten.ps1 als Administrator ausführen'

function Gut([string]$text) { Write-Output "  ok     $text" }
function Fehlt([string]$text) {
    $script:fehlen++
    if ($imLauf) { Write-Output "::error::Runner: $text fehlt ($einrichten)." } else { Write-Output "  FEHLT  $text" }
}
function Hinweis([string]$text) {
    if ($imLauf) { Write-Output "::warning::Runner: $text" } else { Write-Output "  Tipp   $text" }
}

$ich = [Security.Principal.WindowsIdentity]::GetCurrent().Name
Write-Output "Runner $env:RUNNER_NAME auf $env:COMPUTERNAME, Konto $ich, prüfe für: $Fuer"

if ($env:OS -ne 'Windows_NT' -or -not [Environment]::Is64BitOperatingSystem) {
    Fehlt 'Windows (64 Bit)'
}

# Python (anwenden.py, hochladen.py) – nicht der Platzhalter aus dem Microsoft Store.
$py = Get-Command python -ErrorAction SilentlyContinue
$pyText = ''
if ($py -and $py.Source -notlike '*\WindowsApps\*') { $pyText = (& python --version 2>&1 | Out-String).Trim() }
if ($pyText -match 'Python (3)\.(\d+)' -and [int]$Matches[2] -ge 9) {
    & python -m pip --version *> $null
    if ($LASTEXITCODE -eq 0) { Gut "$pyText ($($py.Source))" } else { Fehlt "pip für $pyText" }
} else {
    Fehlt 'Python 3.9 oder neuer (Befehl python, für alle Konten installiert)'
}

# PowerShell 7: alle anderen Schritte der Workflows laufen mit shell: pwsh.
$pwsh = Get-Command pwsh -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if ($pwsh) { Gut "PowerShell $((& pwsh -NoProfile -Command '$PSVersionTable.PSVersion.ToString()' | Out-String).Trim())" } else { Fehlt 'PowerShell 7 (Befehl pwsh)' }

$git = Get-Command git -ErrorAction SilentlyContinue
if ($git) { Gut ((& git --version | Out-String).Trim()) } else { Fehlt 'Git' }

if ($Fuer -ne 'msi') {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs = $null
    if (Test-Path $vswhere) {
        $vs = & $vswhere -products * -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.Component.MSBuild -format json |
            Out-String | ConvertFrom-Json | Select-Object -First 1
    }
    if ($vs -and ([version]$vs.installationVersion).Major -in 16, 17, 18) {
        Gut "$($vs.displayName) $($vs.installationVersion)"
    } else {
        Fehlt 'Visual Studio 2022 (oder 2019/2026) mit C++-Werkzeugen (Workload Desktopentwicklung mit C++)'
    }

    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include'
    $sdk = Get-ChildItem $kits -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' -and (Test-Path (Join-Path $_.FullName 'um\windows.h')) } |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if ($sdk) { Gut "Windows SDK $($sdk.Name)" } else { Fehlt 'Windows 10/11 SDK' }
}

if ($Fuer -ne 'kompilieren') {
    # WiX 5 kommt als dotnet-Werkzeug (msi.ps1 installiert es beim ersten Lauf).
    $sdks = @()
    if (Get-Command dotnet -ErrorAction SilentlyContinue) { $sdks = @(& dotnet --list-sdks 2>$null) }
    $neu = $sdks | ForEach-Object { if ($_ -match '^(\d+)\.') { [int]$Matches[1] } } | Sort-Object -Descending | Select-Object -First 1
    if ($neu -ge 6) { Gut ".NET SDK $neu (für WiX)" } else { Fehlt '.NET SDK 6 oder neuer (für WiX)' }
}

if ($Fuer -eq 'bauen') {
    # OpenSSL in den Abhängigkeiten braucht ein Windows-Perl; das Perl aus Git taugt dafür nicht.
    $perl = Get-Command perl -ErrorAction SilentlyContinue
    $perlOs = ''
    if ($perl) { $perlOs = (& perl -e 'print $^O' 2>$null | Out-String).Trim() }
    $lager = if ($env:EULEN_LAGER) { $env:EULEN_LAGER } elseif ($env:RUNNER_TOOL_CACHE) { Join-Path $env:RUNNER_TOOL_CACHE 'eulen' } else { '' }
    $schonGebaut = $lager -and (Get-ChildItem $lager -Directory -Filter 'd-*' -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName 'fertig.txt') })
    if ($perlOs -eq 'MSWin32') {
        Gut "Perl ($($perl.Source))"
    } elseif ($schonGebaut) {
        Hinweis 'Strawberry Perl fehlt. Jetzt nicht nötig, aber sobald die Abhängigkeiten neu gebaut werden (neue OrcaSlicer-Version).'
    } else {
        Fehlt 'Strawberry Perl (für OpenSSL beim ersten Bauen der Abhängigkeiten)'
    }
}

# Empfehlungen
$lang = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -ErrorAction SilentlyContinue).LongPathsEnabled
if ($lang -eq 1) { Gut 'Lange Pfade eingeschaltet' } else { Hinweis 'Lange Pfade sind aus (LongPathsEnabled). Beim Bauen der Abhängigkeiten kann das scheitern.' }

if ($env:GITHUB_WORKSPACE) {
    $n = $env:GITHUB_WORKSPACE.Length
    if ($n -gt 45) {
        Hinweis "Der Arbeitsordner $env:GITHUB_WORKSPACE ist $n Zeichen lang (bei GitHub etwa 37). Lange Pfade können beim Bauen scheitern - Runner besser unter C:\a einrichten."
    }
    $laufwerk = Split-Path -Qualifier $env:GITHUB_WORKSPACE
    $frei = (Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='$laufwerk'" -ErrorAction SilentlyContinue).FreeSpace
    $brauche = if ($Fuer -eq 'bauen') { 60 } else { 15 }
    if ($frei) {
        $gb = [math]::Floor($frei / 1GB)
        if ($gb -lt $brauche) { Hinweis "Auf $laufwerk sind nur $gb GB frei (empfohlen: $brauche GB)." } else { Gut "$gb GB frei auf $laufwerk" }
    }
}

if ($script:fehlen -gt 0) {
    $was = if ($script:fehlen -eq 1) { 'fehlt 1 Voraussetzung' } else { "fehlen $($script:fehlen) Voraussetzungen" }
    Write-Output ''
    Write-Output "Auf diesem Runner $was. Abhilfe: $einrichten, danach den Runner-Dienst neu starten."
    Write-Output 'Solange wieder bei GitHub bauen: Variable EULEN_RUNNER = github (Settings -> Secrets and variables -> Actions).'
    exit 1
}
Write-Output 'Alles da.'
exit 0
