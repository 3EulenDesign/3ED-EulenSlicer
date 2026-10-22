<#
  Abhängigkeiten von OrcaSlicer auf dem eigenen Runner aufheben und wiederholen.

  Auf windows-latest liegen sie im Zwischenspeicher von GitHub (actions/cache). Ein eigener
  Runner behält sie stattdessen auf seiner Platte, im Werkzeug-Ordner des Runners
  (RUNNER_TOOL_CACHE, z. B. C:\a\_work\_tool) unter eulen\d-<Hash>\OrcaSlicer_dep – oder
  unter dem Ordner aus der Variable EULEN_LAGER. Der Hash ist derselbe wie beim
  Zwischenspeicher (über orca/deps): Eine neue OrcaSlicer-Version baut sie einmal neu.
  Behalten werden die zwei zuletzt benutzten Stände, ältere löscht -Ablegen.

    -Holen   -Hash <hex> -Ziel <Ordner>    kopiert sie nach <Ordner> (build_release_vs.bat sucht
                                           sie unter orca/deps/build/OrcaSlicer_dep)
    -Ordner  -Hash <hex>                   nennt nur den Ordner („Kompilieren“ setzt DEP_BUILD_DIR)
    -Ablegen -Hash <hex> -Quelle <Ordner>  hebt frisch gebaute auf

  Ausgaben (GITHUB_OUTPUT): da = true/false, ordner = Ordner, in dem OrcaSlicer_dep liegt.
#>
[CmdletBinding(DefaultParameterSetName = 'Holen')]
param(
    [Parameter(ParameterSetName = 'Holen', Mandatory = $true)] [switch]$Holen,
    [Parameter(ParameterSetName = 'Ordner', Mandatory = $true)] [switch]$Ordner,
    [Parameter(ParameterSetName = 'Ablegen', Mandatory = $true)] [switch]$Ablegen,
    [Parameter(Mandatory = $true)] [string]$Hash,
    [Parameter(ParameterSetName = 'Holen', Mandatory = $true)] [string]$Ziel,
    [Parameter(ParameterSetName = 'Ablegen', Mandatory = $true)] [string]$Quelle
)
$ErrorActionPreference = 'Stop'

function Ausgabe([string]$name, [string]$wert) {
    if ($env:GITHUB_OUTPUT) { "$name=$wert" | Out-File -Append -FilePath $env:GITHUB_OUTPUT -Encoding utf8 }
    Write-Output "$name=$wert"
}

function Kopieren([string]$von, [string]$nach) {
    # robocopy: schnell bei vielen kleinen Dateien (Boost), kann lange Pfade; Rückgabe unter 8 heißt Erfolg.
    robocopy $von $nach /MIR /MT:16 /R:2 /W:5 /NFL /NDL /NP /NJH
    if ($LASTEXITCODE -ge 8) {
        Write-Output "::error::Kopieren von $von nach $nach ging nicht (robocopy $LASTEXITCODE)."
        exit 1
    }
    $global:LASTEXITCODE = 0
}

if ($Hash -notmatch '^[0-9a-fA-F]{12,}$') {
    Write-Output "::error::Kein gültiger Hash: '$Hash'"
    exit 1
}
if ($env:EULEN_LAGER) {
    $wurzel = $env:EULEN_LAGER
} elseif ($env:RUNNER_TOOL_CACHE) {
    $wurzel = Join-Path $env:RUNNER_TOOL_CACHE 'eulen'
} else {
    Write-Output '::error::Weder EULEN_LAGER noch RUNNER_TOOL_CACHE ist gesetzt – wo sollen die Abhängigkeiten liegen?'
    exit 1
}
# Kurzer Name: die Pfade in den Abhängigkeiten (Boost) sind schon lang genug.
$fach = Join-Path $wurzel ('d-' + $Hash.Substring(0, 12).ToLower())
$marke = Join-Path $fach 'fertig.txt'
$dep = Join-Path $fach 'OrcaSlicer_dep'

if ($Holen -or $Ordner) {
    if (-not ((Test-Path $marke) -and (Test-Path $dep))) {
        Write-Output "Auf diesem Runner liegen noch keine Abhängigkeiten für diesen Stand ($fach)."
        Ausgabe 'da' 'false'
        exit 0
    }
    (Get-Item $marke).LastWriteTime = Get-Date
    Write-Output "Abhängigkeiten vom Runner: $fach ($((Get-Content $marke -Raw).Trim()))"
    if ($Holen) {
        New-Item -ItemType Directory -Force -Path $Ziel | Out-Null
        Kopieren $dep $Ziel
    }
    Ausgabe 'da' 'true'
    Ausgabe 'ordner' $fach
    exit 0
}

# Ablegen: erst vollständig in einen neuen Ordner, dann umbenennen – ein abgebrochener
# Lauf hinterlässt so nie einen halben Stand, der als fertig gilt.
if (-not (Test-Path (Join-Path $Quelle 'usr'))) {
    Write-Output "::error::In $Quelle liegen keine gebauten Abhängigkeiten (usr fehlt)."
    exit 1
}
New-Item -ItemType Directory -Force -Path $wurzel | Out-Null
$neu = "$fach.neu"
if (Test-Path $neu) { Remove-Item $neu -Recurse -Force }
Kopieren $Quelle (Join-Path $neu 'OrcaSlicer_dep')
"gebaut $(Get-Date -Format 'yyyy-MM-dd HH:mm') auf $env:COMPUTERNAME, Lauf $env:GITHUB_RUN_NUMBER" |
    Out-File -FilePath (Join-Path $neu 'fertig.txt') -Encoding utf8
if (Test-Path $fach) { Remove-Item $fach -Recurse -Force }
Rename-Item -Path $neu -NewName (Split-Path $fach -Leaf)
Write-Output "Abhängigkeiten aufgehoben: $fach"
Ausgabe 'ordner' $fach

# Aufräumen: die zwei zuletzt benutzten Stände behalten, Reste abgebrochener Ablagen weg.
Get-ChildItem -Path $wurzel -Directory -Filter 'd-*' |
    Where-Object { $_.Name -notlike '*.neu' -and (Test-Path (Join-Path $_.FullName 'fertig.txt')) } |
    Sort-Object { (Get-Item (Join-Path $_.FullName 'fertig.txt')).LastWriteTime } -Descending |
    Select-Object -Skip 2 |
    ForEach-Object {
        Write-Output "Alter Stand entfernt: $($_.Name)"
        Remove-Item $_.FullName -Recurse -Force
    }
Get-ChildItem -Path $wurzel -Directory -Filter 'd-*.neu' | ForEach-Object { Remove-Item $_.FullName -Recurse -Force }
exit 0
