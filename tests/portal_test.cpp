// Tests für quellen/EulenPortalKern.cpp (ohne wxWidgets):
//   g++ -std=c++17 -I quellen -I <orca>/deps_src tests/portal_test.cpp quellen/EulenPortalKern.cpp -o portal_test
#include "EulenPortalKern.hpp"

#include <cstdio>
#include <set>
#include <string>

using namespace Slic3r::Eulen;

static int g_fehler = 0;
static int g_ok     = 0;

static void pruefe(bool bedingung, const char* name)
{
    if (bedingung) {
        ++g_ok;
    } else {
        ++g_fehler;
        std::printf("FEHL %s\n", name);
    }
}

int main()
{
    const std::string api  = "https://dev.3ed.app/wp-json/eulen/v1/";
    const std::string api2 = "https://dev.3ed.app/?rest_route=/eulen/v1/";

    // Adressen
    pruefe(mit_param("https://x/a", {{"dunkel", "1"}}) == "https://x/a?dunkel=1", "mit_param ?");
    pruefe(mit_param("https://x/?rest_route=/a", {{"dunkel", "1"}, {"auftrag", "42"}}) == "https://x/?rest_route=/a&dunkel=1&auftrag=42", "mit_param &");
    pruefe(mit_param("https://x/a", {}) == "https://x/a", "mit_param leer");
    pruefe(url_kodieren("Ä b/c") == "%C3%84%20b%2Fc", "url_kodieren");
    pruefe(seiten_url(api, "start", {{"dunkel", "1"}}) == "https://dev.3ed.app/wp-json/eulen/v1/slicer/seite/start?dunkel=1", "seiten_url");
    pruefe(seiten_url(api2, "senden") == "https://dev.3ed.app/?rest_route=/eulen/v1/slicer/seite/senden", "seiten_url rest_route");
    pruefe(seiten_url("", "start").empty(), "seiten_url ohne Portal");
    pruefe(ursprung("HTTPS://Dev.3ED.app:443/x") == "https://dev.3ed.app", "ursprung normalisiert");
    pruefe(ursprung("https://user@evil.example/x").empty(), "ursprung mit Benutzer");
    pruefe(ursprung("file:///C:/x").empty(), "ursprung file");

    pruefe(ist_portal_seite(api, "https://dev.3ed.app/wp-json/eulen/v1/slicer/seite/start?dunkel=1"), "Seite ok");
    pruefe(ist_portal_seite(api, "https://DEV.3ed.app/wp-json/eulen/v1/slicer/seite/auftraege"), "Seite Host groß");
    pruefe(ist_portal_seite(api2, "https://dev.3ed.app/?rest_route=/eulen/v1/slicer/seite/start&dunkel=1"), "Seite rest_route");
    pruefe(!ist_portal_seite(api, "https://dev.3ed.app/wp-json/eulen/v1/slicer/api/ich"), "API ist keine Seite");
    pruefe(!ist_portal_seite(api, "https://dev.3ed.app.evil.example/wp-json/eulen/v1/slicer/seite/start"), "fremder Host mit gleichem Anfang");
    pruefe(!ist_portal_seite(api, "https://evil.example/?x=https://dev.3ed.app/wp-json/eulen/v1/slicer/seite/start"), "Adresse im Parameter");
    pruefe(!ist_portal_seite(api, "http://dev.3ed.app/wp-json/eulen/v1/slicer/seite/start"), "http statt https");
    pruefe(!ist_portal_seite(api, "https://dev.3ed.app/wp-json/eulen/v1/slicer/seite/../../../wp-admin/"), "Pfad mit ..");
    pruefe(!ist_portal_seite(api, "https://dev.3ed.app/wp-json/eulen/v1/slicer/seite/%2e%2e/x"), "Pfad mit %2e");
    pruefe(!ist_portal_seite(api, "https://dev.3ed.app/wp-json/eulen/v1/slicer/seite/start x"), "Leerzeichen");
    pruefe(!ist_portal_seite(api, "file:///C:/Programme/EulenSlicer/resources/web/homepage/index.html"), "lokale Startseite");
    pruefe(!ist_portal_seite("", "https://dev.3ed.app/wp-json/eulen/v1/slicer/seite/start"), "ohne Portal");
    pruefe(ist_portal_adresse(api, "https://dev.3ed.app/wp-json/eulen/v1/druckauftraege/laden/1.2.abc/DA0001.3mf"), "Link zum Öffnen");
    pruefe(!ist_portal_adresse(api, "https://example.org/datei.3mf"), "fremder Link");
    pruefe(ist_http("HTTPS://x") && ist_http("http://x") && !ist_http("javascript:alert(1)") && !ist_http("file:///x"), "ist_http");

    // Fehlertexte
    pruefe(fehlertext("{\"code\":\"x\",\"message\":\"Der Werkstatt-Code stimmt nicht.\"}", 401, "") == "Der Werkstatt-Code stimmt nicht.", "fehlertext JSON");
    pruefe(fehlertext("<html>", 502, "") == "Das Portal antwortet mit HTTP 502.", "fehlertext HTTP");
    pruefe(fehlertext("", 0, "Could not resolve host") == "Could not resolve host", "fehlertext curl");

    // Dateinamen
    pruefe(profil_dateiname("3 Eulen PLA @A1") == "3 Eulen PLA @A1", "Dateiname normal");
    pruefe(profil_dateiname("a/b\\c:d*e?f\"g<h>i|j") == "abcdefghij", "Dateiname verbotene Zeichen");
    pruefe(profil_dateiname("  ..PLA.. ") == "PLA", "Dateiname Ränder");
    pruefe(profil_dateiname("CON") == "_CON" && profil_dateiname("nul.x") == "_nul.x", "Dateiname reserviert");
    pruefe(profil_dateiname("...").empty(), "Dateiname leer");
    std::string lang(130, 'a');
    lang += "ÄÖÜ";
    pruefe(profil_dateiname(lang).size() == 120, "Dateiname gekürzt");
    std::string umlaut(119, 'a');
    umlaut += "Ä"; // 2 Byte, ginge über 120
    pruefe(profil_dateiname(umlaut) == std::string(119, 'a'), "Dateiname nicht mitten im Zeichen");

    // Profile aus dem Portal
    const std::string liste = R"J([
      {"id":"aaaaaaaaaaaaaaaa","art":"filament","name":"3 Eulen PLA @A1","sha1":"0123456789abcdef0123456789abcdef01234567"},
      {"id":"bbbbbbbbbbbbbbbb","art":"process","name":"0.20mm 3 Eulen @A1","sha1":"1111111111111111111111111111111111111111"},
      {"id":"cccccccccccccccc","art":"machine","name":"Eigener Drucker","sha1":"2222222222222222222222222222222222222222"},
      {"id":"BAD","art":"filament","name":"x","sha1":"0123456789abcdef0123456789abcdef01234567"},
      {"id":"dddddddddddddddd","art":"sla","name":"x","sha1":"0123456789abcdef0123456789abcdef01234567"},
      {"id":"eeeeeeeeeeeeeeee","art":"filament","name":"x","sha1":"kurz"}
    ])J";
    const auto portal = profile_lesen(liste);
    pruefe(portal.size() == 3, "ungültige Einträge fallen weg");
    pruefe(profile_lesen("{kaputt").empty() && profile_lesen("{\"a\":1}").empty(), "kaputte Liste");

    // Erster Abgleich: alles neu, „Eigener Drucker“ gibt es schon als eigenes Profil.
    std::set<std::string> da = {"machine/Eigener Drucker.json"};
    auto datei_da = [&](const std::string& d) { return da.count(d) > 0; };
    std::map<std::string, Profil> verz;
    auto plan = profil_plan(portal, verz, datei_da);
    pruefe(plan.neu == 2 && plan.laden.size() == 2 && plan.belegt.size() == 1 && plan.entfernen.empty(), "erster Abgleich");
    pruefe(plan.laden[0].datei == "filament/3 Eulen PLA @A1.json", "Pfad im Plan");

    // Verzeichnis nach dem Laden
    for (const auto& p : plan.laden) {
        verz[p.id] = p;
        da.insert(p.datei);
    }
    const auto gelesen = verzeichnis_lesen(verzeichnis_schreiben(verz));
    pruefe(gelesen.size() == 2 && gelesen.at("aaaaaaaaaaaaaaaa").datei == "filament/3 Eulen PLA @A1.json", "Verzeichnis hin und zurück");
    pruefe(verzeichnis_lesen("{\"profile\":{\"aaaaaaaaaaaaaaaa\":{\"art\":\"filament\",\"name\":\"x\",\"sha1\":\"\",\"datei\":\"../../x.json\"}}}").empty(), "Verzeichnis ohne ..");

    // Zweiter Abgleich ohne Änderung: nichts zu tun
    plan = profil_plan(portal, gelesen, datei_da);
    pruefe(plan.laden.empty() && plan.entfernen.empty() && plan.belegt.size() == 1, "nichts geändert");

    // Geändert (neue Prüfsumme), umbenannt, gelöscht
    auto portal2 = portal;
    portal2[0].sha1 = "ffffffffffffffffffffffffffffffffffffffff";
    portal2[1].name = "0.16mm 3 Eulen @A1";
    portal2.erase(portal2.begin() + 2);
    portal2[1].id = "bbbbbbbbbbbbbbbb";
    plan = profil_plan(portal2, gelesen, datei_da);
    pruefe(plan.geaendert == 2 && plan.neu == 0, "geändert und umbenannt");
    bool alt_weg = false;
    for (const auto& e : plan.entfernen)
        alt_weg = alt_weg || e.datei == "process/0.20mm 3 Eulen @A1.json";
    pruefe(alt_weg && plan.entfernen.size() == 1, "alte Datei der Umbenennung entfernen");

    // Im Portal gelöscht
    plan = profil_plan({portal[0]}, gelesen, datei_da);
    pruefe(plan.entfernen.size() == 1 && plan.entfernen[0].id == "bbbbbbbbbbbbbbbb", "gelöscht");

    // Datei fehlt lokal (von Hand gelöscht) → neu laden
    da.erase("filament/3 Eulen PLA @A1.json");
    plan = profil_plan(portal, gelesen, datei_da);
    pruefe(plan.laden.size() == 1 && plan.laden[0].id == "aaaaaaaaaaaaaaaa", "fehlende Datei neu laden");

    std::printf("%d ok, %d fehlgeschlagen\n", g_ok, g_fehler);
    return g_fehler == 0 ? 0 : 1;
}
