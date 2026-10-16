#!/usr/bin/env python3
"""Die Anpassungen des EulenSlicers auf einen Checkout von OrcaSlicer anwenden.

    python skripte/anwenden.py --orca ../OrcaSlicer --build 17
    python skripte/anwenden.py --orca ../OrcaSlicer --pruefen     # nur prüfen, nichts ändern

Statt .patch-Dateien steht hier je Änderung die Stelle im Original und ihr
Ersatz. Ändert OrcaSlicer eine dieser Stellen, bricht das Skript mit einer
klaren Meldung ab („Stelle nicht gefunden …“) – dann die Liste unten anpassen.
Ein zweiter Lauf auf denselben Checkout schadet nicht (schon geänderte Stellen
werden erkannt).

Was es tut:
  1. quellen/*.cpp, *.hpp nach src/slic3r/GUI/ kopieren, bilder/* (Startbild, Symbole
     mit der Eule, erzeugt von skripte/bilder.py) nach resources/images/
  2. src/slic3r/GUI/EulenKonfig.hpp schreiben (Build, Adresse des Portals, Schlüssel)
  3. die Stellen in ERSETZUNGEN ändern
  4. Versionen ausgeben (für GitHub Actions auch in $GITHUB_OUTPUT)

Umgebungsvariablen (statt der Schalter):
  PORTAL_API     REST-Adresse des Portals bis „eulen/v1/“
  UPDATE_PUBKEY  öffentlicher Ed25519-Schlüssel (Base64, 32 Byte)
  EULEN_BUILD    fortlaufende Build-Nummer
"""

from __future__ import annotations

import argparse
import base64
import os
import re
import shutil
import sys
from pathlib import Path

HIER = Path(__file__).resolve().parent.parent

