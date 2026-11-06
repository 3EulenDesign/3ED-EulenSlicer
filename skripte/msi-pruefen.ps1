<#
  Prüft die fertige MSI, bevor sie ins Portal und als GitHub-Release rausgeht.

    pwsh skripte/msi-pruefen.ps1 -Msi EulenSlicer-2.4.2-b17.msi -Version 2.4.17

  -Msi      die MSI aus skripte/msi.ps1
  -Version  die Version, die msi.ps1 hineingeschrieben hat (in Actions: $env:MSI_VERSION)

  Liest die MSI selbst (Windows Installer, nur lesend) und bricht ab, wenn
    - ProductVersion nicht genau drei Zahlen hat (Windows Installer vergleicht nur drei)
      oder nicht -Version ist,
    - der UpgradeCode nicht der aus installer/EulenSlicer.wxs ist,
    - sie nicht pro Maschine installiert (ALLUSERS = 1),
    - das Major Upgrade fehlt (Upgrade-Tabelle und RemoveExistingProducts).
  In GitHub Actions stehen danach version, upgrade_code, name und sha256 in $env:GITHUB_OUTPUT.

  Läuft in Windows PowerShell 5.1 (dort ist der COM-Zugriff auf Windows Installer erprobt) und
  in PowerShell 7 – deshalb keine Syntax von PowerShell 7, in Zeichenketten keine typografischen
  Anführungszeichen, und die Datei hat eine BOM.
#>
param(
    [Parameter(Mandatory = $true)] [string] $Msi,
    [Parameter(Mandatory = $true)] [string] $Version
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }
$repo = Split-Path -Parent $PSScriptRoot
$msiPfad = (Resolve-Path $Msi).Path
$script:fehler = 0

function Fehler([string]$text) {
    $script:fehler++
    Write-Output "::error::MSI: $text"
}

# Windows Installer über COM, spät gebunden (die Objekte haben keine Typinfo für PowerShell).
function Com($objekt, [string]$name, [string]$art, [object[]]$argumente) {
    $objekt.GetType().InvokeMember($name, [System.Reflection.BindingFlags]::$art, $null, $objekt, $argumente)
}

$installer = New-Object -ComObject WindowsInstaller.Installer
$db = Com $installer 'OpenDatabase' 'InvokeMethod' @($msiPfad, 0) # 0 = nur lesen

function Abfrage([string]$sql) {
    $view = Com $db 'OpenView' 'InvokeMethod' @($sql)
    [void](Com $view 'Execute' 'InvokeMethod' $null)
    $werte = @()
    while ($true) {
        $zeile = Com $view 'Fetch' 'InvokeMethod' $null
        if ($null -eq $zeile) { break }
        $werte += [string](Com $zeile 'StringData' 'GetProperty' @(1))
    }
    [void](Com $view 'Close' 'InvokeMethod' $null)
    return , $werte
}

function Eigenschaft([string]$name) {
    $werte = Abfrage "SELECT ``Value`` FROM ``Property`` WHERE ``Property`` = '$name'"
    if ($werte.Count -gt 0) { return $werte[0] }
    return ''
}

function Guid-Normal([string]$text) { return $text.Trim().Trim('{', '}').ToUpperInvariant() }

$name = Eigenschaft 'ProductName'
$produktVersion = Eigenschaft 'ProductVersion'
$upgradeCode = Eigenschaft 'UpgradeCode'
$alleKonten = Eigenschaft 'ALLUSERS'

# Version: genau drei Zahlen in den Grenzen von Windows Installer, gleich -Version
if ($produktVersion -notmatch '^(\d+)\.(\d+)\.(\d+)$') {
    Fehler "ProductVersion '$produktVersion' hat nicht genau drei Zahlen (Major.Minor.Build)."
} elseif ([int]$Matches[1] -gt 255 -or [int]$Matches[2] -gt 255 -or [int]$Matches[3] -gt 65535) {
    Fehler "ProductVersion '$produktVersion' liegt außerhalb von 255.255.65535."
}
if ($produktVersion -ne $Version) {
    Fehler "ProductVersion ist '$produktVersion', erwartet '$Version'."
}

# UpgradeCode: derselbe wie in der .wxs (darf sich nie ändern)
[xml]$wxs = Get-Content (Join-Path $repo 'installer\EulenSlicer.wxs') -Raw -Encoding utf8
$erwartet = Guid-Normal ([string]$wxs.Wix.Package.UpgradeCode)
if (-not $erwartet) {
    Fehler 'In installer/EulenSlicer.wxs steht kein UpgradeCode.'
} elseif ((Guid-Normal $upgradeCode) -ne $erwartet) {
    Fehler "UpgradeCode ist '$upgradeCode', in installer/EulenSlicer.wxs steht '$erwartet'."
}

# Pro Maschine
if ($alleKonten -ne '1') {
    Fehler "ALLUSERS ist '$alleKonten' statt 1 - die MSI installiert nicht pro Maschine."
}

# Major Upgrade: Zeile in der Upgrade-Tabelle für den eigenen UpgradeCode und RemoveExistingProducts
$zeilen = Abfrage "SELECT ``UpgradeCode`` FROM ``Upgrade`` WHERE ``ActionProperty`` = 'WIX_UPGRADE_DETECTED'"
$upgrades = @($zeilen | ForEach-Object { Guid-Normal $_ })
if ($upgrades -notcontains (Guid-Normal $upgradeCode)) {
    Fehler 'In der Upgrade-Tabelle fehlt das Major Upgrade (WIX_UPGRADE_DETECTED).'
}
$entfernen = Abfrage "SELECT ``Action`` FROM ``InstallExecuteSequence`` WHERE ``Action`` = 'RemoveExistingProducts'"
if ($entfernen.Count -eq 0) {
    Fehler 'RemoveExistingProducts fehlt - die alte Version würde nicht entfernt.'
}

$db = $null
$installer = $null
[GC]::Collect()
[GC]::WaitForPendingFinalizers()

$sha = (Get-FileHash -Algorithm SHA256 $msiPfad).Hash.ToLowerInvariant()
Write-Output "MSI gelesen: $name $produktVersion, UpgradeCode $upgradeCode, ALLUSERS $alleKonten, SHA-256 $sha"
if ($script:fehler -gt 0) { exit 1 }
Write-Output 'MSI ok: drei Stellen, UpgradeCode wie in der .wxs, pro Maschine, Major Upgrade.'

if ($env:GITHUB_OUTPUT) {
    # UTF-8 ohne BOM (Out-File -Encoding utf8 schreibt in 5.1 eine BOM vor den ersten Schlüssel).
    $ausgabe = "version=$produktVersion`nupgrade_code=$upgradeCode`nname=$(Split-Path -Leaf $msiPfad)`nsha256=$sha`n"
    [System.IO.File]::AppendAllText($env:GITHUB_OUTPUT, $ausgabe, (New-Object System.Text.UTF8Encoding $false))
}
