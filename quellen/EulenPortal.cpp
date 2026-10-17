// EulenSlicer – Verbindung zum 3 Eulen Portal (Oberfläche).
// Lizenz: AGPL-3.0 wie OrcaSlicer.
#include "EulenPortal.hpp"
#include "EulenPortalKern.hpp"
#include "EulenUpdater.hpp"

#include <algorithm>
#include <cctype>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <map>
#include <thread>
#include <utility>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>

#include <wx/dialog.h>
#include <wx/menu.h>
#include <wx/secretstore.h>
#include <wx/sizer.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <wx/weakref.h>
#include <wx/webview.h>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "NotificationManager.hpp"
#include "PartPlate.hpp"
#include "Plater.hpp"
#include "WebViewDialog.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "Widgets/WebView.hpp"
#include "wxExtensions.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"

namespace Slic3r {
namespace GUI {

namespace {

using json = nlohmann::json;
namespace kern = ::Slic3r::Eulen;
namespace fs   = boost::filesystem;

wxString utf8(const std::string& s) { return wxString::FromUTF8(s.c_str()); }

std::string api() { return EULEN_PORTAL_API; }

fs::path pfad(const std::string& s) { return into_path(utf8(s)); }

void melden(const std::string& text, long stil = wxOK | wxICON_INFORMATION)
{
    MessageDialog dlg(wxGetApp().mainframe, utf8(text), utf8("EulenSlicer"), stil);
    dlg.ShowModal();
}

// Kurzer Hinweis unten in der 3D-Ansicht.
void hinweis(const std::string& text)
{
    if (Plater* plater = wxGetApp().plater())
        if (NotificationManager* nm = plater->get_notification_manager())
            nm->push_notification(text);
}

/*
 * Konto und Werkstatt-Code (Windows-Anmeldeinformationsverwaltung)
 * ----------------------------------------------------------------------- */

struct Konto
{
    std::string login, passwort, name;
};

struct Werkstatt
{
    std::string code, name, email, klasse; // Name, E-Mail und Klasse merkt der Slicer auf Wunsch
};

struct Ablage
{
    std::string id, schluessel, name;
    fs::path    datei;
};

struct Zustand
{
    bool                     geladen = false;
    std::optional<Konto>     konto;
    std::optional<Werkstatt> werkstatt;
    std::optional<Ablage>    ablage;
};

const char* DIENST_KONTO     = "EulenSlicer/Portal";
const char* DIENST_WERKSTATT = "EulenSlicer/Werkstatt";

std::optional<std::pair<std::string, std::string>> geheim_lesen(const char* dienst)
{
    wxSecretStore store = wxSecretStore::GetDefault();
    if (!store.IsOk())
        return std::nullopt;
    wxString      benutzer;
    wxSecretValue wert;
    if (!store.Load(wxString(dienst), benutzer, wert) || !wert.IsOk())
        return std::nullopt;
    std::string text(static_cast<const char*>(wert.GetData()), wert.GetSize());
    if (benutzer.empty() || text.empty())
        return std::nullopt;
    return std::make_pair(into_u8(benutzer), text);
}

bool geheim_speichern(const char* dienst, const std::string& benutzer, const std::string& wert)
{
    wxSecretStore store = wxSecretStore::GetDefault();
    if (!store.IsOk()) {
        BOOST_LOG_TRIVIAL(error) << "EulenSlicer: Anmeldeinformationsverwaltung nicht verfügbar";
        return false;
    }
    return store.Save(wxString(dienst), utf8(benutzer), wxSecretValue(utf8(wert)));
}

void geheim_loeschen(const char* dienst)
{
    wxSecretStore store = wxSecretStore::GetDefault();
    if (store.IsOk())
        store.Delete(wxString(dienst));
}

std::string config(const char* schluessel)
{
    AppConfig* c = wxGetApp().app_config;
    return c != nullptr ? c->get(schluessel) : std::string();
}

void config_setzen(const char* schluessel, const std::string& wert)
{
    if (AppConfig* c = wxGetApp().app_config)
        c->set(schluessel, wert);
}

Zustand& zustand()
{
    static Zustand z;
    if (!z.geladen) {
        z.geladen = true;
        if (auto k = geheim_lesen(DIENST_KONTO))
            z.konto = Konto{k->first, k->second, config("eulen_konto_name")};
        if (auto w = geheim_lesen(DIENST_WERKSTATT))
            z.werkstatt = Werkstatt{w->second, config("eulen_werkstatt_name"), config("eulen_werkstatt_email"), config("eulen_werkstatt_klasse")};
    }
    return z;
}

bool angemeldet() { return zustand().konto.has_value() || zustand().werkstatt.has_value(); }

using Kopf = std::vector<std::pair<std::string, std::string>>;

Kopf zugang_kopf()
{
    Kopf      kopf;
    Zustand&  z = zustand();
    if (z.konto)
        kopf.emplace_back("X-Api-Key", z.konto->login + ":" + z.konto->passwort);
    else if (z.werkstatt)
        kopf.emplace_back("X-Eulen-Werkstatt", z.werkstatt->code);
    return kopf;
}

/*
 * HTTP
 * ----------------------------------------------------------------------- */

struct Antwort
{
    unsigned    status = 0;
    std::string body;
    std::string fehler;
    bool        ok() const { return fehler.empty() && status >= 200 && status < 300; }
};

Antwort anfrage(const std::string& methode, const std::string& url, const Kopf& kopf, const std::string& body = std::string(),
                const std::string& typ = "application/json", long max_sekunden = 120, long verbinden = 15,
                std::function<void(size_t, size_t, bool&)> fortschritt = nullptr)
{
    Antwort a;
    Http    http = methode == "GET" ? Http::get(url) : (methode == "DELETE" ? Http::del(url) : Http::post(url));
    // Keine Weiterleitungen: curl schickte X-Api-Key sonst auch an das neue Ziel.
    http.tls_verify(url.rfind("https://", 0) == 0).follow_redirects(false).timeout_connect(verbinden).timeout_max(max_sekunden);
    for (const auto& [name, wert] : kopf)
        http.header(name, wert);
    http.header("Accept", "application/json");
    if (methode == "POST") {
        http.header("Content-Type", typ);
        http.set_post_body(body.empty() ? std::string("{}") : body);
    }
    if (fortschritt)
        http.on_progress([fortschritt](Http::Progress p, bool& abbrechen) { fortschritt(p.ulnow, p.ultotal, abbrechen); });
    http.on_complete([&a](std::string b, unsigned s) {
            a.status = s;
            a.body   = std::move(b);
        })
        .on_error([&a](std::string b, std::string e, unsigned s) {
            a.status = s;
            a.body   = std::move(b);
            a.fehler = e.empty() ? "HTTP " + std::to_string(s) : e;
        })
        .perform_sync();
    return a;
}

std::string fehler_von(const Antwort& a)
{
    const bool nur_status = a.fehler.rfind("HTTP ", 0) == 0;
    return kern::fehlertext(a.body, a.status, nur_status ? std::string() : a.fehler);
}

json json_lesen(const std::string& text)
{
    try {
        return json::parse(text);
    } catch (...) {
        return json();
    }
}

std::string text_von(const json& j, const char* schluessel)
{
    if (!j.is_object())
        return std::string();
    const auto it = j.find(schluessel);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

/*
 * Seiten im Browser
 * ----------------------------------------------------------------------- */

void skript(wxWebView* browser, const std::string& js)
{
    if (browser == nullptr)
        return;
    wxWeakRef<wxWebView> ref(browser);
    wxGetApp().CallAfter([ref, js]() {
        if (ref)
            WebView::RunScript(ref.get(), utf8(js));
    });
}

// Browser, die auf eine Antwort aus einem Hintergrundfaden warten. Nur im Hauptfaden
// benutzen: wxWeakRef ist nicht threadsicher, darum gehen Fäden nur mit der Nummer um.
std::map<int, wxWeakRef<wxWebView>> g_wartend;
int                                 g_wartend_nr = 0;

int wartend_merken(wxWebView* browser)
{
    g_wartend[++g_wartend_nr] = browser;
    return g_wartend_nr;
}

wxWebView* wartend_holen(int nr)
{
    const auto it = g_wartend.find(nr);
    if (it == g_wartend.end())
        return nullptr;
    wxWebView* browser = it->second.get();
    g_wartend.erase(it);
    return browser;
}

void antworten(wxWebView* browser, const json& anfrage_json, const json& daten, const std::string& fehler = std::string())
{
    json r           = json::object();
    r["sequence_id"] = anfrage_json.contains("sequence_id") ? anfrage_json["sequence_id"] : json("");
    r["command"]     = text_von(anfrage_json, "command");
    r["data"]        = daten;
    if (!fehler.empty()) {
        r["ok"]      = false;
        r["message"] = fehler;
    }
    skript(browser, "window.postMessage(" + r.dump(-1, ' ', false, json::error_handler_t::replace) + ")");
}

kern::Parameter seiten_param(int auftrag = 0)
{
    kern::Parameter p;
    if (wxGetApp().dark_mode())
        p.emplace_back("dunkel", "1");
    if (auftrag > 0)
        p.emplace_back("auftrag", std::to_string(auftrag));
    return p;
}

// Reiter „Start“
wxWeakRef<wxWebView> g_start;
wxString             g_start_lokal;

// Die Startseite soll neu nachsehen (Anmeldung geändert, Auftrag gesendet).
void start_neu_laden()
{
    if (g_start && kern::ist_portal_seite(api(), into_u8(g_start->GetCurrentURL())))
        skript(g_start.get(), "window.postMessage({\"command\":\"eulen_neu_laden\"})");
}

std::string fehlerseite()
{
    return "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\"><title>EulenSlicer</title></head>"
           "<body style=\"font-family:'Segoe UI',system-ui,sans-serif;background:#F8FAFC;color:#1E2B38;margin:0;padding:3rem 2rem;\">"
           "<h1 style=\"font-size:1.4rem;\">Das Portal ist gerade nicht erreichbar</h1>"
           "<p style=\"max-width:40rem;line-height:1.5;\">Prüfe die Internetverbindung und öffne das Fenster gleich noch einmal "
           "(Menü „3 Eulen“). Ohne Portal kannst du weiter slicen und direkt im LAN drucken.</p></body></html>";
}

// Fenster mit einer Seite des Portals (nicht modal, eines zur Zeit).
class EulenFenster : public wxDialog
{
public:
    EulenFenster(wxWindow* parent, const std::string& url, int auftrag)
        : wxDialog(parent, wxID_ANY, utf8("EulenSlicer"), wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX)
        , m_auftrag(auftrag)
    {
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        m_browser   = WebView::CreateWebView(this, utf8(url));
        if (m_browser != nullptr) {
            sizer->Add(m_browser, 1, wxEXPAND);
            Bind(wxEVT_WEBVIEW_NAVIGATING, &EulenFenster::navigation, this, m_browser->GetId());
            Bind(wxEVT_WEBVIEW_NEWWINDOW, &EulenFenster::neues_fenster, this, m_browser->GetId());
            Bind(wxEVT_WEBVIEW_TITLE_CHANGED, &EulenFenster::titel, this, m_browser->GetId());
            Bind(wxEVT_WEBVIEW_ERROR, &EulenFenster::ladefehler, this, m_browser->GetId());
            Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &EulenFenster::nachricht, this, m_browser->GetId());
        }
        SetSizer(sizer);
        SetMinSize(FromDIP(wxSize(520, 460)));
        SetSize(FromDIP(wxSize(1000, 780)));
        CenterOnParent();
        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { Destroy(); });
    }

