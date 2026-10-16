// Tests für quellen/EulenPortalKern.cpp (ohne wxWidgets):
//   g++ -std=c++17 -I quellen -I <orca>/deps_src tests/portal_test.cpp quellen/EulenPortalKern.cpp -o portal_test
#include "EulenPortalKern.hpp"

#include <cctype>
#include <cstdio>
#include <map>
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
    pruefe(!ist_portal_seite(api, "https://dev.3ed.app/wp-json/eulen/v1/slicer/seite/start?rest_route=/eulen/v1/x"), "zweites rest_route");
    pruefe(!ist_portal_seite(api2, "https://dev.3ed.app/?rest_route=/eulen/v1/slicer/seite/start&%72est_route=/x"), "kodiertes rest_route");
    pruefe(ist_portal_seite(api, "https://dev.3ed.app/wp-json/eulen/v1/slicer/seite/auftraege?dunkel=1&auftrag=42"), "Seite mit Parametern");
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

    // Lokale Dateien: Pfad → Inhalt (Prüfsumme über inhalt_hash)
    std::map<std::string, std::string> dateien = {{"machine/Eigener Drucker.json", "{\"eigen\":1}"}};
    auto klein = [](std::string t) { for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return t; };
    auto datei_hash = [&](const std::string& d) {
        for (const auto& [pfad, inhalt] : dateien)
            if (klein(pfad) == klein(d)) // Windows: Groß/klein egal
                return inhalt_hash(inhalt);
        return std::string();
    };
    // So führt der Slicer einen Plan aus: erst entfernen, dann laden.
    std::map<std::string, Profil> verz;
    auto ausfuehren = [&](const ProfilPlan& plan) {
        for (const auto& v : plan.freigeben)
            verz.erase(v.id);
        for (const auto& v : plan.entfernen) {
            for (auto it = dateien.begin(); it != dateien.end(); ++it)
                if (klein(it->first) == klein(v.datei)) {
                    dateien.erase(it);
                    break;
                }
            auto it = verz.find(v.id);
            if (it != verz.end() && it->second.datei == v.datei)
                verz.erase(it);
        }
        for (Profil p : plan.laden) {
            const std::string inhalt = "{\"name\":\"" + p.name + "\",\"sha\":\"" + p.sha1 + "\"}";
            dateien[p.datei] = inhalt;
            p.inhalt = inhalt_hash(inhalt);
            verz[p.id] = p;
        }
    };

    pruefe(inhalt_hash("abc") == inhalt_hash("abc") && inhalt_hash("abc") != inhalt_hash("abd") && inhalt_hash("").size() == 16, "inhalt_hash");

    // Erster Abgleich: alles neu, „Eigener Drucker“ gibt es schon als eigenes Profil.
    auto plan = profil_plan(portal, verz, datei_hash);
    pruefe(plan.neu == 2 && plan.laden.size() == 2 && plan.belegt.size() == 1 && plan.entfernen.empty(), "erster Abgleich");
    pruefe(plan.laden[0].datei == "filament/3 Eulen PLA @A1.json", "Pfad im Plan");
    ausfuehren(plan);

    const auto gelesen = verzeichnis_lesen(verzeichnis_schreiben(verz));
    pruefe(gelesen.size() == 2 && gelesen.at("aaaaaaaaaaaaaaaa").datei == "filament/3 Eulen PLA @A1.json" && gelesen.at("aaaaaaaaaaaaaaaa").inhalt.size() == 16,
           "Verzeichnis hin und zurück");
    pruefe(verzeichnis_lesen("{\"profile\":{\"aaaaaaaaaaaaaaaa\":{\"art\":\"filament\",\"name\":\"x\",\"sha1\":\"\",\"datei\":\"../../x.json\"}}}").empty(), "Verzeichnis ohne ..");

    // Zweiter Abgleich ohne Änderung: nichts zu tun
    plan = profil_plan(portal, verz, datei_hash);
    pruefe(plan.laden.empty() && plan.entfernen.empty() && plan.freigeben.empty() && plan.belegt.size() == 1, "nichts geändert");

    // Geändert (neue Prüfsumme) und umbenannt; „Eigener Drucker“ fehlt jetzt im Portal (gehört nicht uns → bleibt)
    auto portal2 = portal;
    portal2[0].sha1 = "ffffffffffffffffffffffffffffffffffffffff";
    portal2[1].name = "0.16mm 3 Eulen @A1";
    portal2.erase(portal2.begin() + 2);
    plan = profil_plan(portal2, verz, datei_hash);
    pruefe(plan.geaendert == 2 && plan.neu == 0 && plan.entfernen.size() == 1 && plan.entfernen[0].datei == "process/0.20mm 3 Eulen @A1.json", "geändert und umbenannt");
    ausfuehren(plan);
    pruefe(dateien.count("process/0.16mm 3 Eulen @A1.json") && !dateien.count("process/0.20mm 3 Eulen @A1.json") && dateien.count("machine/Eigener Drucker.json"),
           "Dateien nach Umbenennen, eigenes Profil bleibt");

    // Nur Schreibweise geändert (Windows: dieselbe Datei) → nicht „belegt“, sondern neu schreiben
    auto portal3 = portal2;
    portal3[1].name = "0.16MM 3 Eulen @A1";
    plan = profil_plan(portal3, verz, datei_hash);
    pruefe(plan.belegt.empty() && plan.laden.size() == 1 && plan.entfernen.size() == 1, "Schreibweise geändert");
    ausfuehren(plan);
    pruefe(dateien.count("process/0.16MM 3 Eulen @A1.json") && dateien.size() == 3, "nach Schreibweise: eine Datei");

    // Gelöscht und gleichnamig neu hochgeladen (neue id, gleicher Pfad): erst entfernen, dann laden
    auto portal4 = portal3;
    portal4[0].id   = "ffffffffffffffff";
    portal4[0].sha1 = "3333333333333333333333333333333333333333";
    plan = profil_plan(portal4, verz, datei_hash);
    ausfuehren(plan);
    pruefe(dateien.count("filament/3 Eulen PLA @A1.json") && verz.count("ffffffffffffffff") && !verz.count("aaaaaaaaaaaaaaaa"), "neu hochgeladen, Datei bleibt da");

    // Im Slicer geändert und gespeichert → gehört jetzt der Person: weder überschreiben noch löschen
    dateien["filament/3 Eulen PLA @A1.json"] = "{\"im slicer geaendert\":1}";
    auto portal5 = portal4;
    portal5[0].sha1 = "4444444444444444444444444444444444444444";
    plan = profil_plan(portal5, verz, datei_hash);
    pruefe(plan.freigeben.size() == 1 && plan.laden.empty() && plan.belegt.size() == 1, "lokal geändert: nicht überschreiben");
    ausfuehren(plan);
    plan = profil_plan({portal5[1]}, verz, datei_hash); // im Portal gelöscht
    pruefe(dateien.count("filament/3 Eulen PLA @A1.json") && !verz.count("ffffffffffffffff"), "lokal geänderte Datei bleibt");

    // Datei fehlt lokal (von Hand gelöscht) → neu laden
    dateien.erase("process/0.16MM 3 Eulen @A1.json");
    plan = profil_plan(portal5, verz, datei_hash);
    bool neu_laden = false;
    for (const auto& p : plan.laden)
        neu_laden = neu_laden || p.id == "bbbbbbbbbbbbbbbb";
    pruefe(neu_laden, "fehlende Datei neu laden");

    std::printf("%d ok, %d fehlgeschlagen\n", g_ok, g_fehler);
    return g_fehler == 0 ? 0 : 1;
}
