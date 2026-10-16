# EulenSlicer

Der EulenSlicer ist [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer) mit ein paar
Anpassungen für 3 Eulen Design – gebaut für Windows als MSI:

- **Druckdateien ins Portal:** Geslicete Dateien gehen mit „Hochladen“ als Druckauftrag
  ins 3 Eulen Portal (Modul „EulenSlicer“ in [3ED-App](https://github.com/3EulenDesign/3ED-App)).
  Gedruckt wird erst nach der Freigabe im Auftrag; dann startet der Drucker-Agent in der
  Werkstatt den Druck. Direkt im LAN drucken geht weiterhin.
- **Updates aus dem Portal:** Beim Start fragt der EulenSlicer das Portal nach einer neueren
  Version, lädt die MSI, prüft ihre Signatur und installiert sie.
- **Eigener Datenordner** (`%APPDATA%\EulenSlicer`) und eigenes Link-Schema
  (`eulenslicer://open?file=…` für „In EulenSlicer öffnen“ im Portal) – ein normaler
  OrcaSlicer kann daneben installiert bleiben.

Alles andere ist OrcaSlicer, unverändert. Dieses Repo enthält nur die Anpassungen; der
Quelltext von OrcaSlicer kommt beim Bauen frisch von GitHub (Version in `orca.version`).

## Aufbau

```
orca.version                 Tag von OrcaSlicer, auf dem gebaut wird (z. B. v2.4.2)
quellen/                     neue Dateien, landen in src/slic3r/GUI/ von OrcaSlicer
  EulenUpdater.cpp/.hpp      Update-Kanal: fragen, laden, prüfen, installieren (wxWidgets)
  EulenUpdaterKern.cpp/.hpp  Kern ohne Oberfläche: Antwort lesen, SHA-256, Ed25519, Installer-Skript
skripte/
  anwenden.py                kopiert quellen/, schreibt EulenKonfig.hpp, ändert die Stellen in OrcaSlicer
  hochladen.py               signiert die MSI und lädt sie in Stücken ins Portal
  msi.ps1                    baut die MSI aus dem Programmordner (WiX 5, absolute Pfade, Größenprüfung)
installer/EulenSlicer.wxs    MSI (WiX Toolset 5, Oberfläche Deutsch): für alle Konten, Startmenü, Desktop, eulenslicer://
installer/Lizenz.rtf         Lizenzhinweis im ersten Fenster der Installation
tests/kern_test.cpp          Test für den Kern (läuft in „Prüfen“)
.github/workflows/
  bauen.yml                  „EulenSlicer bauen“: Windows-Build, MSI, Upload ins Portal
  msi.yml                    „MSI neu packen“: nur die MSI aus dem Programmordner eines Laufs (Minuten statt Stunden)
  kompilieren.yml            „Kompilieren“: übersetzt nur die geänderten Dateien (etwa 15 Minuten), bei jedem Push auf quellen/
  pruefen.yml                „Prüfen“: Kern-Test, Skripte, passen die Stellen zu OrcaSlicer?
  upstream.yml               „OrcaSlicer-Update“: wöchentlich, Pull Request bei neuer OrcaSlicer-Version
```

### Was an OrcaSlicer geändert wird (`skripte/anwenden.py`)

| Datei | Änderung |
|---|---|
| `src/slic3r/GUI/GUI_App.cpp` | `SetAppName("EulenSlicer")` → eigener Datenordner; `associate_url(L"eulenslicer")` statt `orcaslicer`; Versionsprüfung ruft `eulen_update_pruefen()` statt GitHub |
| `src/libslic3r/Utils.hpp`, `src/slic3r/GUI/Downloader.cpp`, `src/slic3r/GUI/InstanceCheck.cpp` | `eulenslicer://open?file=…` wird wie `orcaslicer://` geöffnet |
| `src/slic3r/Utils/OctoPrint.cpp` | Zum Portal (`…/eulen/v1/slicer`) nur mit geprüftem HTTPS-Zertifikat – OrcaSlicer prüft sonst keine Zertifikate, und der API-Schlüssel ist ein Anwendungspasswort |
| `src/slic3r/GUI/Plater.cpp` | Fenstertitel „… – EulenSlicer“ |
| `src/slic3r/CMakeLists.txt` | baut die Dateien aus `quellen/` mit |

Statt `.patch`-Dateien stehen dort die Original-Stellen und ihr Ersatz. Ändert OrcaSlicer
eine Stelle, bricht das Skript mit „Stelle nicht gefunden“ ab – dann die Liste anpassen.

## Einmal einrichten

1. **Portal** (*3 Eulen → EulenSlicer*, Modul „slicer“ ab 1.0.0):
   - *Einstellungen → Schlüsselpaar erzeugen.* Den privaten Schlüssel zeigt das Portal nur
     einmal – kopieren und zusätzlich sicher aufbewahren (Passwort-Manager).
   - *GitHub → Build-Konto anlegen und Zugang erzeugen.* Den Zugang kopieren.
2. **GitHub**, dieses Repo → *Settings → Secrets and variables → Actions*:

   | Art | Name | Wert |
   |---|---|---|
   | Variable | `PORTAL_API` | steht im Portal unter *GitHub*, z. B. `https://example.org/wp-json/eulen/v1/` |
   | Variable | `UPDATE_PUBKEY` | öffentlicher Schlüssel (Base64), steht im Portal |
   | Secret | `PORTAL_ZUGANG` | `eulenslicer-build:xxxx xxxx …` aus Schritt 1 |
   | Secret | `SIGNATUR_SCHLUESSEL` | der private Schlüssel mit den Zeilen `BEGIN`/`END PRIVATE KEY` |
   | Variable (optional) | `BUILD_OFFSET` | Zahl, die zur Lauf-Nummer addiert wird (falls die Build-Nummern einmal neu anfangen müssen) |

3. *Settings → Actions → General → Workflow permissions:* „Allow GitHub Actions to create
   and approve pull requests“ anhaken (für „OrcaSlicer-Update“).
4. *Actions → EulenSlicer bauen → Run workflow.* Der erste Lauf baut die Abhängigkeiten von
   OrcaSlicer (gut 1,5 Stunden) und legt sie in den Zwischenspeicher, danach etwa eine Stunde.
   Die MSI liegt danach im Portal (*Versionen*) und 14 Tage unter *Artifacts* des Laufs.

Den Schlüssel nur einmal am Anfang erzeugen: Jeder EulenSlicer kennt den öffentlichen
Schlüssel, mit dem er gebaut wurde. Ein neuer Schlüssel heißt, dass alle Rechner die
nächste Version einmal von Hand installieren müssen.

## Bauen

| Auslöser | Ergebnis im Portal |
|---|---|
| *Run workflow* mit „freigeben“ (Standard) | freigegeben – die Slicer holen sie beim nächsten Start |
| *Run workflow* ohne „freigeben“ | zurückgezogen – erst testen, dann im Portal freigeben |
| Tag `v…` pushen (z. B. `v2.4.2-3`) | freigegeben |
| Push auf `main`, der `orca.version` ändert (neue OrcaSlicer-Version) | zurückgezogen |
| *Run workflow* mit „Nur bauen“ | nichts – die MSI liegt nur unter *Artifacts* (Test, geht auch ohne Secrets) |

„Was ist neu?“ kommt aus dem Feld beim Starten von Hand, sonst aus der Commit-Nachricht.

Jeder Lauf hebt den gebauten Programmordner 3 Tage als `EulenSlicer-Programm-b<Build>` auf.
Bei Änderungen nur am Installer packt *Actions → MSI neu packen* (Nummer des Laufs eintragen)
die MSI daraus neu, ohne den Slicer noch einmal zu übersetzen.

**Versionen:** `version` = Version von OrcaSlicer (z. B. 2.4.2), `build` = Lauf-Nummer
(+ `BUILD_OFFSET`). Die MSI hat die Version `2.4.<build>` – der Build zählt immer weiter,
so ersetzt jede neue MSI die alte, auch über einen Versionssprung von OrcaSlicer hinweg.

## Auf den Rechnern

- **Installieren:** MSI aus dem Portal (*Interner Bereich → EulenSlicer*) doppelklicken.
  Installiert für alle Konten nach `C:\Program Files\EulenSlicer` (einmal Administratorrechte).
  Ohne Nachfrage, z. B. per GPO oder Intune: `msiexec /i EulenSlicer-2.4.2-b17.msi /qn`;
  ohne Desktop-Verknüpfung: `DESKTOP=0`.
- **SmartScreen:** Die MSI hat keine gekaufte Code-Signatur. Beim ersten Mal „Weitere
  Informationen“ → „Trotzdem ausführen“.
- **Profile aus OrcaSlicer übernehmen:** Vor dem ersten Start den Ordner
  `%APPDATA%\OrcaSlicer` nach `%APPDATA%\EulenSlicer` kopieren. Sonst startet der
  EulenSlicer mit dem Einrichtungsassistenten.
- **Mit dem Portal verbinden:** im Portal unter *Interner Bereich → EulenSlicer* einen Zugang
  für den Rechner anlegen und der Anleitung dort folgen (Drucker-Profil kopieren,
  „Benutze Drittanbieter-Druck-Hosts“ und „Use 3MF instead of G-code“ anhaken, Host-Typ
  „Octo/Klipper“ mit Adresse und API-Schlüssel).
- **Updates:** Beim Start fragt der EulenSlicer `…/eulen/v1/slicer/update?build=<eigener>`.
  Ist im Portal ein neuerer Build freigegeben, bietet er ihn an. „Ja“ lädt die MSI über einen
  Link, der 30 Minuten gilt, prüft Größe, SHA-256 und die Ed25519-Signatur mit dem
  eingebauten Schlüssel, schließt sich (ein ungespeichertes Projekt fragt er vorher ab) und
  startet `msiexec /passive` – Windows fragt nach Administratorrechten. Danach startet er
  neu. „Nein“ merkt sich den Build; *Hilfe → Nach Updates suchen* fragt trotzdem.
- **Fehlersuche:** Protokoll der Installation bei Updates in `%TEMP%\EulenSlicer-Update\installation.log`,
  von Hand: `msiexec /i EulenSlicer-….msi /l*v "%TEMP%\eulenslicer-install.log"`.
  Protokoll des Slicers in `%APPDATA%\EulenSlicer\log`.

## Neue Version von OrcaSlicer

„OrcaSlicer-Update“ sieht montags nach. Gibt es eine neue Version, öffnet es einen Pull
Request, der `orca.version` hochsetzt, und schreibt hinein, ob alle Stellen von
`skripte/anwenden.py` noch passen. Passt etwas nicht: im Zweig die Liste `ERSETZUNGEN`
anpassen (die neue Stelle im Quelltext von OrcaSlicer suchen), bis „Prüfen“ grün ist.
Nach dem Zusammenführen baut „EulenSlicer bauen“ die neue Version (zurückgezogen) – auf
einem Rechner testen, dann im Portal freigeben.

Von Hand: `orca.version` ändern, committen, pushen.

## Lokal bauen (Windows)

Voraussetzungen wie bei OrcaSlicer (Visual Studio 2022 mit C++, CMake, Strawberry Perl, Git).

```powershell
git clone --depth 1 --branch v2.4.2 https://github.com/OrcaSlicer/OrcaSlicer.git orca
$env:PORTAL_API = "https://example.org/wp-json/eulen/v1/"
$env:UPDATE_PUBKEY = "…"          # aus dem Portal
python skripte\anwenden.py --orca orca --build 9999
cd orca
.\build_release_vs.bat deps
.\build_release_vs.bat slicer
cd ..
dotnet tool install --global wix --version 5.0.2
wix extension add -g WixToolset.UI.wixext/5.0.2
# Pfade absolut – WiX sucht relative Pfade vom Ordner installer\ aus
wix build installer\EulenSlicer.wxs -arch x64 -ext WixToolset.UI.wixext -culture de-DE `
  -d Version=2.4.9999 -d Icon=$PWD\orca\resources\images\OrcaSlicer.ico -d Lizenz=$PWD\installer\Lizenz.rtf `
  -bindpath Quelle=$PWD\orca\build\OrcaSlicer -o EulenSlicer-test.msi
```

Den Kern ohne Windows testen (Linux, OpenSSL):

```sh
g++ -std=c++17 -I quellen -I orca/deps_src tests/kern_test.cpp quellen/EulenUpdaterKern.cpp -lcrypto -o kern_test && ./kern_test
```

## Lizenz

OrcaSlicer steht unter der GNU Affero General Public License 3.0 (`LICENSE.txt`), die
Anpassungen hier ebenso. Der EulenSlicer ist für den Gebrauch bei 3 Eulen Design gedacht.
Wer ihn an andere weitergibt, muss ihnen auch den Quelltext (OrcaSlicer in der Version aus
`orca.version` plus dieses Repo) anbieten.
