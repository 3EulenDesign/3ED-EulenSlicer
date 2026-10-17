// EulenSlicer – Verbindung zum 3 Eulen Portal (Oberfläche).
//
// - Startseite: Der Reiter „Start“ zeigt die Seite …/slicer/seite/start aus dem
//   Portal, sobald sie erreichbar ist (sonst die normale Startseite von OrcaSlicer).
// - Fenster: „An 3 Eulen senden“, „Meine Aufträge“ und „Anmelden“ sind Seiten aus
//   dem Portal in einem eigenen Fenster.
// - Brücke: Die Seiten schicken Befehle eulen_* über window.wx.postMessage, der
//   Slicer antwortet mit window.postMessage(…). Nur Seiten unter der Adresse des
//   Portals bekommen eine Antwort (Liste der Befehle: ENTWICKLER.md im Repo 3ED-App).
// - Konto: Zugang (Anwendungspasswort) bzw. Werkstatt-Code liegen in der
//   Windows-Anmeldeinformationsverwaltung, nicht in der Konfiguration.
// - Senden: Projekt als 3MF speichern (geslicet mit G-Code der Platte), in Stücken
//   im Portal ablegen, Fenster „senden“ öffnen.
// - Profile: Profile aus dem Portal beim Start und auf Wunsch abgleichen.
// - Werkstatt: Der Reiter „Gerät“ zeigt die Drucker aus dem Portal.
//
// Lizenz: AGPL-3.0 wie OrcaSlicer.
#pragma once

#include <string>
#include <vector>

#include <wx/string.h>

#include "EulenKonfig.hpp"

class wxWebView;
class wxWebViewEvent;
class wxWindow;
class wxMenu;

namespace Slic3r {
namespace GUI {

// Ist ein Portal eingetragen (Build mit PORTAL_API)?
bool eulen_portal_aktiv();

// Reiter „Start“: nach dem Anlegen des Browsers aufrufen. lokal = Adresse der
// normalen Startseite (Rückfall, wenn das Portal nicht erreichbar ist).
void eulen_start_einrichten(wxWebView* browser, const wxString& lokal);
// Navigation im Reiter „Start“: true = erledigt (verhindert oder umgeleitet).
bool eulen_start_navigation(wxWebView* browser, wxWebViewEvent& evt);
// Ladefehler im Reiter „Start“: true = erledigt (normale Startseite geladen).
bool eulen_start_fehler(wxWebView* browser, wxWebViewEvent& evt);

// Nachricht einer Seite (window.wx.postMessage). true = Befehl eulen_* (erledigt);
// false = nicht für uns, OrcaSlicer macht weiter.
bool eulen_web_nachricht(wxWebView* browser, const wxString& nachricht, wxWindow* fenster);

// Fenster mit einer Seite des Portals öffnen (start, anmelden, senden, auftraege).
void eulen_fenster(const std::string& seite, int auftrag = 0);

// „An 3 Eulen senden“ (Menü, Knopf neben „Drucken“, Startseite).
void eulen_senden();

// Profile aus dem Portal abgleichen. vom_nutzer: Ergebnis auch melden, wenn sich nichts ändert.
void eulen_profile_abgleichen(bool vom_nutzer);

// Menü „3 Eulen“ (in der Titelleiste neben „Hilfe“).
wxMenu* eulen_menue(wxWindow* fenster);

// Reiter „Gerät“: Drucker der Werkstatt aus dem Portal (Seite „geraete“), ersetzt die
// Ansicht für einen direkt verbundenen Drucker. nullptr, wenn kein Portal eingetragen ist.
wxWindow* eulen_geraete_seite(wxWindow* parent);

// Einmal nach dem Start (mit der Update-Prüfung): Profile abgleichen.
void eulen_beim_start();

// Fenster „Über EulenSlicer“ (AboutDialog von OrcaSlicer): eigene Absätze, eine Zeile über
// dem Copyright von OrcaSlicer (das bleibt stehen, AGPL) und die Links darunter (HTML).
std::vector<wxString> eulen_ueber_texte();
wxString eulen_ueber_copyright();
wxString eulen_ueber_links();

} // namespace GUI
} // namespace Slic3r
