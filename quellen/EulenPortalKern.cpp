// EulenSlicer – Verbindung zum 3 Eulen Portal: Kern ohne Oberfläche.
// Lizenz: AGPL-3.0 wie OrcaSlicer.
#include "EulenPortalKern.hpp"

#include <algorithm>
#include <cctype>
#include <set>

#include <nlohmann/json.hpp>

namespace Slic3r {
namespace Eulen {

namespace {

using json = nlohmann::json;

std::string klein(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool beginnt_mit(const std::string& s, const std::string& anfang) { return s.size() >= anfang.size() && s.compare(0, anfang.size(), anfang) == 0; }

// Ursprung und Rest einer Adresse („https://host“, „/pfad?x“). false bei ungültiger Adresse.
bool zerlegen(const std::string& url, std::string& ursprung_out, std::string& rest)
{
    const auto trenner = url.find("://");
    if (trenner == std::string::npos)
        return false;
    const std::string schema = klein(url.substr(0, trenner));
    if (schema != "http" && schema != "https")
        return false;
    const auto start = trenner + 3;
    auto ende        = url.find_first_of("/?#", start);
    if (ende == std::string::npos)
        ende = url.size();
    std::string host = klein(url.substr(start, ende - start));
    if (host.empty() || host.find('@') != std::string::npos)
        return false;
    if (schema == "https" && host.size() > 4 && host.compare(host.size() - 4, 4, ":443") == 0)
        host.resize(host.size() - 4);
    if (schema == "http" && host.size() > 3 && host.compare(host.size() - 3, 3, ":80") == 0)
        host.resize(host.size() - 3);
    ursprung_out = schema + "://" + host;
    rest         = url.substr(ende);
    if (rest.empty())
        rest = "/";
    return true;
}

bool ist_hex(const std::string& s, size_t laenge)
{
    return s.size() == laenge && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) || (c >= 'a' && c <= 'f'); });
}

bool ist_id(const std::string& s)
{
    return s.size() == 16 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) || (c >= 'a' && c <= 'z'); });
}

bool ist_art(const std::string& s) { return s == "machine" || s == "filament" || s == "process"; }

std::string text(const json& j, const char* key)
{
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

} // namespace

std::string url_kodieren(const std::string& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string mit_param(const std::string& basis, const Parameter& param)
{
    if (param.empty())
        return basis;
    std::string url = basis;
    if (!url.empty() && url.back() != '?' && url.back() != '&')
        url += url.find('?') == std::string::npos ? '?' : '&';
    bool erstes = true;
    for (const auto& [name, wert] : param) {
        if (!erstes)
            url += '&';
        erstes = false;
        url += url_kodieren(name) + "=" + url_kodieren(wert);
    }
    return url;
}

std::string ursprung(const std::string& url)
{
    std::string o, rest;
    return zerlegen(url, o, rest) ? o : std::string();
}

bool unter_adresse(const std::string& basis, const std::string& url)
{
    for (unsigned char c : url)
        if (c <= 0x20 || c == 0x7f || c == '\\')
            return false;
    std::string o1, r1, o2, r2;
    if (basis.empty() || !zerlegen(basis, o1, r1) || !zerlegen(url, o2, r2) || o1 != o2)
        return false;
    if (!beginnt_mit(r2, r1))
        return false;
    const std::string danach = klein(r2.substr(r1.size()));
    return danach.find("..") == std::string::npos && danach.find("%2e") == std::string::npos;
}

std::string seiten_basis(const std::string& api) { return api.empty() ? std::string() : api + "slicer/seite/"; }

std::string api_basis(const std::string& api) { return api.empty() ? std::string() : api + "slicer/api/"; }

std::string seiten_url(const std::string& api, const std::string& seite, const Parameter& param)
{
    return api.empty() ? std::string() : mit_param(seiten_basis(api) + seite, param);
}

bool ist_portal_seite(const std::string& api, const std::string& url) { return !api.empty() && unter_adresse(seiten_basis(api), url); }

bool ist_portal_adresse(const std::string& api, const std::string& url) { return !api.empty() && unter_adresse(api, url); }

bool ist_http(const std::string& url)
{
    const std::string u = klein(url);
    return beginnt_mit(u, "https://") || beginnt_mit(u, "http://");
}

std::string fehlertext(const std::string& body, unsigned status, const std::string& fehler)
{
    try {
        const json j = json::parse(body);
        if (j.is_object()) {
            const std::string meldung = text(j, "message");
            if (!meldung.empty())
                return meldung;
        }
    } catch (...) {
        // kein JSON
    }
    if (!fehler.empty())
        return status != 0 ? fehler + " (HTTP " + std::to_string(status) + ")" : fehler;
    if (status != 0)
        return "Das Portal antwortet mit HTTP " + std::to_string(status) + ".";
    return "Keine Verbindung zum Portal.";
}

std::string profil_dateiname(const std::string& name)
{
    std::string out;
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7f || std::string("\\/:*?\"<>|").find(static_cast<char>(c)) != std::string::npos)
            continue;
        out += static_cast<char>(c);
    }
    const auto anfang = out.find_first_not_of(" .");
    if (anfang == std::string::npos)
        return std::string();
    out = out.substr(anfang);
    while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    if (out.size() > 120) {
        size_t n = 120;
        while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0) == 0x80)
            --n; // nicht mitten in einem UTF-8-Zeichen abschneiden
        out.resize(n);
        while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
            out.pop_back();
    }
    std::string stamm = klein(out.substr(0, out.find('.')));
    static const std::set<std::string> reserviert = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7",
                                                     "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    if (reserviert.count(stamm))
        out = "_" + out;
    return out;
}