# (Datei, Original, Ersatz, Anzahl) – Original muss genau so oft vorkommen.
ERSETZUNGEN: list[tuple[str, str, str, int]] = [
    # Eigener Datenordner, damit EulenSlicer und OrcaSlicer nebeneinander laufen.
    (
        "src/slic3r/GUI/GUI_App.cpp",
        "    SetAppName(SLIC3R_APP_KEY);\n",
        "    // EulenSlicer: eigener Datenordner (%APPDATA%\\EulenSlicer), getrennt von OrcaSlicer.\n"
        "    SetAppName(\"EulenSlicer\");\n",
        1,
    ),
    # Links „eulenslicer://open?file=…“ (Knopf „In EulenSlicer öffnen“ im Portal);
    # orcaslicer:// bleibt dem normalen OrcaSlicer.
    (
        "src/slic3r/GUI/GUI_App.cpp",
        'associate_url(L"orcaslicer");',
        'associate_url(L"eulenslicer");',
        2,
    ),
    (
        "src/libslic3r/Utils.hpp",
        'inline bool is_orca_open(const std::string& url) { return boost::starts_with(url, "orcaslicer://open"); }',
        'inline bool is_orca_open(const std::string& url) { return boost::starts_with(url, "orcaslicer://open") || boost::starts_with(url, "eulenslicer://open"); }',
        1,
    ),
    (
        "src/slic3r/GUI/Downloader.cpp",
        "(orcaslicer|prusaslicer|bambustudio|cura)",
        "(eulenslicer|orcaslicer|prusaslicer|bambustudio|cura)",
        1,
    ),
    (
        "src/slic3r/GUI/InstanceCheck.cpp",
        "(orcaslicer|prusaslicer|cura|bambustudio)",
        "(eulenslicer|orcaslicer|prusaslicer|cura|bambustudio)",
        1,
    ),
    # Updates aus dem Portal statt von GitHub.
    (
        "src/slic3r/GUI/GUI_App.cpp",
        '#include "MainFrame.hpp"\n',
        '#include "MainFrame.hpp"\n#include "EulenUpdater.hpp"\n#include "EulenPortal.hpp"\n',
        1,
    ),
    (
        "src/slic3r/GUI/GUI_App.cpp",
        "void GUI_App::check_new_version_sf(bool show_tips, int by_user)\n{\n",
        "void GUI_App::check_new_version_sf(bool show_tips, int by_user)\n{\n"
        "    // EulenSlicer: Updates kommen aus dem 3 Eulen Portal (signierte MSI), nicht von GitHub.\n"
        "    (void) show_tips;\n"
        "    eulen_update_pruefen(by_user != 0);\n"
        "    if (by_user == 0)\n"
        "        eulen_beim_start(); // Profile aus dem Portal abgleichen\n"
        "    return;\n",
        1,
    ),
    # Zum Portal nur mit geprüftem Zertifikat – der API-Schlüssel ist ein Anwendungspasswort.
    (
        "src/slic3r/Utils/OctoPrint.cpp",
        "void OctoPrint::set_auth(Http &http) const\n{\n    http.header(\"X-Api-Key\", m_apikey);\n",
        "void OctoPrint::set_auth(Http &http) const\n{\n    http.header(\"X-Api-Key\", m_apikey);\n"
        "    // EulenSlicer: Zum 3 Eulen Portal nur mit geprüftem Zertifikat.\n"
        "    if (boost::istarts_with(m_host, \"https://\") && m_host.find(\"/eulen/v1/slicer\") != std::string::npos)\n"
        "        http.tls_verify(true);\n",
        1,
    ),
    # Fenstertitel
    (
        "src/slic3r/GUI/Plater.cpp",
        'wxGetApp().mainframe->SetTitle(m_project_name + " - OrcaSlicer");',
        'wxGetApp().mainframe->SetTitle(m_project_name + " - EulenSlicer");',
        1,
    ),
    # Reiter „Start“: Seite aus dem Portal, Befehle der Seiten (eulen_*), Rückfall auf die normale Startseite.
    (
        "src/slic3r/GUI/WebViewDialog.cpp",
        '#include "slic3r/GUI/MainFrame.hpp"\n',
        '#include "slic3r/GUI/MainFrame.hpp"\n#include "slic3r/GUI/EulenPortal.hpp"\n',
        1,
    ),
    (
        "src/slic3r/GUI/WebViewDialog.cpp",
        "    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));\n",
        "    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));\n"
        "    // EulenSlicer: Startseite aus dem 3 Eulen Portal, sobald sie erreichbar ist.\n"
        "    eulen_start_einrichten(m_browser, url);\n",
        1,
    ),
    (
        "src/slic3r/GUI/WebViewDialog.cpp",
        "    const wxString &url = evt.GetURL();\n",
        "    const wxString &url = evt.GetURL();\n"
        "    // EulenSlicer: Von der Portal-Seite aus nur Seiten des Portals, Links im Browser.\n"
        "    if (eulen_start_navigation(m_browser, evt))\n"
        "        return;\n",
        1,
    ),
    (
        "src/slic3r/GUI/WebViewDialog.cpp",
        "void WebViewPanel::OnError(wxWebViewEvent& evt)\n{\n",
        "void WebViewPanel::OnError(wxWebViewEvent& evt)\n{\n"
        "    // EulenSlicer: Portal nicht erreichbar -> normale Startseite.\n"
        "    if (eulen_start_fehler(m_browser, evt))\n"
        "        return;\n",
        1,
    ),
    (
        "src/slic3r/GUI/WebViewDialog.cpp",
        "void WebViewPanel::OnScriptMessage(wxWebViewEvent& evt)\n{\n"
        "    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << \": \" << evt.GetString().ToUTF8().data();\n",
        "void WebViewPanel::OnScriptMessage(wxWebViewEvent& evt)\n{\n"
        "    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << \": \" << evt.GetString().ToUTF8().data();\n"
        "    // EulenSlicer: Befehle der Portal-Seite (eulen_*).\n"
        "    if (eulen_web_nachricht(m_browser, evt.GetString(), nullptr))\n"
        "        return;\n",
        1,
    ),
    # Menü „3 Eulen“ statt „Sync Presets“ (OrcaCloud), Eintrag im Dateimenü und neben „Drucken“.
    (
        "src/slic3r/GUI/MainFrame.cpp",
        '#include "MainFrame.hpp"\n',
        '#include "MainFrame.hpp"\n#include "EulenPortal.hpp"\n',
        1,
    ),
    (
        "src/slic3r/GUI/MainFrame.cpp",
        "    append_menu_item(\n"
        "        m_topbar->GetTopMenu(), wxID_ANY, _L(\"Sync Presets\"), _L(\"Pull and apply the latest presets from OrcaCloud\"),\n"
        "        [this](wxCommandEvent&) {\n"
        "            if (!wxGetApp().is_user_login()) {\n"
        "                MessageDialog info_dlg(this, _L(\"You must be logged in to sync presets from cloud.\"),\n"
        "                    _L(\"Sync Presets\"), wxOK | wxICON_INFORMATION);\n"
        "                info_dlg.ShowModal();\n"
        "                return;\n"
        "            }\n"
        "            if (m_plater)\n"
        "                m_plater->get_notification_manager()->push_notification(\n"
        "                    into_u8(_L(\"Syncing presets from cloud\\u2026\")));\n"
        "            wxGetApp().restart_sync_user_preset();\n"
        "        }, \"\", nullptr,\n"
        "        [this]() {\n"
        "            return wxGetApp().is_user_login() && !wxGetApp().app_config->get_stealth_mode();\n"
        "        }, this);\n",
        "    // EulenSlicer: Menü „3 Eulen“ statt „Sync Presets“ (OrcaCloud).\n"
        "    m_topbar->AddDropDownSubMenu(eulen_menue(this), wxString::FromUTF8(\"3 Eulen\"));\n",
        1,
    ),
    (
        "src/slic3r/GUI/MainFrame.cpp",
        "        append_menu_item(fileMenu, wxID_ANY, _L(\"Sync Presets\"), _L(\"Pull and apply the latest presets from OrcaCloud\"),\n"
        "            [this](wxCommandEvent&) {\n"
        "                if (!wxGetApp().is_user_login()) {\n"
        "                    MessageDialog info_dlg(this, _L(\"You must be logged in to sync presets from cloud.\"),\n"
        "                        _L(\"Sync Presets\"), wxOK | wxICON_INFORMATION);\n"
        "                    info_dlg.ShowModal();\n"
        "                    return;\n"
        "                }\n"
        "                wxGetApp().restart_sync_user_preset();\n"
        "            }, \"\", nullptr,\n"
        "            [this]() {\n"
        "                return wxGetApp().is_user_login() && !wxGetApp().app_config->get_stealth_mode();\n"
        "            }, this);\n",
        "        // EulenSlicer: „An 3 Eulen senden“ statt „Sync Presets“ (OrcaCloud).\n"
        "        append_menu_item(fileMenu, wxID_ANY, wxString::FromUTF8(\"An 3 Eulen senden\"),\n"
        "            wxString::FromUTF8(\"Projekt als Druckauftrag ins 3 Eulen Portal schicken\"),\n"
        "            [](wxCommandEvent&) { eulen_senden(); }, \"\", nullptr,\n"
        "            []() { return eulen_portal_aktiv(); }, this);\n",
        1,
    ),
    (
        "src/slic3r/GUI/MainFrame.cpp",
        "            p->Popup(m_print_btn);\n",
        "            // EulenSlicer: Projekt ans 3 Eulen Portal schicken.\n"
        "            if (eulen_portal_aktiv()) {\n"
        "                SideButton* eulen_btn = new SideButton(p, wxString::FromUTF8(\"An 3 Eulen senden\"), \"\");\n"
        "                eulen_btn->SetCornerRadius(0);\n"
        "                eulen_btn->Bind(wxEVT_BUTTON, [p](wxCommandEvent&) {\n"
        "                    p->Dismiss();\n"
        "                    wxGetApp().CallAfter([] { eulen_senden(); });\n"
        "                });\n"
        "                p->append_button(eulen_btn);\n"
        "            }\n"
        "            p->Popup(m_print_btn);\n",
        1,
    ),
    # Login-Spalte von OrcaCloud auf der normalen Startseite ausblenden (Anmelden geht übers Portal).
    (
        "src/libslic3r/AppConfig.cpp",
        "    if (get(\"hide_login_side_panel\").empty()) {\n        set_bool(\"hide_login_side_panel\", false);\n",
        "    if (get(\"hide_login_side_panel\").empty()) {\n        set_bool(\"hide_login_side_panel\", true); // EulenSlicer\n",
        1,
    ),
    # Anfragen ans Portal ohne Weiterleitungen: curl schickte X-Api-Key sonst auch an
    # das neue Ziel. Http folgt sonst fest eingestellt jeder Weiterleitung.
    (
        "src/slic3r/Utils/Http.hpp",
        "\tHttp& tls_verify(bool enable);\n",
        "\tHttp& tls_verify(bool enable);\n"
        "\t// EulenSlicer: Weiterleitungen folgen (Standard) oder nicht.\n"
        "\tHttp& follow_redirects(bool enable);\n",
        1,
    ),
    (
        "src/slic3r/Utils/Http.cpp",
        "\tbool cancel;\n",
        "\tbool cancel;\n\tbool follow_location = true; // EulenSlicer\n",
        1,
    ),
    (
        "src/slic3r/Utils/Http.cpp",
        "\t::curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);\n",
        "\t::curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, follow_location ? 1L : 0L);\n",
        1,
    ),
    (
        "src/slic3r/Utils/Http.cpp",
        "Http& Http::tls_verify(bool enable)\n",
        "Http& Http::follow_redirects(bool enable)\n"
        "{\n"
        "\tif (p) { p->follow_location = enable; }\n"
        "\treturn *this;\n"
        "}\n"
        "\n"
        "Http& Http::tls_verify(bool enable)\n",
        1,
    ),
    # Neue Quelldateien bauen
    (
        "src/slic3r/CMakeLists.txt",
        "    GUI/UpdateDialogs.cpp\n",
        "    GUI/UpdateDialogs.cpp\n"
        "    GUI/EulenKonfig.hpp\n"
        "    GUI/EulenPortal.cpp\n"
        "    GUI/EulenPortal.hpp\n"
        "    GUI/EulenPortalKern.cpp\n"
        "    GUI/EulenPortalKern.hpp\n"
        "    GUI/EulenUpdater.cpp\n"
        "    GUI/EulenUpdater.hpp\n"
        "    GUI/EulenUpdaterKern.cpp\n"
        "    GUI/EulenUpdaterKern.hpp\n",
        1,
    ),
]


