# EulenSlicer

Der EulenSlicer ist [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer) mit ein paar
Anpassungen für 3 Eulen Design – gebaut für Windows als MSI:

- **Eulen statt Orca:** Startbild mit den drei Eulen aus dem Website-Theme und unten
  „Powered by 3 Eulen Design®“ mit dem Logo, App-Symbol mit
  der Schiefer-Eule, Menü „3 Eulen“ in der Titelleiste, Fenster „Über EulenSlicer“ (das
  Copyright von OrcaSlicer und die Lizenzhinweise bleiben, wie es die AGPL verlangt).
- **Farben von 3 Eulen:** Blau statt Türkis (Schiefer-Blau `#50708F` wie die Buttons der
  Website) – Knöpfe, Schalter, Reiter, Symbole, 3D-Ansicht, Startseite und Assistent.
  Schwarz, Weiß und Grau bleiben wie bei OrcaSlicer.
- **Anmelden im Browser:** „Anmelden“ zeigt einen Code, der Browser öffnet das Portal, dort
  den Code eintippen und bestätigen – fertig (seit Modul `slicer` 1.6.0 steht der Code nicht
  mehr im Link, damit ein zugeschickter Link allein keinen fremden Slicer anmeldet). Kein
  Schlüssel, kein OrcaCloud-Konto. Ohne Konto geht es
  mit dem **Werkstatt-Code** (Name Pflicht, E-Mail freiwillig).
- **An 3 Eulen senden:** neben „Drucken“, im Menü „3 Eulen“ und auf der Startseite. Der
  Slicer speichert das Projekt (geslicet mit G-Code der Platte, sonst nur das Modell), legt
  es im Portal ab und zeigt ein Fenster für Titel, Material, Farbe aus dem Lager, Anzahl,
  Termin und Notiz. Gedruckt wird erst nach der Freigabe im Auftrag; dann startet der
  Drucker-Agent in der Werkstatt den Druck. Direkt im LAN drucken geht weiterhin.
- **Drucker der Werkstatt im Reiter „Gerät“:** Mit Portal zeigt „Gerät“ nur noch alle
  Drucker aus dem Druckerstatus des Portals – was läuft, Fortschritt, Restzeit, Fehler,
  Temperaturen und das AMS. Die Ansicht für einen direkt verbundenen Drucker (Status,
  Speicher, Firmware, HMS) und damit das Netzwerk-Plugin von Bambu Lab braucht es dafür
  nicht mehr; das Plugin braucht nur noch, wer direkt im LAN druckt („Drucken“).
- **Kein Reiter „Kalibrierung“:** Er kalibriert über eine direkte Verbindung zum Drucker
  („Kein Drucker“, Netzwerk-Plugin). Die Kalibriertests im Menü „Kalibrierung“ (Temperatur,
  Fluss, Pressure Advance, Retraction …) legen ein Testobjekt auf die Platte, das wie jedes
  Projekt mit „An 3 Eulen senden“ gedruckt wird.
- **Startseite und „Meine Aufträge“ aus dem Portal:** zuletzt geöffnete Projekte, eigene
  offene Aufträge mit Rückfragen, Drucker in der Werkstatt, Hinweise aus der Verwaltung.
  Ohne Verbindung zum Portal bleibt die normale Startseite.
- **Profile aus dem Portal:** gemeinsame Drucker-, Filament- und Prozess-Profile kommen
  beim Start (und auf Wunsch) aus dem Portal.