std::vector<Profil> profile_lesen(const std::string& json_text)
{
    std::vector<Profil> out;
    try {
        const json liste = json::parse(json_text);
        if (!liste.is_array())
            return out;
        for (const auto& e : liste) {
            if (!e.is_object())
                continue;
            Profil p;
            p.id   = text(e, "id");
            p.art  = text(e, "art");
            p.name = text(e, "name");
            p.sha1 = text(e, "sha1");
            if (ist_id(p.id) && ist_art(p.art) && !profil_dateiname(p.name).empty() && ist_hex(p.sha1, 40))
                out.push_back(p);
        }
    } catch (...) {
        out.clear();
    }
    return out;
}

std::map<std::string, Profil> verzeichnis_lesen(const std::string& json_text)
{
    std::map<std::string, Profil> out;
    try {
        const json j = json::parse(json_text);
        const auto it = j.find("profile");
        if (it == j.end() || !it->is_object())
            return out;
        for (const auto& [id, e] : it->items()) {
            if (!ist_id(id) || !e.is_object())
                continue;
            Profil p;
            p.id    = id;
            p.art   = text(e, "art");
            p.name  = text(e, "name");
            p.sha1  = text(e, "sha1");
            p.datei = text(e, "datei");
            if (ist_art(p.art) && !p.datei.empty() && p.datei.find("..") == std::string::npos && beginnt_mit(p.datei, p.art + "/"))
                out[id] = p;
        }
    } catch (...) {
        out.clear();
    }
    return out;
}

std::string verzeichnis_schreiben(const std::map<std::string, Profil>& verzeichnis)
{
    json profile = json::object();
    for (const auto& [id, p] : verzeichnis)
        profile[id] = {{"art", p.art}, {"name", p.name}, {"sha1", p.sha1}, {"datei", p.datei}};
    json j = {{"version", 1}, {"hinweis", "Profile aus dem 3 Eulen Portal – der EulenSlicer ändert und löscht nur diese."}, {"profile", profile}};
    return j.dump(1, '\t');
}

ProfilPlan profil_plan(const std::vector<Profil>& portal, const std::map<std::string, Profil>& verzeichnis,
                       const std::function<bool(const std::string&)>& datei_da)
{
    ProfilPlan plan;
    std::set<std::string> eigene; // Dateien, die uns (dem Portal) gehören
    for (const auto& [id, v] : verzeichnis)
        eigene.insert(v.datei);
    std::set<std::string> vergeben;
    std::set<std::string> im_portal;

    for (Profil p : portal) {
        p.datei = p.art + "/" + profil_dateiname(p.name) + ".json";
        if (vergeben.count(p.datei))
            continue; // zwei Namen ergeben dieselbe Datei – der erste gewinnt
        vergeben.insert(p.datei);
        im_portal.insert(p.id);
        const auto alt = verzeichnis.find(p.id);
        const bool fremd = datei_da(p.datei) && !eigene.count(p.datei);
        if (alt == verzeichnis.end()) {
            if (fremd) {
                plan.belegt.push_back(p);
                continue;
            }
            plan.laden.push_back(p);
            ++plan.neu;
            continue;
        }
        const Profil& v = alt->second;
        if (v.sha1 == p.sha1 && v.datei == p.datei && datei_da(p.datei))
            continue;
        if (v.datei != p.datei) {
            if (fremd) {
                plan.belegt.push_back(p);
                continue;
            }
            plan.entfernen.push_back(v);
        }
        plan.laden.push_back(p);
        ++plan.geaendert;
    }
    for (const auto& [id, v] : verzeichnis)
        if (!im_portal.count(id))
            plan.entfernen.push_back(v);
    return plan;
}

} // namespace Eulen
} // namespace Slic3r
