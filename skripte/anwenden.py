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
  3. die Stellen in ERSETZUNGEN ändern, das Türkis von OrcaSlicer durch das Blau von
     3 Eulen ersetzen (FARBEN)
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
        "                eulen_btn->Bind(wxEVT_BUTTON, [this, p](wxCommandEvent&) {\n"
        "                    m_print_btn->SetLabel(wxString::FromUTF8(\"An 3 Eulen senden\"));\n"
        "                    m_print_select = eEulenSenden;\n"
        "                    m_print_enable = get_enable_print_status();\n"
        "                    m_print_btn->Enable(m_print_enable);\n"
        "                    this->Layout();\n"
        "                    fit_tab_labels();\n"
        "                    p->Dismiss();\n"
        "                });\n"
        "                p->append_button(eulen_btn);\n"
        "            }\n"
        "            p->Popup(m_print_btn);\n",
        1,
    ),
    # „An 3 Eulen senden“ als Auswahl des Knopfs „Drucken“ – und als Standard, wenn das Portal
    # eingerichtet ist. Geht auch ungeslicet (dann das ganze Projekt).
    (
        "src/slic3r/GUI/MainFrame.hpp",
        "        ePrintMultiMachine   = 9\n    };\n",
        "        ePrintMultiMachine   = 9,\n        eEulenSenden         = 50 // EulenSlicer: ans 3 Eulen Portal\n    };\n",
        1,
    ),
    (
        "src/slic3r/GUI/MainFrame.cpp",
        "    m_print_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)\n        {\n",
        "    m_print_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)\n        {\n"
        "            if (m_print_select == eEulenSenden) { // EulenSlicer\n"
        "                wxGetApp().CallAfter([] { eulen_senden(); });\n"
        "                return;\n"
        "            }\n",
        1,
    ),
    (
        "src/slic3r/GUI/MainFrame.cpp",
        "    else if (m_print_select == ePrintMultiMachine)\n    {\n        if (!current_plate->is_slice_result_ready_for_print())\n",
        "    else if (m_print_select == eEulenSenden) // EulenSlicer: auch ungeslicet, nur nicht leer\n"
        "    {\n"
        "        enable = m_plater != nullptr && !m_plater->model().objects.empty();\n"
        "    }\n"
        "    else if (m_print_select == ePrintMultiMachine)\n    {\n        if (!current_plate->is_slice_result_ready_for_print())\n",
        1,
    ),
    (
        "src/slic3r/GUI/MainFrame.cpp",
        "    if (!can_print)\n        enable_print = false;\n",
        "    if (!can_print && m_print_select != eEulenSenden) // EulenSlicer: Senden braucht kein Slicen\n        enable_print = false;\n",
        1,
    ),
    (
        "src/slic3r/GUI/MainFrame.cpp",
        "void MainFrame::set_print_button_to_default(PrintSelectType select_type)\n{\n",
        "void MainFrame::set_print_button_to_default(PrintSelectType select_type)\n{\n"
        "    // EulenSlicer: Mit Portal ist „An 3 Eulen senden“ die Standardauswahl.\n"
        "    if (eulen_portal_aktiv()) {\n"
        "        m_print_btn->SetLabel(wxString::FromUTF8(\"An 3 Eulen senden\"));\n"
        "        m_print_select = eEulenSenden;\n"
        "        m_print_enable = get_enable_print_status();\n"
        "        m_print_btn->Enable(m_print_enable);\n"
        "        this->Layout();\n"
        "        fit_tab_labels();\n"
        "        return;\n"
        "    }\n",
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
    # Links in Hinweisen: im Dunkelmodus das helle Blau der Website (#9DBCE0), sonst
    # wäre das Blau auf dem dunklen Grund schlecht lesbar. Hell: Blau wie in FARBEN.
    (
        "src/slic3r/GUI/NotificationManager.cpp",
        "\tm_HyperTextColor = m_is_dark ? ImVec4(0, 0.588, 0.533, 1) : ImVec4(0, 0.588, 0.533, 1);\n",
        "\tm_HyperTextColor = m_is_dark ? ImVec4(0.616, 0.737, 0.878, 1) : ImVec4(0.314, 0.439, 0.561, 1); // EulenSlicer\n",
        1,
    ),
    (
        "src/slic3r/GUI/NotificationManager.cpp",
        "\tm_HyperTextColorHover = m_is_dark ? ImVec4(38.f / 255.f, 166.f / 255.f, 154.f / 255.f, 1) : ImVec4(0.f, 129.f / 255.f, 114.f / 255.f, 1); //#26A69A / #008172;\n",
        "\tm_HyperTextColorHover = m_is_dark ? ImVec4(198.f / 255.f, 219.f / 255.f, 242.f / 255.f, 1) : ImVec4(106.f / 255.f, 133.f / 255.f, 160.f / 255.f, 1); // EulenSlicer: #C6DBF2 / #6A85A0\n",
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

# Farben: das Türkis von OrcaSlicer wird zum Blau von 3 Eulen Design (Schiefer-Blau #50708F wie
# die Buttons der Website, Theme drei-eulen). Schwarz, Weiß und Grau von OrcaSlicer bleiben.
# Ersetzt wird nach Farbwert, nicht nach Stelle – in allen Schreibweisen (#009688, 0x009688,
# wxColour(0, 150, 136), 150.f / 255.f, 0.59f …), in Quelltext, Symbolen und Seiten. So bricht
# ein Update von OrcaSlicer nichts; bleibt danach irgendwo Türkis übrig (neue Schreibweise,
# neuer Ton), meldet „Prüfen“ die Stelle.
FARBEN: dict[str, str] = {
    # Grundfarbe (Knöpfe, Schalter, Reiter, Rahmen, Symbole). Im Dunkelmodus dunkelt
    # OrcaSlicer sie ab (#00675B) – das Blau ist schon gedeckt genug und bleibt gleich.
    "009688": "50708F", "009789": "50708F", "009687": "50708F", "00675B": "50708F",
    # gedrückt / unter der Maus (hell und dunkel)
    "00897B": "486581", "26A69A": "6A85A0", "008172": "6A85A0",
    # hellere Töne in Symbolen und Seiten
    "33ABA1": "738DA5", "4DB6AC": "849BB1", "02C7B3": "849BB1", "00BFA5": "849BB1", "52C7B8": "8DA2B6",
    # Fokusrand und leuchtende Töne (Startseite, Assistent) → helles Blau der Website
    "22BFB0": "9DBCE0", "00FFD4": "C6DBF2", "00FFEA": "9DBCE0", "00FFD9": "9DBCE0",
    "00F0D8": "9DBCE0", "77EFF9": "9DBCE0",
    # Grund ausgewählter Einträge: 25 % / 10 % Blau auf Weiß bzw. auf dem dunklen Grau
    "BFE1DE": "D3DBE3", "E5F0EE": "EEF1F4", "223C3C": "38414D", "244945": "38414D", "283232": "32373F",
}
# Diese Töne kommen auch als Kommazahlen vor (ImGui: 0.59f, 0.53f bzw. 150.f / 255.f).
FARBEN_ZAHLEN = ("009688", "00897B", "26A69A", "008172", "00675B")
# Wo ersetzt wird (Ordner bzw. Datei, Endungen). Nicht: Profile (Filamentfarben), Farbpaletten
# für Filamente und Farbskalen der Vorschau – deren Töne stehen nicht in FARBEN.
FARB_ORTE: list[tuple[str, tuple[str, ...]]] = [
    ("src/slic3r", (".cpp", ".hpp", ".h")),
    ("src/libslic3r/Color.hpp", ()),
    ("deps_src/imgui", (".cpp", ".h")),
    ("resources/images", (".svg",)),
    ("resources/web", (".css", ".html", ".htm", ".js", ".svg")),
]
# Welche .cpp-Dateien das Umfärben geändert hat (für „Kompilieren“, steht im Checkout).
FARB_LISTE = ".eulen-farben.txt"


def _rgb(hexwert: str) -> tuple[int, int, int]:
    return int(hexwert[0:2], 16), int(hexwert[2:4], 16), int(hexwert[4:6], 16)


_HEX = re.compile(r"(#|(?<![\w])0x)([0-9A-Fa-f]{6})(?![0-9A-Fa-f])")
_INTS = re.compile(r"\b(wxColour|wxColor|ImColor|IM_COL32)(\s*\(\s*)(\d{1,3})(\s*,\s*)(\d{1,3})(\s*,\s*)(\d{1,3})(?!\d)")
_BRUCH = re.compile(
    r"(?<![\w.])(\d{1,3})(?:\.0*)?f?(?:\s*/\s*255(?:\.0*)?f?)?\s*,\s*(\d{1,3})(?:\.0*)?f?\s*/\s*255(?:\.0*)?f?"
    r"\s*,\s*(\d{1,3})(?:\.0*)?f?\s*/\s*255(?:\.0*)?f?"
)
_KOMMA = re.compile(r"(?<![\w.])(0|1|\d?\.\d+)(f?)\s*,\s*(\d?\.\d+)f?\s*,\s*(\d?\.\d+)f?(?![\d.])")
_NACH_RGB = {_rgb(a): _rgb(n) for a, n in FARBEN.items()}
_ZAHLEN_RGB = {_rgb(a): _rgb(FARBEN[a]) for a in FARBEN_ZAHLEN}


def _komma_ziel(r: float, g: float, b: float) -> tuple[int, int, int] | None:
    """Kommazahlen (0…1) einer Farbe aus FARBEN_ZAHLEN zuordnen (ImGui rundet auf 2–3 Stellen)."""
    if max(r, g, b) > 1:
        return None
    ist = (r * 255, g * 255, b * 255)
    for alt, neu in _ZAHLEN_RGB.items():
        if all(abs(ist[i] - alt[i]) <= 2 for i in range(3)):
            return neu
    return None


def farben_tauschen(text: str) -> tuple[str, int]:
    """Alle Türkis-Töne aus FARBEN im Text ersetzen. Gibt den neuen Text und die Anzahl zurück."""
    n = 0

    def hexwert(m: re.Match) -> str:
        nonlocal n
        neu = FARBEN.get(m.group(2).upper())
        if neu is None:
            return m.group(0)
        n += 1
        return m.group(1) + neu

    def ganzzahlen(m: re.Match) -> str:
        nonlocal n
        neu = _NACH_RGB.get((int(m.group(3)), int(m.group(5)), int(m.group(7))))
        if neu is None:
            return m.group(0)
        n += 1
        return f"{m.group(1)}{m.group(2)}{neu[0]}{m.group(4)}{neu[1]}{m.group(6)}{neu[2]}"

    def bruch(m: re.Match) -> str:
        nonlocal n
        neu = _ZAHLEN_RGB.get((int(m.group(1)), int(m.group(2)), int(m.group(3))))
        if neu is None:
            return m.group(0)
        n += 1
        return f"{neu[0]}.f / 255.f, {neu[1]}.f / 255.f, {neu[2]}.f / 255.f"

    def komma(m: re.Match) -> str:
        nonlocal n
        neu = _komma_ziel(float(m.group(1)), float(m.group(3)), float(m.group(4)))
        if neu is None:
            return m.group(0)
        n += 1
        f = m.group(2)
        return ", ".join(f"{c / 255:.3f}{f}" for c in neu)

    text = _HEX.sub(hexwert, text)
    text = _INTS.sub(ganzzahlen, text)
    text = _BRUCH.sub(bruch, text)
    text = _KOMMA.sub(komma, text)
    # BitmapCache: Türkis als ABGR-Zahl (färbt Symbole beim Laden um)
    abgr_alt = "0xFF889600"
    if abgr_alt in text:
        r, g, b = _rgb(FARBEN["009688"])
        n += text.count(abgr_alt)
        text = text.replace(abgr_alt, f"0xFF{b:02X}{g:02X}{r:02X}")
    return text, n


def farb_dateien(orca: Path) -> list[Path]:
    dateien: list[Path] = []
    for ort, endungen in FARB_ORTE:
        pfad = orca / ort
        if pfad.is_file():
            dateien.append(pfad)
        elif pfad.is_dir():
            dateien += sorted(p for p in pfad.rglob("*") if p.is_file() and p.suffix.lower() in endungen)
    return dateien


def farben(orca: Path, pruefen: bool) -> list[str]:
    """Türkis → Blau. Gibt Probleme zurück (Türkis, das danach noch übrig wäre)."""
    probleme: list[str] = []
    gesamt = 0
    geaendert: list[str] = []
    for pfad in farb_dateien(orca):
        roh = pfad.read_bytes()  # Bytes: Zeilenenden und Kodierung bleiben, wie sie sind
        try:
            text, kodierung = roh.decode("utf-8"), "utf-8"
        except UnicodeDecodeError:
            text, kodierung = roh.decode("latin-1"), "latin-1"
        neu, n = farben_tauschen(text)
        rel = pfad.relative_to(orca).as_posix()
        if n:
            gesamt += n
            geaendert.append(rel)
            if not pruefen:
                pfad.write_bytes(neu.encode(kodierung))
        # Rest: noch ein Türkis aus FARBEN in irgendeiner Schreibweise?
        rest, m = farben_tauschen(neu)
        if m:
            probleme.append(f"{rel}: {m}× Türkis nach dem Ersetzen übrig")
    print(f"  Farben    {gesamt} Stellen in {len(geaendert)} Dateien (Türkis zu Blau)")
    if not pruefen:
        cpp = [d for d in geaendert if d.endswith(".cpp") and d.startswith("src/")]
        alt = []
        liste = orca / FARB_LISTE
        if liste.is_file():  # zweiter Lauf: dann ist schon alles blau, Liste behalten
            alt = [z for z in liste.read_text(encoding="utf-8").split() if z]
        liste.write_text("\n".join(sorted(set(cpp) | set(alt))) + "\n", encoding="utf-8")
    if gesamt == 0 and not (orca / FARB_LISTE).is_file():
        probleme.append("kein Türkis gefunden – hat OrcaSlicer die Farbe gewechselt? FARBEN in skripte/anwenden.py anpassen")
    return probleme


def png_farben(orca: Path) -> None:
    """Türkis in den PNG-Bildern der Seiten (Startseite, Assistent) zu Blau umfärben.

    Je Bildpunkt mit Türkis-Ton: Anteil Türkis gegen Weiß (heller) bzw. Schwarz (dunkler)
    bestimmen und denselben Anteil Blau nehmen – so bleiben Kanten und Verläufe sauber.
    Braucht Pillow; ohne Pillow bleiben die Bilder türkis (Warnung).
    """
    try:
        from PIL import Image
    except ImportError:
        print("::warning::Pillow fehlt – PNG-Bilder der Seiten bleiben türkis (pip install pillow)." if os.environ.get("GITHUB_ACTIONS")
              else "Warnung: Pillow fehlt – PNG-Bilder der Seiten bleiben türkis (pip install pillow).", file=sys.stderr)
        return
    import colorsys

    blau = _rgb(FARBEN["009688"])
    l_tuerkis = 150 / 255 / 2  # Helligkeit (HLS) von #009688
    cache: dict[tuple[int, int, int], tuple[int, int, int] | None] = {}

    def neu(rgb: tuple[int, int, int]) -> tuple[int, int, int] | None:
        if rgb in cache:
            return cache[rgb]
        h, l, s = colorsys.rgb_to_hls(*(c / 255 for c in rgb))
        ziel = None
        if 150 <= h * 360 <= 200 and s >= 0.25 and 0.04 < l < 0.97:
            if l >= l_tuerkis:
                t = min(1.0, (1 - l) / (1 - l_tuerkis))
                ziel = tuple(round(255 + (c - 255) * t) for c in blau)
            else:
                t = l / l_tuerkis
                ziel = tuple(round(c * t) for c in blau)
        cache[rgb] = ziel
        return ziel

    anzahl = 0
    for pfad in sorted((orca / "resources" / "web").rglob("*.png")):
        try:
            with Image.open(pfad) as roh:
                bild = roh.convert("RGBA")
        except Exception:  # noqa: BLE001 – kaputte oder exotische Bilder überspringen
            continue
        punkte = list(bild.getdata())
        sichtbar = geaendert = 0
        aus = []
        for r, g, b, a in punkte:
            if a >= 40:
                sichtbar += 1
            z = neu((r, g, b)) if a else None
            if z is None:
                aus.append((r, g, b, a))
            else:
                geaendert += a >= 40
                aus.append((*z, a))
        # Nur Bilder, in denen Türkis wirklich vorkommt (nicht einzelne Punkte in Fotos).
        if sichtbar and geaendert / sichtbar >= 0.01:
            bild.putdata(aus)
            bild.save(pfad)
            anzahl += 1
    print(f"  Farben    {anzahl} PNG-Bilder der Seiten umgefärbt")


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
    # Windows-Runner schreiben in cp1252 – ein Zeichen außerhalb davon soll den Build
    # nicht abbrechen (so passiert mit „→“), sondern nur als „?“ erscheinen.
    for strom in (sys.stdout, sys.stderr):
        try:
            strom.reconfigure(errors="replace")
        except (AttributeError, ValueError):
            pass
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
        farb_liste = Path(args.orca) / FARB_LISTE  # vom Umfärben geändert (nach dem Anwenden)
        if farb_liste.is_file():
            dateien |= {z for z in farb_liste.read_text(encoding="utf-8").split() if z}
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
    probleme += farben(orca, args.pruefen)
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
    # Eule statt Orca auch auf den Seiten (Startseite, Willkommen im Assistenten).
    logo = orca / "resources" / "web" / "image" / "logo.png"
    if not logo.is_file():
        fehler("resources/web/image/logo.png gibt es in OrcaSlicer nicht (mehr) – skripte/anwenden.py anpassen.")
    shutil.copy2(HIER / "bilder" / "OrcaSlicer_154.png", logo)
    print("  kopiert   Eule nach resources/web/image/logo.png")
    png_farben(orca)
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