- **Druckdateien ins Portal wie bisher:** „Hochladen“ über den Druck-Host (OctoPrint-Schnittstelle,
  Modul „EulenSlicer“ in [3ED-App](https://github.com/3EulenDesign/3ED-App)) geht weiter –
  auch mit einem normalen OrcaSlicer.
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
  EulenPortal.cpp/.hpp       Portal: Startseite, Fenster, Brücke zu den Seiten (eulen_*), Konto in der
                             Windows-Anmeldeinformationsverwaltung, Senden (3MF speichern, in Stücken
                             ablegen), Profile abgleichen, Menü „3 Eulen“
  EulenPortalKern.cpp/.hpp   Kern ohne Oberfläche: welche Adressen dürfen mit dem Slicer reden,
                             Fehlertexte, Plan für den Abgleich der Profile
bilder/                      Startbild (splash_logo*.svg), Symbole mit der Eule und Kopf von „Über“
                             (OrcaSlicer_about*.svg), ersetzen die von OrcaSlicer
  marke/                     Logo von 3 Eulen Design (JPG) und als Pfade (abzeichen.svg, schriftzug.svg)
                             fürs Startbild – NanoSVG kennt keine Pixelbilder
  vorschau/                  so sehen die Startbilder aus (nicht im Programm)
skripte/
  anwenden.py                kopiert quellen/ und bilder/, schreibt EulenKonfig.hpp, ändert die Stellen in OrcaSlicer
  logo_vektor.py             wandelt das Logo (bilder/marke/3-eulen-design.jpg) in SVG-Pfade um (OpenCV; nur bei Änderungen)
  bilder.py                  erzeugt bilder/ (Eulen aus dem Theme, Schriftzug Poppins Bold als Pfad; nur bei Änderungen)
  hochladen.py               signiert die MSI und lädt sie in Stücken ins Portal
  msi.ps1                    baut die MSI aus dem Programmordner (WiX 5, absolute Pfade, Größenprüfung)
  abhaengigkeiten.ps1        hebt die Abhängigkeiten von OrcaSlicer auf dem eigenen Runner auf und holt sie wieder
  runner-pruefen.ps1         prüft, ob der eigene Runner alles hat (erster Schritt jedes Windows-Laufs dort)
  runner-einrichten.ps1      richtet die VM des Runners ein (winget, einmal als Administrator)
installer/EulenSlicer.wxs    MSI (WiX Toolset 5, Oberfläche Deutsch): für alle Konten, Startmenü, Desktop, eulenslicer://
installer/Lizenz.rtf         Lizenzhinweis im ersten Fenster der Installation
tests/kern_test.cpp          Test für den Kern des Update-Kanals (läuft in „Prüfen“)
tests/portal_test.cpp        Test für den Kern der Portal-Verbindung (läuft in „Prüfen“)
.github/workflows/
  bauen.yml                  „EulenSlicer bauen“: Windows-Build, MSI, Upload ins Portal (eigener Runner)
  msi.yml                    „MSI neu packen“: nur die MSI aus dem Programmordner eines Laufs (Minuten statt Stunden)
  kompilieren.yml            „Kompilieren“: übersetzt nur die geänderten Dateien (etwa 15 Minuten), bei jedem Push auf quellen/
  pruefen.yml                „Prüfen“: Kern-Test, Skripte, passen die Stellen zu OrcaSlicer?
  upstream.yml               „OrcaSlicer-Update“: wöchentlich; bei neuer OrcaSlicer-Version Pull Request,
                             Prüfen, automatisch zusammenführen und bauen (zurückgezogen)
```

### Was an OrcaSlicer geändert wird (`skripte/anwenden.py`)

| Datei | Änderung |
|---|---|
| `src/slic3r/GUI/GUI_App.cpp` | `SetAppName("EulenSlicer")` → eigener Datenordner; `associate_url(L"eulenslicer")` statt `orcaslicer`; Versionsprüfung ruft `eulen_update_pruefen()` statt GitHub |
| `src/libslic3r/Utils.hpp`, `src/slic3r/GUI/Downloader.cpp`, `src/slic3r/GUI/InstanceCheck.cpp` | `eulenslicer://open?file=…` wird wie `orcaslicer://` geöffnet |
| `src/slic3r/Utils/OctoPrint.cpp` | Zum Portal (`…/eulen/v1/slicer`) nur mit geprüftem HTTPS-Zertifikat – OrcaSlicer prüft sonst keine Zertifikate, und der API-Schlüssel ist ein Anwendungspasswort |
| `src/slic3r/GUI/Plater.cpp` | Fenstertitel „… – EulenSlicer“ |
| `src/slic3r/GUI/GUI_App.cpp` | nach dem Start `eulen_beim_start()` (Profile aus dem Portal) |
| `src/slic3r/GUI/WebViewDialog.cpp` | Reiter „Start“: Seite aus dem Portal (`eulen_start_einrichten`), von dort nur Seiten des Portals (Links im Browser), Rückfall auf die normale Startseite bei Fehlern, Befehle `eulen_*` der Seite |
| `src/slic3r/GUI/Monitor.cpp` | Reiter „Gerät“: mit Portal die Seite „geraete“ (`eulen_geraete_seite()`) über die ganze Fläche; die Reiter von OrcaSlicer (Status, Speicher, Firmware, HMS, Druckerauswahl) bleiben unsichtbar im Panel, damit alle Stellen, die sie ansprechen, weiterlaufen |
| `src/slic3r/GUI/MainFrame.cpp`, `MainFrame.hpp` | Menü „3 Eulen“ statt „Sync Presets“ (OrcaCloud) in der Titelleiste, „An 3 Eulen senden“ im Dateimenü und als Auswahl `eEulenSenden` des Knopfs „Drucken“ – mit Portal die Standardauswahl (`set_print_button_to_default`), auch ohne Slicen aktiv |
| `src/slic3r/GUI/MainFrame.cpp` (Kalibrierung) | mit Portal kein Reiter „Kalibrierung“ (beim Start und in `show_device()`); das Panel bleibt unsichtbar angelegt, weil OrcaSlicer `m_calibration` anspricht |
| `src/libslic3r/AppConfig.cpp` | Login-Spalte von OrcaCloud auf der normalen Startseite standardmäßig aus |
| `resources/images/` | Startbild und Symbole aus `bilder/` (auch Symbol der `.exe` und der MSI) |
| `src/libslic3r/libslic3r.h` | `SLIC3R_APP_FULL_NAME` „EulenSlicer“ statt „Orca Slicer“ – Name in Fenstertiteln und Meldungen (nur Anzeige; Datenordner und Konfiguration hängen an `SLIC3R_APP_KEY` bzw. `SetAppName`) |
| `src/slic3r/GUI/GUI_App.cpp` (Startbild) | Version bei 70 % statt 72 %, Fortschritt bei 79 % statt 88 % der Höhe – unten steht „Powered by 3 Eulen Design®“ (im Startbild aus `bilder/`) |
| `src/slic3r/GUI/AboutDialog.cpp` | „Über EulenSlicer“: Kopf `OrcaSlicer_about*.svg` aus `bilder/`, „Build 12 · OrcaSlicer <Commit>“, eigene Absätze (`eulen_ueber_texte`), Zeile „Anpassungen von 3 Eulen Design“ über dem Copyright von OrcaSlicer, Links zum Portal und zu orcaslicer.com |
| `resources/web/image/logo.png` | Eule (`bilder/OrcaSlicer_154.png`) statt Orca auf der Startseite und im Assistenten |
| `src/slic3r/`, `src/libslic3r/Color.hpp`, `deps_src/imgui/`, `resources/images/*.svg`, `resources/web/` | **Farben** (`FARBEN`): Türkis → Blau nach Farbwert in allen Schreibweisen (`#009688`, `0x009688`, `wxColour(0, 150, 136)`, `150.f / 255.f`, `0.59f` …), dazu Hover-, Druck- und Auswahltöne; PNG-Bilder der Seiten mit Pillow. Bleibt nach einem Orca-Update Türkis übrig (neuer Ton, neue Schreibweise), meldet „Prüfen“ die Datei – dann den Ton in `FARBEN` ergänzen. Filamentfarben, Profile und Farbskalen der Vorschau bleiben unberührt |
| `src/slic3r/GUI/NotificationManager.cpp` | Links in Hinweisen im Dunkelmodus hellblau (`#9DBCE0`), sonst schlecht lesbar |
| `src/slic3r/Utils/Http.cpp`, `Http.hpp` | `Http::follow_redirects(bool)` – Anfragen ans Portal folgen keinen Weiterleitungen (sonst ginge der Zugang im Kopf `X-Api-Key` an das neue Ziel) |
| `src/slic3r/CMakeLists.txt` | baut die Dateien aus `quellen/` mit |

Statt `.patch`-Dateien stehen dort die Original-Stellen und ihr Ersatz. Ändert OrcaSlicer
eine Stelle, bricht das Skript mit „Stelle nicht gefunden“ ab – dann die Liste anpassen.

### Brücke zu den Seiten des Portals

Die Fenster „Start“, „An 3 Eulen senden“, „Meine Aufträge“ und „Anmelden“ und der Reiter
„Gerät“ zeigen Seiten aus dem Portal (`…/eulen/v1/slicer/seite/<name>`). Neue Funktionen dort kommen ohne neuen Build.
Die Seiten schicken `window.wx.postMessage({sequence_id, command, data})`, der Slicer
antwortet mit `window.postMessage({sequence_id, command, data})` (bei Fehlern `ok: false`,
`message`). Antworten bekommen nur Seiten unter `<PORTAL_API>slicer/seite/` (Prüfung in
`EulenPortalKern.cpp`, Test in `tests/portal_test.cpp`); andere Adressen öffnet der Slicer im
Browser. Die Liste der Befehle (`eulen_info`, `eulen_konto_setzen`, `eulen_senden`,
`eulen_oeffnen`, `eulen_profile` …) steht in `eulen-portal/ENTWICKLER.md` im Repo 3ED-App.

- **Konto:** Zugang (`benutzer:anwendungspasswort`, gilt im Portal nur unter `/slicer/api/`)
  bzw. Werkstatt-Code liegen in der Windows-Anmeldeinformationsverwaltung
  („EulenSlicer/Portal“, „EulenSlicer/Werkstatt“), nicht in der Konfiguration.
- **Senden:** `Plater::export_3mf()` nach `%TEMP%\EulenSlicer-Senden\` (geslicete Platte mit
  G-Code, sonst das ganze Projekt), dann `POST …/slicer/api/ablage`, die Stücke, `…/fertig`,
  danach das Fenster „senden“.
- **Profile:** `GET …/slicer/api/profile`, geänderte nach
  `%APPDATA%\EulenSlicer\user\<Konto>\<machine|filament|process>\`. Welche aus dem Portal
  kommen, steht mit Prüfsumme in `eulen_profile.json` daneben – nur die ändert oder löscht
  der Slicer. Gleichnamige eigene Profile bleiben unangetastet; ein Profil aus dem Portal,
  das jemand im Slicer ändert und speichert, gehört ab dann der Person (es wird nicht mehr
  überschrieben). Profile mit Nachbearbeitungs-Skripten (`post_process`) lehnt der Slicer ab.
  Neue Profile erscheinen sofort, geänderte und gelöschte nach dem nächsten Start (OrcaSlicer
  lädt geladene Profile nicht neu); sind gerade Profile ungespeichert geändert, auch die neuen.
- **Drucker der Werkstatt:** `GET …/slicer/api/drucker` (Modul ab 1.3.0) liefert die
  Modelle von Bambu Lab mit Düse (aus Druckerstatus und Druckaufträgen). Fehlen sie in
  `vendors` der AppConfig, richtet der Slicer sie mit `PresetBundle::apply_vendor_config()`
  ein wie der Assistent (Hersteller „BBL“, passende Filamente kommen mit) und wählt beim
  ersten Mal den ersten. Läuft mit dem Abgleich der Profile (beim Start, nach dem Anmelden,
  Menü „3 Eulen“ → „Profile aus dem Portal holen“), entfernt nichts.
- **Filamente aus dem Lager:** kommen als normale Profile aus dem Portal (Modul ab 1.4.0,
  je Sorte und Drucker, erben von Profilen von Bambu Lab). Welches Profil zu welchem
  Drucker passt, liest das Portal aus `daten/orca-filamente.json`; die Datei erzeugt
  `skripte/filamente.py --orca <Checkout> --version v2.4.2` aus den Profilen von OrcaSlicer.
  Nach einem Update von OrcaSlicer neu erzeugen und ins Repo 3ED-App
  (`module/slicer/daten/`) kopieren.
- **Werkstatt im Reiter „Gerät“** (Modul ab 1.5.0): `EulenReiter` (Panel mit WebView, im
  MonitorPanel statt dessen Reitern) lädt
  …/slicer/seite/geraete erst, wenn der Reiter zu sehen ist (`IsShownOnScreen()`, jede
  Sekunde nachgesehen; vorher fragt er die Seite einmal ab – fehlt sie, weil das Modul älter
  als 1.5.0 ist, steht ein Hinweis da), und meldet danach Wechsel mit `eulen_sichtbar` (`sichtbar`: true/false) –
  so fragt die Seite im Hintergrund nicht im Portal nach. Nach einem Ladefehler versucht er es
  alle 30 Sekunden, solange der Reiter zu sehen ist. `eulen_info` sagt dort `fenster: "reiter"`;
  Anmeldung und Senden schicken auch ihr `eulen_neu_laden`.
- **Normale Befehle der Startseite:** Von der Portal-Seite nimmt der Slicer nur die Befehle
  für zuletzt geöffnete Projekte, „Neues Projekt“ und „Projekt öffnen“ an; alles andere von
  OrcaSlicer (OrcaCloud, Anmelde-Zeitgeber) bleibt draußen.

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
   | Variable (optional) | `ORCA_AUTOMATISCH` | `nein`: „OrcaSlicer-Update“ öffnet nur den Pull Request und führt ihn nicht selbst zusammen |
   | Variable (optional) | `EULEN_RUNNER` | `github`: die Windows-Workflows laufen auf `windows-latest` bei GitHub statt auf dem eigenen Runner |

3. *Settings → Actions → General → Workflow permissions:* „Allow GitHub Actions to create
   and approve pull requests“ anhaken (für „OrcaSlicer-Update“). Ohne diesen Haken kann es
   keinen Pull Request anlegen und damit auch nichts automatisch übernehmen.
4. **Eigener Runner** einrichten, siehe nächster Abschnitt (oder Variable `EULEN_RUNNER` =
   `github`, dann baut GitHub selbst).
5. *Actions → EulenSlicer bauen → Run workflow.* Der erste Lauf baut die Abhängigkeiten von
   OrcaSlicer und hebt sie auf. Auf dem eigenen Runner dauert das knapp eine Stunde, danach
   gut eine halbe (bei GitHub gut 2,5 Stunden, danach etwa 1¼). Die MSI liegt danach im Portal (*Versionen*) und 14 Tage unter
   *Artifacts* des Laufs.

Den Schlüssel nur einmal am Anfang erzeugen: Jeder EulenSlicer kennt den öffentlichen
Schlüssel, mit dem er gebaut wurde. Ein neuer Schlüssel heißt, dass alle Rechner die
nächste Version einmal von Hand installieren müssen.

## Eigener Runner

„EulenSlicer bauen“, „Kompilieren“ und „MSI neu packen“ laufen auf einem eigenen Runner mit
Windows (VM auf Proxmox, Labels `self-hosted`, `Windows`, `X64`). „Prüfen“ und
„OrcaSlicer-Update“ laufen weiter bei GitHub unter Linux.

**Einrichten**

1. **VM:** Windows 11 oder Windows Server 2025, 64 Bit, mindestens 100 GB freier Platz
   (Abhängigkeiten, Build und das Lager der Abhängigkeiten). Mehr Kerne bauen schneller.
2. **Runner registrieren:** dieses Repo → *Settings → Actions → Runners → New self-hosted
   runner → Windows x64*. Die Befehle von dort in `C:\a` ausführen (kurze Pfade – lange
   scheitern beim Bauen der Abhängigkeiten) und bei `config.cmd` als Dienst einrichten.
   Unser Runner hängt an der **Organisation**, in einer Runner-Gruppe mit diesem Repo
   (*Organisation → Settings → Actions → Runner groups*). Weil das Repo öffentlich ist, muss
   bei der Gruppe zusätzlich **Allow public repositories** an sein – sonst bleiben die Läufe
   als *Queued* stehen. Alternativ den Runner direkt am Repo registrieren.
3. **Programme:** Repo auf die VM holen und in einer PowerShell als Administrator
   `skripte\runner-einrichten.ps1` ausführen. Es installiert über winget Visual Studio 2022
   Build Tools mit C++, Git, Python 3.12, PowerShell 7, .NET SDK 8 und Strawberry Perl,
   schaltet lange Pfade ein, startet den Runner-Dienst neu und prüft am Ende alles.
   Mit `-DefenderAusnahme C:\a` baut die VM deutlich schneller.
4. **Erster Lauf:** *Actions → EulenSlicer bauen → Run workflow* (ohne „freigeben“ oder mit
   „Nur bauen“). Er baut die Abhängigkeiten einmal; „Kompilieren“ geht erst danach. Auf
   `win-build-01` (22.10.2026): Abhängigkeiten 24 Minuten, Slicer 27 Minuten, alles 54 Minuten.

Jeder Windows-Lauf prüft zuerst mit `runner-pruefen.ps1`, ob alles da ist. Fehlt etwas,
steht es oben im Lauf (z. B. „Runner: Strawberry Perl … fehlt“), und der Lauf hört gleich
auf statt nach einer Stunde.

**Sicherheit – das Repo ist öffentlich.** Ein Pull Request aus einem Fork kann die
Workflows ändern; liefe er auf der VM, käme er dort an alles, was spätere Läufe sehen,
auch an `SIGNATUR_SCHLUESSEL`, mit dem jedes Update signiert wird. Deshalb:

- *Settings → Actions → General → Approval for running fork pull request workflows from
  contributors:* **Require approval for all external contributors**. Dann läuft kein fremder
  Pull Request, ohne dass ihn jemand freigibt – vorher die Änderungen unter `.github/` ansehen.
- Die VM in ein eigenes Netz (VLAN) ohne Zugriff auf andere Rechner, nur ins Internet.
- Auf der VM nichts anderes und keine anderen Zugangsdaten; der Runner nur für dieses Repo.

Die eigenen Workflows starten auf der VM nie durch einen Pull Request: „Kompilieren“ bei
Pushes ins Repo, die anderen von Hand, bei Tags oder über „OrcaSlicer-Update“.

**Abhängigkeiten auf dem Runner:** Statt im Zwischenspeicher von GitHub liegen sie auf der
VM unter `C:\a\_work\_tool\eulen\d-<Hash>` (Werkzeug-Ordner des Runners; ein anderer Ort
über die Umgebungsvariable `EULEN_LAGER` auf der VM). Der Hash kommt aus `deps/` von
OrcaSlicer – nach einem OrcaSlicer-Update baut der nächste Lauf sie einmal neu. Behalten
werden die zwei zuletzt benutzten Stände. „Kompilieren“ nimmt sie direkt von dort.

**Wenn die VM aus ist:** Die Läufe warten als *Queued* (bis zu 24 Stunden). Wartet ein Lauf,
obwohl die VM läuft, passt die Registrierung nicht (Runner offline, Repo nicht in der
Gruppe, Gruppe ohne „Allow public repositories“). Zurück zu GitHub: Variable `EULEN_RUNNER` = `github` – das gilt
für alle drei Windows-Workflows und kostet bei einem öffentlichen Repo nichts.

## Bauen

| Auslöser | Ergebnis im Portal |
|---|---|
| *Run workflow* mit „freigeben“ (Standard) | freigegeben – die Slicer holen sie beim nächsten Start |
| *Run workflow* ohne „freigeben“ | zurückgezogen – erst testen, dann im Portal freigeben |
| Tag `v…` pushen (z. B. `v2.4.2-3`) | freigegeben |
| Push auf `main`, der `orca.version` ändert (neue OrcaSlicer-Version) | zurückgezogen |
| „OrcaSlicer-Update“ nach dem automatischen Zusammenführen | zurückgezogen, Ergebnis als Kommentar im Pull Request |
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
- **Mit dem Portal verbinden:** Startseite → „Anmelden“ (oder Menü „3 Eulen“ → „Anmelden /
  Konto“) → „Im Browser anmelden“ und im Portal den Code eintippen, den der Slicer zeigt, und bestätigen. In der Werkstatt ohne
  Konto: „Ohne Konto (Werkstatt)“ mit dem Code aus dem Aushang. Abmelden widerruft den Zugang
  im Portal. Der alte Weg über den Druck-Host (Zugang im *Interner Bereich → EulenSlicer*,
  Host-Typ „Octo/Klipper“) geht weiterhin.
- **Senden:** „An 3 Eulen senden“ ist die Standardauswahl des Knopfs „Drucken“ (andere wie
  „Platte drucken“ über den Pfeil daneben), dazu im Menü „3 Eulen“ und auf der Startseite. Ist die Platte geslicet, geht die Druckdatei mit; sonst das Modell.
- **Drucker:** Die Drucker von Bambu Lab aus dem Portal richtet der EulenSlicer nach dem
  Anmelden selbst ein (Hinweis unten rechts); „Drucker hinzufügen“ braucht es dafür nicht.
- **Updates:** Beim Start fragt der EulenSlicer `…/eulen/v1/slicer/update?build=<eigener>`.
  Ist im Portal ein neuerer Build freigegeben, bietet er ihn an. „Ja“ lädt die MSI über einen
  Link, der 30 Minuten gilt, prüft Größe, SHA-256 und die Ed25519-Signatur mit dem
  eingebauten Schlüssel, schließt sich (ein ungespeichertes Projekt fragt er vorher ab) und
  startet `msiexec /passive` – Windows fragt nach Administratorrechten. Danach startet er
  neu. „Nein“ merkt sich den Build; *3 Eulen → Nach Updates suchen* (oder *Hilfe → Nach Updates
  suchen*) fragt trotzdem.
- **Fehlersuche:** Protokoll der Installation bei Updates in `%TEMP%\EulenSlicer-Update\installation.log`,
  von Hand: `msiexec /i EulenSlicer-….msi /l*v "%TEMP%\eulenslicer-install.log"`.
  Protokoll des Slicers in `%APPDATA%\EulenSlicer\log`.

## Neue Version von OrcaSlicer

„OrcaSlicer-Update“ sieht montags um 5:17 UTC nach (nur echte Releases, keine Betas).
Gibt es eine neue Version:

1. Zweig `orca-<Version>` mit neuer `orca.version` und ein Pull Request dazu. Darin steht,
   ob alle Stellen von `skripte/anwenden.py` noch passen und ob sich die Tabelle der
   Filament-Profile geändert hat.
2. „Prüfen“ läuft auf diesem Zweig (Stellen, Kern-Tests, Skripte, Installer).
3. **Alles grün:** Der Pull Request wird automatisch zusammengeführt, und „EulenSlicer bauen“
   startet. Die neue Version kommt **zurückgezogen** ins Portal; wie der Build ausgeht, steht
   danach als Kommentar im Pull Request. Auf einem Rechner testen, dann unter
   *3 Eulen → EulenSlicer → Versionen* freigeben – erst dann holen sich die Slicer das Update.
4. **Etwas rot:** Der Pull Request bleibt offen, mit dem Grund. Im Zweig die Liste
   `ERSETZUNGEN` anpassen (die neue Stelle im Quelltext von OrcaSlicer suchen), bis „Prüfen“
   grün ist, dann von Hand zusammenführen – das startet „EulenSlicer bauen“ wie oben.

Mit der Variable `ORCA_AUTOMATISCH` = `nein` bleibt es beim Pull Request (Schritt 3 von Hand).
Von Hand ganz ohne Pull Request: `orca.version` ändern, committen, pushen.

**Filament-Profile für das Portal:** „OrcaSlicer-Update“ erzeugt die Tabelle
(`skripte/filamente.py`) für alte und neue Version und vergleicht sie. Hat sie sich
geändert, liegt die neue `orca-filamente.json` 90 Tage unter *Artifacts* des Laufs, und der
Pull Request listet die Unterschiede. Dann vor dem Freigeben im Portal die Datei als
`module/slicer/daten/orca-filamente.json` ins Repo 3ED-App legen und das Modul „slicer“
neu hochladen – sonst erben die Filamente aus dem Lager womöglich von Profilen, die es
nicht mehr gibt. Von Hand: `python skripte/filamente.py --orca <Checkout> --version <Tag>`,
vergleichen mit `python skripte/filamente.py --vergleichen alt.json neu.json`.

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