    void laden(const std::string& url, int auftrag)
    {
        m_auftrag = auftrag;
        if (m_browser != nullptr)
            WebView::LoadUrl(m_browser, utf8(url));
    }

    int auftrag() const { return m_auftrag; }

private:
    void navigation(wxWebViewEvent& evt)
    {
        const std::string ziel = into_u8(evt.GetURL());
        if (kern::ist_portal_seite(api(), ziel) || ziel.rfind("data:", 0) == 0 || ziel == "about:blank")
            return;
        evt.Veto();
        if (kern::ist_http(ziel))
            wxLaunchDefaultBrowser(utf8(ziel));
    }

    void neues_fenster(wxWebViewEvent& evt)
    {
        const std::string ziel = into_u8(evt.GetURL());
        if (kern::ist_http(ziel))
            wxLaunchDefaultBrowser(utf8(ziel));
    }

    void titel(wxWebViewEvent& evt) { SetTitle(evt.GetString()); }

    void ladefehler(wxWebViewEvent& evt)
    {
        if (evt.GetInt() == wxWEBVIEW_NAV_ERR_USER_CANCELLED || !kern::ist_portal_seite(api(), into_u8(evt.GetURL())))
            return;
        BOOST_LOG_TRIVIAL(warning) << "EulenSlicer: Seite nicht erreichbar: " << into_u8(evt.GetURL());
        if (m_browser != nullptr)
            m_browser->SetPage(utf8(fehlerseite()), wxEmptyString);
    }