def fehler(text: str) -> None:
    print(f"::error::{text}" if os.environ.get("GITHUB_ACTIONS") else f"Fehler: {text}", file=sys.stderr)
    sys.exit(1)


def orca_version(orca: Path) -> str:
    inc = (orca / "version.inc").read_text(encoding="utf-8")
    m = re.search(r'set\(SoftFever_VERSION "([0-9]+\.[0-9]+\.[0-9]+[^"]*)"\)', inc)
    if not m:
        fehler("version.inc: SoftFever_VERSION nicht gefunden.")
    return m.group(1)


def pubkey_pruefen(key: str) -> str:
    key = re.sub(r"\s+", "", key)
    if not key:
        return ""
    try:
        roh = base64.b64decode(key + "=" * (-len(key) % 4), altchars=b"-_" if ("-" in key or "_" in key) else None, validate=True)
    except Exception:  # noqa: BLE001
        fehler("UPDATE_PUBKEY ist kein Base64.")
    if len(roh) != 32:
        fehler("UPDATE_PUBKEY muss 32 Byte lang sein (Ed25519, so wie ihn das Portal unter Einstellungen zeigt).")
    return base64.b64encode(roh).decode()


def update_url(api: str) -> str:
    api = api.strip()
    if not api:
        return ""
    if not api.endswith("/"):
        api += "/"
    if not api.startswith("https://") and not re.match(r"^http://(localhost|127\.0\.0\.1)[:/]", api):
        fehler("PORTAL_API muss mit https:// beginnen.")
    return api + "slicer/update"


