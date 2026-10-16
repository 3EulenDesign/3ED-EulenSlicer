// EulenSlicer – Verbindung zum 3 Eulen Portal: Kern ohne Oberfläche.
//
// Adressen (welche Seiten dürfen mit dem Slicer reden?), Fehlertexte aus den
// Antworten des Portals und der Abgleich der Profile aus dem Portal. Nur
// Standardbibliothek und nlohmann/json – getestet in tests/portal_test.cpp.
//
// Lizenz: AGPL-3.0 wie OrcaSlicer.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {
namespace Eulen {

using Parameter = std::vector<std::pair<std::string, std::string>>;

// Prozent-Kodierung für Parameter (RFC 3986, unreservierte Zeichen bleiben).
std::string url_kodieren(const std::string& s);

// Parameter anhängen – mit „?“ oder „&“, je nachdem, ob die Adresse schon
// einen Teil mit „?“ hat (…/?rest_route=/eulen/v1/…).
std::string mit_param(const std::string& basis, const Parameter& param);

// „https://host[:port]“ in Kleinbuchstaben, leer bei ungültiger Adresse.
std::string ursprung(const std::string& url);

// Liegt url unter basis? Schema und Host ohne Rücksicht auf Groß/klein, dazu
// keine Leer- oder Steuerzeichen, kein „\“ und kein „..“ im Pfad.
bool unter_adresse(const std::string& basis, const std::string& url);

// Adressen aus der REST-Adresse des Portals (…/wp-json/eulen/v1/).
std::string seiten_basis(const std::string& api); // …/slicer/seite/
std::string api_basis(const std::string& api);    // …/slicer/api/
std::string seiten_url(const std::string& api, const std::string& seite, const Parameter& param = {});

// Darf diese Seite mit dem Slicer reden (Befehle eulen_*)?
bool ist_portal_seite(const std::string& api, const std::string& url);
// Adresse unter der REST-Adresse des Portals (z. B. signierte Links zum Öffnen)?
bool ist_portal_adresse(const std::string& api, const std::string& url);
// http:// oder https://
bool ist_http(const std::string& url);

// Verständlicher Fehlertext aus einer Antwort: „message“ aus dem JSON des Portals,
// sonst der Fehler von curl oder „HTTP 500“.
std::string fehlertext(const std::string& body, unsigned status, const std::string& fehler);

// Profile -------------------------------------------------------------------

struct Profil
{
    std::string id;   // 16 Zeichen a-z0-9
    std::string art;  // machine, filament, process
    std::string name;
    std::string sha1; // 40 Zeichen hex
    std::string datei; // nur im Verzeichnis: relativer Pfad, z. B. „filament/3 Eulen PLA.json“
};

// Sicherer Dateiname (ohne Endung) für einen Profilnamen; leer, wenn keiner übrig bleibt.
std::string profil_dateiname(const std::string& name);

// Liste aus GET /slicer/api/profile – ungültige Einträge fallen weg.
std::vector<Profil> profile_lesen(const std::string& json_text);

// Verzeichnis der Profile aus dem Portal (eulen_profile.json im Ordner der eigenen Profile).
std::map<std::string, Profil> verzeichnis_lesen(const std::string& json_text);
std::string verzeichnis_schreiben(const std::map<std::string, Profil>& verzeichnis);

struct ProfilPlan
{
    std::vector<Profil> laden;     // neu oder geändert (datei gesetzt)
    std::vector<Profil> entfernen; // nicht mehr im Portal oder umbenannt (alte datei)
    std::vector<Profil> belegt;    // gleichnamiges eigenes Profil – bleibt unangetastet
    int neu = 0;
    int geaendert = 0;
};

// Was ist zu tun? datei_da(relativer Pfad) sagt, ob eine Datei schon existiert.
ProfilPlan profil_plan(const std::vector<Profil>& portal,
                       const std::map<std::string, Profil>& verzeichnis,
                       const std::function<bool(const std::string&)>& datei_da);

} // namespace Eulen
} // namespace Slic3r