    void nachricht(wxWebViewEvent& evt) { eulen_web_nachricht(m_browser, evt.GetString(), this); }

    wxWebView* m_browser = nullptr;
    int        m_auftrag = 0;
};

wxWeakRef<EulenFenster> g_fenster;

/*
 * Senden: speichern, in Stücken ablegen
 * ----------------------------------------------------------------------- */

std::atomic<bool> g_sendet{false};

// Datei in Stücken im Portal ablegen (läuft im Hintergrund, das Fenster zeigt den Fortschritt).
std::optional<Ablage> hochladen(const fs::path& datei, const std::string& name, std::string& fehler)
{
    boost::system::error_code ec;
    const std::uintmax_t      groesse = fs::file_size(datei, ec);
    if (ec || groesse == 0) {
        fehler = "Die gespeicherte Datei ist leer.";
        return std::nullopt;
    }

    struct Lauf
    {
        std::mutex            sperre;
        std::atomic<int>      prozent{0};
        std::atomic<bool>     fertig{false};
        std::atomic<bool>     abbrechen{false};
        std::string           fehler;
        std::optional<Ablage> ergebnis;
    };
    auto lauf = std::make_shared<Lauf>();

    const std::string basis = kern::api_basis(api());
    const Kopf        kopf  = zugang_kopf();
    std::thread arbeiter([lauf, basis, kopf, datei, name, groesse]() {
      try {
        auto ende = [&lauf](const std::string& f) {
            {
                std::lock_guard<std::mutex> g(lauf->sperre);
                lauf->fehler = f;
            }
            lauf->fertig = true;
        };
        Antwort a = anfrage("POST", basis + "ablage", kopf, json{{"name", name}, {"groesse", groesse}}.dump());
        if (!a.ok())
            return ende(fehler_von(a));
        const json        start      = json_lesen(a.body);
        const std::string id         = text_von(start, "id");
        const std::string schluessel = text_von(start, "schluessel");
        std::size_t       teil       = 4u * 1024 * 1024;
        if (start.is_object() && start.contains("teil_max") && start["teil_max"].is_number_integer())
            teil = static_cast<std::size_t>(std::clamp<long long>(start["teil_max"].get<long long>(), 64 * 1024, 8 * 1024 * 1024));
        if (id.empty() || schluessel.empty())
            return ende("Das Portal hat die Ablage nicht angelegt (unerwartete Antwort).");

        Kopf mit = kopf;
        mit.emplace_back("X-Eulen-Ablage", schluessel);
        fs::ifstream in(datei, std::ios::binary);
        std::string  stueck;
        std::uintmax_t gesendet = 0;
        int            nr       = 0;
        while (gesendet < groesse) {
            const std::size_t n = static_cast<std::size_t>(std::min<std::uintmax_t>(teil, groesse - gesendet));
            stueck.resize(n);
            in.read(&stueck[0], static_cast<std::streamsize>(n));
            if (static_cast<std::size_t>(in.gcount()) != n)
                return ende("Die gespeicherte Datei ließ sich nicht lesen.");
            Antwort t;
            for (int versuch = 0; versuch < 3; ++versuch) {
                t = anfrage("POST", kern::mit_param(basis + "ablage/" + id + "/teil", {{"nr", std::to_string(nr)}}), mit, stueck,
                            "application/octet-stream", 600, 15, [&](size_t jetzt, size_t, bool& abbrechen) {
                                lauf->prozent = static_cast<int>((gesendet + jetzt) * 95 / groesse);
                                abbrechen     = lauf->abbrechen.load();
                            });
                if (t.ok() || t.status != 0 || lauf->abbrechen)
                    break; // nur Verbindungsfehler noch einmal versuchen
            }
            if (lauf->abbrechen) {
                anfrage("DELETE", basis + "ablage/" + id, mit);
                return ende(std::string());
            }
            if (!t.ok())
                return ende(fehler_von(t));
            gesendet += n;
            ++nr;
            lauf->prozent = static_cast<int>(gesendet * 95 / groesse);
        }
        Antwort f = anfrage("POST", basis + "ablage/" + id + "/fertig", mit, "{}", "application/json", 300);
        if (lauf->abbrechen) {
            anfrage("DELETE", basis + "ablage/" + id, mit);
            return ende(std::string());
        }
        if (!f.ok())
            return ende(fehler_von(f));
        {
            std::lock_guard<std::mutex> g(lauf->sperre);
            lauf->ergebnis = Ablage{id, schluessel, name, datei};
        }
        lauf->prozent = 100;
        lauf->fertig  = true;
      } catch (const std::exception& ex) {
        {
            std::lock_guard<std::mutex> g(lauf->sperre);
            lauf->fehler = std::string("Fehler beim Senden: ") + ex.what();
        }
        lauf->fertig = true;
      }
    });

    {
        ProgressDialog dlg(utf8("An 3 Eulen senden"), utf8("„" + name + "“ geht ans Portal …"), 100, wxGetApp().mainframe,
                           wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_CAN_ABORT);
        while (!lauf->fertig) {
            wxMilliSleep(50);
            if (!dlg.Update(std::clamp(lauf->prozent.load(), 0, 99)))
                lauf->abbrechen = true;
        }
        dlg.Update(100);
    }
    if (arbeiter.joinable())
        arbeiter.join();

    std::lock_guard<std::mutex> g(lauf->sperre);
    if (lauf->abbrechen && !lauf->ergebnis) {
        fehler.clear();
        return std::nullopt;
    }
    fehler = lauf->fehler;
    return lauf->ergebnis;
}

/*
 * Profile
 * ----------------------------------------------------------------------- */

struct ProfilErgebnis
{
    int         neu = 0, geaendert = 0, entfernt = 0, belegt = 0;
    std::string fehler;
    // Drucker aus dem Portal: Modell von OrcaSlicer („Bambu Lab P1S“) und Düse („0.4“).
    std::vector<std::pair<std::string, std::string>> drucker;
    std::string drucker_text; // was eingerichtet wurde (leer = nichts Neues)
};

std::atomic<bool> g_profile_laeuft{false};

std::string datei_lesen(const fs::path& p)
{
    fs::ifstream in(p, std::ios::binary);
    if (!in)
        return std::string();
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool datei_schreiben(const fs::path& p, const std::string& inhalt)
{
    boost::system::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.parent_path() / (p.filename().wstring() + L".eulen-neu");
    {
        fs::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(inhalt.data(), static_cast<std::streamsize>(inhalt.size()));
        if (!out.good())
            return false;
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

// Neue Profile gleich laden; geänderte und entfernte wirken erst nach dem Neustart
// (OrcaSlicer lädt ein schon geladenes Profil nicht noch einmal).
void presets_neu_laden(const std::string& user, bool neustart_noetig)
{
    PresetBundle* pb = wxGetApp().preset_bundle;
    if (pb == nullptr || wxGetApp().mainframe == nullptr)
        return;
    if (pb->prints.current_is_dirty() || pb->filaments.current_is_dirty() || pb->printers.current_is_dirty()) {
        hinweis("Neue Profile aus dem Portal sind da – sie erscheinen nach dem nächsten Start (gerade sind Änderungen an Profilen offen).");
        return;
    }
    try {
        pb->load_user_presets(user, ForwardCompatibilitySubstitutionRule::Enable);
        wxGetApp().mainframe->update_side_preset_ui();
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "EulenSlicer: Profile neu laden: " << ex.what();
    }
    if (neustart_noetig)
        hinweis("Profile aus dem Portal wurden geändert – sie gelten nach dem nächsten Start des EulenSlicers.");
}

// Antwort von GET /slicer/api/drucker: nur Modelle von Bambu Lab mit bekannter Düse.
std::vector<std::pair<std::string, std::string>> drucker_lesen(const std::string& body)
{
    std::vector<std::pair<std::string, std::string>> out;
    const json j = json_lesen(body);
    if (!j.is_object() || !j.contains("drucker") || !j["drucker"].is_array())
        return out;
    static const std::set<std::string> duesen = {"0.2", "0.4", "0.6", "0.8"};
    for (const auto& d : j["drucker"]) {
        const std::string modell = text_von(d, "modell");
        const std::string duese  = text_von(d, "duese");
        const bool        sauber = modell.size() < 40 && modell.rfind("Bambu Lab ", 0) == 0 &&
                            std::all_of(modell.begin(), modell.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == ' '; });
        if (sauber && duesen.count(duese) && std::find(out.begin(), out.end(), std::make_pair(modell, duese)) == out.end())
            out.emplace_back(modell, duese);
    }
    return out;
}

// Drucker der Werkstatt wie mit dem Einrichtungsassistenten hinzufügen (Hersteller „BBL“,
// Modell, Düse). Gibt true zurück, wenn die Profile dabei neu geladen wurden. Was schon
// eingerichtet ist, bleibt; entfernt wird nichts.
bool drucker_anwenden(const std::vector<std::pair<std::string, std::string>>& drucker, std::string& text)
{
    GUI_App& app = wxGetApp();
    if (drucker.empty() || app.preset_bundle == nullptr || app.app_config == nullptr || app.mainframe == nullptr)
        return false;
    if (!fs::exists(fs::path(resources_dir()) / "profiles" / "BBL.json")) {
        hinweis("Die Profile von Bambu Lab fehlen in dieser Installation (resources\\profiles\\BBL.json) – bitte den EulenSlicer neu installieren.");
        return false;
    }
    const AppConfig::VendorMap& vorhanden = app.app_config->vendors();
    AppConfig::VendorMap        neu;
    std::vector<std::string>    namen;
    for (const auto& [modell, duese] : drucker) {
        const auto v  = vorhanden.find("BBL");
        const bool da = v != vorhanden.end() && v->second.count(modell) && v->second.at(modell).count(duese);
        if (!da) {
            neu["BBL"][modell].insert(duese);
            namen.push_back(modell + " (" + duese + " mm)");
        }
    }
    if (neu.empty())
        return false;
    PresetBundle* pb = app.preset_bundle;
    if (pb->prints.current_is_dirty() || pb->filaments.current_is_dirty() || pb->printers.current_is_dirty()) {
        hinweis("Drucker aus dem Portal werden beim nächsten Start eingerichtet (gerade sind Änderungen an Profilen offen).");
        return false;
    }
    // Ist noch kein richtiger Drucker eingerichtet oder kommen die Bambu-Drucker zum ersten
    // Mal dazu, gleich den ersten aus dem Portal wählen.
    const auto  bbl      = vorhanden.find("BBL");
    const bool  erstmals = bbl == vorhanden.end() || bbl->second.empty();
    std::string modell, variante;
    if (erstmals || pb->printers.only_default_printers()) {
        modell   = drucker.front().first;
        variante = drucker.front().second;
    }
    try {
        if (!pb->apply_vendor_config(neu, {}, app.app_config, false, modell, variante)) {
            hinweis("Die Drucker aus dem Portal ließen sich nicht einrichten (Profile von Bambu Lab nicht gefunden).");
            return false;
        }
        app.load_current_presets();
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "EulenSlicer: Drucker einrichten: " << ex.what();
        hinweis(std::string("Die Drucker aus dem Portal ließen sich nicht einrichten: ") + ex.what());
        return false;
    }
    text = "Drucker aus dem Portal eingerichtet: ";
    for (size_t i = 0; i < namen.size(); ++i)
        text += (i ? ", " : "") + namen[i];
    text += ".";
    return true;
}

// Abgleich im Hintergrund; fertig läuft danach im Hauptfaden.
void profile_abgleichen(std::function<void(const ProfilErgebnis&)> fertig)
{
    if (!eulen_portal_aktiv() || !angemeldet()) {
        ProfilErgebnis e;
        e.fehler = "Erst mit dem Portal verbinden (Menü „3 Eulen“ → „Anmelden“).";
        fertig(e);
        return;
    }
    if (g_profile_laeuft.exchange(true)) {
        ProfilErgebnis e;
        e.fehler = "Der Abgleich läuft schon.";
        fertig(e);
        return;
    }
    std::string user = DEFAULT_USER_FOLDER_NAME;
    if (wxGetApp().getAgent() != nullptr && wxGetApp().is_user_login())
        user = wxGetApp().getAgent()->get_user_id();
    const fs::path    ordner = pfad(data_dir()) / PRESET_USER_DIR / pfad(user);
    const Kopf        kopf   = zugang_kopf();
    const std::string basis  = kern::api_basis(api());

    std::thread([fertig, user, ordner, kopf, basis]() {
        ProfilErgebnis e;
        try {
            // Erst die Drucker der Werkstatt (Modul ab 1.3.0; ältere antworten mit 404 – dann ohne).
            Antwort dr = anfrage("GET", basis + "drucker", kopf);
            if (dr.ok())
                e.drucker = drucker_lesen(dr.body);
            Antwort a = anfrage("GET", basis + "profile", kopf);
            if (!a.ok()) {
                e.fehler = fehler_von(a);
            } else {
                const auto     portal     = kern::profile_lesen(a.body);
                const fs::path verz_datei = ordner / "eulen_profile.json";
                auto           verz       = kern::verzeichnis_lesen(datei_lesen(verz_datei));
                const auto     plan       = kern::profil_plan(portal, verz, [&ordner](const std::string& rel) {
                    boost::system::error_code ec;
                    const fs::path            p = ordner / pfad(rel);
                    return fs::exists(p, ec) ? kern::inhalt_hash(datei_lesen(p)) : std::string();
                });
                std::set<std::string> im_portal;
                for (const auto& p : portal)
                    im_portal.insert(p.id);
                // Im Slicer geänderte Profile gehören jetzt der Person.
                for (const auto& v : plan.freigeben)
                    verz.erase(v.id);
                // Erst entfernen (alte Namen, gelöschte), dann laden – dieselbe Datei kann in beiden stehen.
                for (const auto& v : plan.entfernen) {
                    boost::system::error_code ec;
                    fs::path                  alt = ordner / pfad(v.datei);
                    fs::remove(alt, ec);
                    alt.replace_extension(".info");
                    fs::remove(alt, ec);
                    const auto it = verz.find(v.id);
                    if (it != verz.end() && it->second.datei == v.datei)
                        verz.erase(it);
                    if (!im_portal.count(v.id))
                        ++e.entfernt;
                }
                for (kern::Profil p : plan.laden) {
                    Antwort d   = anfrage("GET", basis + "profile/" + p.id, kopf);
                    json    inh = d.ok() ? json_lesen(d.body) : json();
                    if (!inh.is_object() || text_von(inh, "name").empty()) {
                        BOOST_LOG_TRIVIAL(warning) << "EulenSlicer: Profil " << p.name << " nicht geladen: " << (d.ok() ? "kein Profil" : fehler_von(d));
                        if (e.fehler.empty())
                            e.fehler = "Ein Profil ließ sich nicht laden: " + p.name;
                        continue;
                    }
                    // Nachbearbeitungs-Skripte liefen beim Exportieren auf jedem Rechner – nie aus dem Portal.
                    const auto pp = inh.find("post_process");
                    if (pp != inh.end() && !(pp->is_null() || (pp->is_string() && pp->get<std::string>().empty()) ||
                                             (pp->is_array() && std::all_of(pp->begin(), pp->end(), [](const json& x) {
                                                  return x.is_string() && x.get<std::string>().empty();
                                              })))) {
                        BOOST_LOG_TRIVIAL(warning) << "EulenSlicer: Profil " << p.name << " mit post_process abgelehnt.";
                        if (e.fehler.empty())
                            e.fehler = "Profil „" + p.name + "“ enthält Nachbearbeitungs-Skripte und wird nicht übernommen.";
                        continue;
                    }
                    const std::string text = inh.dump(1, '\t');
                    if (!datei_schreiben(ordner / pfad(p.datei), text)) {
                        if (e.fehler.empty())
                            e.fehler = "Ein Profil ließ sich nicht speichern: " + p.name;
                        continue;
                    }
                    p.inhalt   = kern::inhalt_hash(text);
                    verz[p.id] = p;
                }
                e.neu       = plan.neu;
                e.geaendert = plan.geaendert;
                e.belegt    = static_cast<int>(plan.belegt.size());
                datei_schreiben(verz_datei, kern::verzeichnis_schreiben(verz));
            }
        } catch (const std::exception& ex) {
            e.fehler = std::string("Abgleich der Profile fehlgeschlagen: ") + ex.what();
        }
        wxGetApp().CallAfter([fertig, e, user]() mutable {
            g_profile_laeuft = false;
            // Drucker zuerst: Profile aus dem Portal erben oft von den Profilen von Bambu Lab.
            // apply_vendor_config() lädt dabei alle Profile neu, auch die eigenen.
            const bool neu_geladen = drucker_anwenden(e.drucker, e.drucker_text);
            if (!neu_geladen && (e.neu > 0 || e.geaendert + e.entfernt > 0))
                presets_neu_laden(user, e.geaendert + e.entfernt > 0);
            fertig(e);
        });
    }).detach();
}

std::string profil_text(const ProfilErgebnis& e)
{
    std::string t = "Profile aus dem Portal: " + std::to_string(e.neu) + " neu, " + std::to_string(e.geaendert) + " geändert, " +
                    std::to_string(e.entfernt) + " entfernt.";
    if (e.belegt > 0)
        t += " " + std::to_string(e.belegt) + " nicht übernommen, weil es schon ein eigenes Profil mit dem Namen gibt.";
    return t;
}

/*
 * Befehle der Seiten
 * ----------------------------------------------------------------------- */

json info_json(wxWindow* fenster)
{
    Zustand& z       = zustand();
    int      auftrag = 0;
    if (auto* f = dynamic_cast<EulenFenster*>(fenster))
        auftrag = f->auftrag();
    json j       = json::object();
    j["konto"]   = z.konto ? json{{"login", z.konto->login},
                                  {"name", z.konto->name.empty() ? z.konto->login : z.konto->name},
                                  {"schluessel", z.konto->login + ":" + z.konto->passwort}} :
                             json(nullptr);
    j["werkstatt"] = !z.konto && z.werkstatt ? json{{"code", z.werkstatt->code}, {"name", z.werkstatt->name}, {"email", z.werkstatt->email}, {"klasse", z.werkstatt->klasse}} :
                                               json(nullptr);
    j["ablage"]  = z.ablage ? json{{"id", z.ablage->id}, {"schluessel", z.ablage->schluessel}, {"name", z.ablage->name}} : json(nullptr);
    j["rechner"] = into_u8(wxGetHostName());
    j["version"] = EULEN_ORCA_VERSION;
    j["build"]   = EULEN_BUILD;
    j["fenster"] = fenster != nullptr ? "dialog" : "start";
    j["dunkel"]  = wxGetApp().dark_mode();
    j["auftrag"] = auftrag;
    return j;
}

void ablage_wegraeumen()
{
    Zustand& z = zustand();
    if (z.ablage) {
        boost::system::error_code ec;
        fs::remove(z.ablage->datei, ec);
        z.ablage.reset();
    }
}

void befehl_ausfuehren(wxWebView* browser, wxWindow* fenster, const json& m, const std::string& befehl, const json& daten)
{
    Zustand& z = zustand();

    if (befehl == "eulen_info") {
        antworten(browser, m, info_json(fenster));
    } else if (befehl == "eulen_konto_setzen") {
        const std::string login    = text_von(daten, "login");
        const std::string passwort = text_von(daten, "passwort");
        const std::string name     = text_von(daten, "name");
        if (login.empty() || passwort.empty() || login.find(':') != std::string::npos)
            return antworten(browser, m, json::object(), "Die Anmeldung kam unvollständig an.");
        if (!geheim_speichern(DIENST_KONTO, login, passwort))
            return antworten(browser, m, json::object(), "Windows kann den Zugang nicht speichern (Anmeldeinformationsverwaltung).");
        config_setzen("eulen_konto_name", name);
        z.konto = Konto{login, passwort, name};
        geheim_loeschen(DIENST_WERKSTATT);
        z.werkstatt.reset();
        antworten(browser, m, json::object());
        start_neu_laden();
    } else if (befehl == "eulen_werkstatt_setzen") {
        const std::string code = text_von(daten, "code");
        if (!code.empty()) {
            if (!geheim_speichern(DIENST_WERKSTATT, "werkstatt", code))
                return antworten(browser, m, json::object(), "Windows kann den Code nicht speichern (Anmeldeinformationsverwaltung).");
            z.werkstatt = Werkstatt{code, config("eulen_werkstatt_name"), config("eulen_werkstatt_email"), config("eulen_werkstatt_klasse")};
        } else if (z.werkstatt) {
            z.werkstatt->name  = text_von(daten, "name");
            z.werkstatt->email  = text_von(daten, "email");
            z.werkstatt->klasse = text_von(daten, "klasse");
            config_setzen("eulen_werkstatt_name", z.werkstatt->name);
            config_setzen("eulen_werkstatt_email", z.werkstatt->email);
            config_setzen("eulen_werkstatt_klasse", z.werkstatt->klasse);
        }
        antworten(browser, m, json::object());
        start_neu_laden();
    } else if (befehl == "eulen_abmelden") {
        if (text_von(daten, "was") == "werkstatt") {
            geheim_loeschen(DIENST_WERKSTATT);
            config_setzen("eulen_werkstatt_name", "");
            config_setzen("eulen_werkstatt_email", "");
            config_setzen("eulen_werkstatt_klasse", "");
            z.werkstatt.reset();
        } else {
            geheim_loeschen(DIENST_KONTO);
            config_setzen("eulen_konto_name", "");
            z.konto.reset();
        }
        antworten(browser, m, json::object());
        start_neu_laden();
    } else if (befehl == "eulen_browser") {
        const std::string url = text_von(daten, "url");
        if (!kern::ist_http(url))
            return antworten(browser, m, json::object(), "Nur Adressen mit http oder https.");
        wxLaunchDefaultBrowser(utf8(url));
        antworten(browser, m, json::object());
    } else if (befehl == "eulen_seite") {
        const std::string seite = text_von(daten, "seite");
        if (seite != "anmelden" && seite != "senden" && seite != "auftraege" && seite != "start")
            return antworten(browser, m, json::object(), "Diese Seite gibt es nicht.");
        int auftrag = 0;
        if (daten.contains("auftrag") && daten["auftrag"].is_number_integer())
            auftrag = daten["auftrag"].get<int>();
        antworten(browser, m, json::object());
        wxGetApp().CallAfter([seite, auftrag]() { eulen_fenster(seite, auftrag); });
    } else if (befehl == "eulen_senden") {
        antworten(browser, m, json::object());
        wxGetApp().CallAfter([]() { eulen_senden(); });
    } else if (befehl == "eulen_oeffnen") {
        const std::string url  = text_von(daten, "url");
        std::string       name = kern::profil_dateiname(text_von(daten, "name"));
        if (!kern::ist_portal_adresse(api(), url))
            return antworten(browser, m, json::object(), "Nur Dateien aus dem Portal lassen sich hier öffnen.");
        if (name.empty())
            name = "Auftrag.3mf";
        antworten(browser, m, json::object());
        wxGetApp().CallAfter([url, name]() {
            if (MainFrame* frame = wxGetApp().mainframe)
                frame->Raise();
            wxGetApp().request_model_download(utf8(url + "&name=" + name));
        });
    } else if (befehl == "eulen_profile") {
        const int  nr            = wartend_merken(browser);
        const json anfrage_kopie = m;
        profile_abgleichen([nr, anfrage_kopie](const ProfilErgebnis& e) {
            if (!e.drucker_text.empty())
                hinweis(e.drucker_text);
            wxWebView* ziel = wartend_holen(nr);
            if (ziel == nullptr)
                return;
            if (!e.fehler.empty() && e.neu + e.geaendert + e.entfernt == 0)
                antworten(ziel, anfrage_kopie, json::object(), e.fehler);
            else
                antworten(ziel, anfrage_kopie, json{{"neu", e.neu}, {"geaendert", e.geaendert}, {"entfernt", e.entfernt}, {"belegt", e.belegt}, {"drucker", e.drucker_text}});
        });
    } else if (befehl == "eulen_update") {
        antworten(browser, m, json::object());
        wxGetApp().CallAfter([]() { eulen_update_pruefen(true); });
    } else if (befehl == "eulen_fertig") {
        ablage_wegraeumen();
        antworten(browser, m, json::object());
        start_neu_laden();
    } else if (befehl == "eulen_schliessen") {
        antworten(browser, m, json::object());
        if (fenster != nullptr) {
            wxWeakRef<wxWindow> ref(fenster);
            wxGetApp().CallAfter([ref]() {
                if (ref)
                    ref->Close();
            });
        }
    } else {
        antworten(browser, m, json::object(), "Diesen Befehl kennt der EulenSlicer nicht – bitte aktualisieren.");
    }
}

} // namespace

/*
 * Öffentlich
 * ----------------------------------------------------------------------- */

bool eulen_portal_aktiv() { return !api().empty(); }

void eulen_start_einrichten(wxWebView* browser, const wxString& lokal)
{
    g_start       = browser;
    g_start_lokal = lokal;
    if (!eulen_portal_aktiv() || browser == nullptr)
        return;
    // Erst nachsehen, ob die Seite da ist (offline, Modul nicht aktiv) – dann umschalten.
    const std::string url = kern::seiten_url(api(), "start", seiten_param());
    std::thread([url]() {
        Antwort a;
        try {
            a = anfrage("GET", url, Kopf{}, std::string(), std::string(), 15, 6);
        } catch (...) {
            a.fehler = "Ausnahme";
        }
        const bool gut = a.ok() && a.body.find("eulen-konfig") != std::string::npos;
        if (!gut)
            BOOST_LOG_TRIVIAL(warning) << "EulenSlicer: Startseite des Portals nicht erreichbar (" << fehler_von(a) << "), normale Startseite bleibt.";
        wxGetApp().CallAfter([url, gut]() {
            if (gut && g_start)
                WebView::LoadUrl(g_start.get(), utf8(url));
        });
    }).detach();
}

bool eulen_start_navigation(wxWebView* browser, wxWebViewEvent& evt)
{
    if (browser == nullptr || !eulen_portal_aktiv())
        return false;
    const std::string jetzt = into_u8(browser->GetCurrentURL());
    const std::string ziel  = into_u8(evt.GetURL());
    // Normale Startseite: alles wie bei OrcaSlicer.
    if (!kern::ist_portal_seite(api(), jetzt) || kern::ist_portal_seite(api(), ziel))
        return false;
    // Die lokale Startseite (Rückfall) und Dateien behandelt OrcaSlicer.
    if (ziel.rfind("file:", 0) == 0 || ziel.rfind("File:", 0) == 0)
        return false;
    evt.Veto();
    if (kern::ist_http(ziel))
        wxLaunchDefaultBrowser(utf8(ziel));
    return true;
}

bool eulen_start_fehler(wxWebView* browser, wxWebViewEvent& evt)
{
    if (browser == nullptr || g_start_lokal.empty() || !kern::ist_portal_seite(api(), into_u8(evt.GetURL())))
        return false;
    if (evt.GetInt() == wxWEBVIEW_NAV_ERR_USER_CANCELLED)
        return true;
    BOOST_LOG_TRIVIAL(warning) << "EulenSlicer: Startseite des Portals ließ sich nicht laden, normale Startseite.";
    WebView::LoadUrl(browser, g_start_lokal);
    return true;
}

bool eulen_web_nachricht(wxWebView* browser, const wxString& nachricht, wxWindow* fenster)
{
    json m = json_lesen(into_u8(nachricht));
    if (!m.is_object())
        return false;
    const std::string befehl = text_von(m, "command");
    const bool        portal = browser != nullptr && kern::ist_portal_seite(api(), into_u8(browser->GetCurrentURL()));
    if (befehl.rfind("eulen_", 0) != 0) {
        if (!portal)
            return false; // normale Startseite: alles wie bei OrcaSlicer
        // Von der Portal-Seite nur die Befehle der Startseite, die sie braucht – und ohne
        // den Zeitgeber von OrcaSlicer, der Anmeldedaten von OrcaCloud in die Seite schickt.
        static const std::set<std::string> erlaubt = {"get_recent_projects", "homepage_newproject", "homepage_openproject",
                                                      "homepage_open_recentfile", "homepage_delete_all_recentfile"};
        if (erlaubt.count(befehl) && fenster == nullptr)
            wxGetApp().handle_web_request(into_u8(nachricht));
        else
            BOOST_LOG_TRIVIAL(warning) << "EulenSlicer: Befehl " << befehl << " von der Portal-Seite nicht erlaubt.";
        return true;
    }
    if (!portal) {
        BOOST_LOG_TRIVIAL(warning) << "EulenSlicer: Befehl " << befehl << " von einer fremden Seite abgelehnt.";
        return true;
    }
    const json daten = m.contains("data") && m["data"].is_object() ? m["data"] : json::object();
    try {
        befehl_ausfuehren(browser, fenster, m, befehl, daten);
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "EulenSlicer: Befehl " << befehl << " fehlgeschlagen: " << ex.what();
        antworten(browser, m, json::object(), std::string("Fehler im EulenSlicer: ") + ex.what());
    }
    return true;
}

void eulen_fenster(const std::string& seite, int auftrag)
{
    if (!eulen_portal_aktiv()) {
        melden("In diesem EulenSlicer ist kein Portal eingetragen.", wxOK | wxICON_WARNING);
        return;
    }
    const std::string url = kern::seiten_url(api(), seite, seiten_param(auftrag));
    if (g_fenster) {
        g_fenster->laden(url, auftrag);
        g_fenster->Show();
        g_fenster->Raise();
        return;
    }
    auto* f   = new EulenFenster(wxGetApp().mainframe, url, auftrag);
    g_fenster = f;
    f->Show();
}

void eulen_senden()
{
    if (!eulen_portal_aktiv()) {
        melden("In diesem EulenSlicer ist kein Portal eingetragen.", wxOK | wxICON_WARNING);
        return;
    }
    if (!angemeldet()) {
        eulen_fenster("anmelden");
        return;
    }
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr || plater->model().objects.empty()) {
        melden("Auf der Platte liegt noch nichts. Erst ein Modell laden oder ein Projekt öffnen.");
        return;
    }
    if (plater->is_background_process_slicing()) {
        melden("Bitte warten, bis das Slicen fertig ist.");
        return;
    }
    if (g_sendet.exchange(true))
        return;
    struct Frei
    {
        ~Frei() { g_sendet = false; }
    } frei;

    ablage_wegraeumen();
    const fs::path            ordner = fs::path(wxStandardPaths::Get().GetTempDir().ToStdWstring()) / "EulenSlicer-Senden";
    boost::system::error_code ec;
    fs::create_directories(ordner, ec);

    std::string name = kern::profil_dateiname(into_u8(plater->get_project_name()));
    if (name.empty())
        name = "Projekt";
    PartPlateList&    platten   = plater->get_partplate_list();
    PartPlate*        platte    = platten.get_curr_plate();
    const bool        geslicet  = platte != nullptr && platte->is_slice_result_valid();
    const std::string dateiname = name + (geslicet ? ".gcode.3mf" : ".3mf");
    const fs::path    datei     = ordner / pfad(dateiname);
    fs::remove(datei, ec);

    SaveStrategy strategie = SaveStrategy::Silence | SaveStrategy::SplitModel;
    if (geslicet)
        strategie = strategie | SaveStrategy::WithGcode;
    BOOST_LOG_TRIVIAL(info) << "EulenSlicer: sende " << datei.string() << (geslicet ? " (geslicet)" : " (nur Modell)");
    if (plater->export_3mf(datei, strategie, geslicet ? platten.get_curr_plate_index() : -1) < 0 || !fs::exists(datei, ec)) {
        melden("Das Projekt ließ sich nicht als 3MF speichern.", wxOK | wxICON_ERROR);
        return;
    }
    std::string fehler;
    const auto  ablage = hochladen(datei, dateiname, fehler);
    if (!ablage) {
        fs::remove(datei, ec);
        if (!fehler.empty())
            melden("Senden an das Portal hat nicht geklappt:\n" + fehler, wxOK | wxICON_WARNING);
        return;
    }
    zustand().ablage = *ablage;
    eulen_fenster("senden");
}

void eulen_profile_abgleichen(bool vom_nutzer)
{
    profile_abgleichen([vom_nutzer](const ProfilErgebnis& e) {
        if (!e.drucker_text.empty())
            hinweis(e.drucker_text);
        const bool etwas = e.neu + e.geaendert + e.entfernt > 0;
        if (!e.fehler.empty() && !etwas) {
            if (vom_nutzer)
                melden(e.fehler, wxOK | wxICON_WARNING);
            else
                BOOST_LOG_TRIVIAL(warning) << "EulenSlicer: Profile: " << e.fehler;
            return;
        }
        if (etwas || vom_nutzer || e.belegt > 0)
            hinweis(profil_text(e));
    });
}

wxMenu* eulen_menue(wxWindow* fenster)
{
    auto* menu  = new wxMenu();
    auto  aktiv = []() { return eulen_portal_aktiv(); };
    append_menu_item(menu, wxID_ANY, utf8("An 3 Eulen senden"), utf8("Projekt als Druckauftrag ins 3 Eulen Portal schicken"),
                     [](wxCommandEvent&) { eulen_senden(); }, "", fenster, aktiv, fenster);
    append_menu_item(menu, wxID_ANY, utf8("Meine Aufträge"), utf8("Status, Rückfragen und Dateien deiner Druckaufträge"),
                     [](wxCommandEvent&) { eulen_fenster("auftraege"); }, "", fenster, aktiv, fenster);
    append_menu_item(menu, wxID_ANY, utf8("Anmelden / Konto"), utf8("Mit dem Portal verbinden – mit Konto oder Werkstatt-Code"),
                     [](wxCommandEvent&) { eulen_fenster("anmelden"); }, "", fenster, aktiv, fenster);
    menu->AppendSeparator();
    append_menu_item(menu, wxID_ANY, utf8("Profile aus dem Portal holen"), utf8("Drucker-, Filament- und Prozess-Profile aus dem Portal abgleichen"),
                     [](wxCommandEvent&) { eulen_profile_abgleichen(true); }, "", fenster, aktiv, fenster);
    append_menu_item(menu, wxID_ANY, utf8("Nach Updates suchen"), utf8("Im Portal nach einer neuen Version des EulenSlicers sehen"),
                     [](wxCommandEvent&) { eulen_update_pruefen(true); }, "", fenster, aktiv, fenster);
    append_menu_item(menu, wxID_ANY, utf8("Portal im Browser öffnen"), utf8("Das 3 Eulen Portal im Standardbrowser öffnen"),
                     [](wxCommandEvent&) {
                         const std::string o = kern::ursprung(api());
                         if (!o.empty())
                             wxLaunchDefaultBrowser(utf8(o + "/"));
                     },
                     "", fenster, aktiv, fenster);
    return menu;
}

void eulen_beim_start()
{
    if (eulen_portal_aktiv() && angemeldet())
        eulen_profile_abgleichen(false);
}

} // namespace GUI
} // namespace Slic3r