def portal_api(api: str) -> str:
    """REST-Adresse des Portals mit „/“ am Ende (leer, wenn keine eingetragen ist)."""
    url = update_url(api)
    return url[: -len("slicer/update")] if url else ""


def c_text(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def ersetzen(orca: Path, pruefen: bool) -> list[str]:
    probleme: list[str] = []
    for datei, alt, neu, anzahl in ERSETZUNGEN:
        pfad = orca / datei
        if not pfad.is_file():
            probleme.append(f"{datei}: Datei fehlt")
            continue
        text = pfad.read_text(encoding="utf-8")
        gefunden = text.count(alt)
        # Zuerst: schon geändert? (Bei Einfügungen steckt das Original im Ersatz.)
        if text.count(neu) == anzahl:
            print(f"  schon da  {datei}: {alt.strip()[:60]}")
        elif gefunden == anzahl:
            if not pruefen:
                pfad.write_text(text.replace(alt, neu), encoding="utf-8", newline="")
            print(f"  ok        {datei}: {alt.strip()[:60]}")
        else:
            probleme.append(f"{datei}: Stelle {gefunden}× gefunden, erwartet {anzahl}× – {alt.strip()[:80]!r}")
    return probleme


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--orca", required=True, help="Checkout von OrcaSlicer")
    ap.add_argument("--build", type=int, default=int(os.environ.get("EULEN_BUILD", "0") or 0))
    ap.add_argument("--portal-api", default=os.environ.get("PORTAL_API", ""))
    ap.add_argument("--pubkey", default=os.environ.get("UPDATE_PUBKEY", ""))
    ap.add_argument("--pruefen", action="store_true", help="nur prüfen, ob alle Stellen passen")
    ap.add_argument("--dateien", action="store_true", help="nur die .cpp-Dateien ausgeben, die der EulenSlicer ändert oder bringt")
    args = ap.parse_args()

    if args.dateien:
        dateien = {d for d, _, _, _ in ERSETZUNGEN if d.endswith(".cpp")}
        dateien |= {f"src/slic3r/GUI/{q.name}" for q in (HIER / "quellen").glob("*.cpp")}
        for d in sorted(dateien):
            print(d)
        return

    orca = Path(args.orca).resolve()
    if not (orca / "src" / "slic3r" / "GUI" / "GUI_App.cpp").is_file():
        fehler(f"{orca} ist kein Checkout von OrcaSlicer.")
    version = orca_version(orca)
    print(f"OrcaSlicer {version} in {orca}")

    print("Stellen:")
    probleme = ersetzen(orca, args.pruefen)
    if probleme:
        for p in probleme:
            print(f"  FEHLT     {p}", file=sys.stderr)
        fehler(f"{len(probleme)} Stelle(n) passen nicht zu OrcaSlicer {version} – skripte/anwenden.py anpassen.")
    if args.pruefen:
        print("Alle Stellen passen.")
        return

    if args.build < 1 or args.build > 65535:
        fehler("--build (bzw. EULEN_BUILD) muss zwischen 1 und 65535 liegen (dritte Stelle der MSI-Version).")
    url = update_url(args.portal_api)
    key = pubkey_pruefen(args.pubkey)
    if not url:
        print("Warnung: PORTAL_API fehlt – dieser Build sucht nicht nach Updates.", file=sys.stderr)
    if not key:
        print("Warnung: UPDATE_PUBKEY fehlt – dieser Build installiert keine Updates.", file=sys.stderr)

    ziel = orca / "src" / "slic3r" / "GUI"
    for quelle in sorted((HIER / "quellen").glob("*.[ch]pp")):
        shutil.copy2(quelle, ziel / quelle.name)
        print(f"  kopiert   {quelle.name}")
    bilder = orca / "resources" / "images"
    for bild in sorted((HIER / "bilder").glob("*")):
        if bild.is_file() and bild.suffix in (".svg", ".png", ".ico"):
            if not (bilder / bild.name).is_file():
                fehler(f"resources/images/{bild.name} gibt es in OrcaSlicer nicht (mehr) – bilder/ anpassen.")
            shutil.copy2(bild, bilder / bild.name)
    print("  kopiert   bilder/* nach resources/images/")
    (ziel / "EulenKonfig.hpp").write_text(
        "// Erzeugt von skripte/anwenden.py (EulenSlicer) – nicht von Hand ändern.\n"
        "#pragma once\n\n"
        "#define EULENSLICER 1\n"
        f"#define EULEN_ORCA_VERSION {c_text(version)}\n"
        f"#define EULEN_BUILD {args.build}\n"
        f"#define EULEN_UPDATE_URL {c_text(url)}\n"
        f"#define EULEN_PORTAL_API {c_text(portal_api(args.portal_api))}\n"
        f"#define EULEN_UPDATE_PUBKEY {c_text(key)}\n",
        encoding="utf-8",
        newline="\n",
    )
    print("  geschrieben EulenKonfig.hpp")

    teile = version.split("-")[0].split(".")
    msi_version = f"{int(teile[0])}.{int(teile[1])}.{args.build}"
    print(f"Version {version}, Build {args.build}, MSI-Version {msi_version}")
    ausgabe = os.environ.get("GITHUB_OUTPUT")
    if ausgabe:
        with open(ausgabe, "a", encoding="utf-8") as f:
            f.write(f"orca_version={version}\nbuild={args.build}\nmsi_version={msi_version}\n")


if __name__ == "__main__":
    main()
