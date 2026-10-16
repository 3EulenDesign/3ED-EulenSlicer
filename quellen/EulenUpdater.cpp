// EulenSlicer – Update-Kanal über das 3 Eulen Portal (Oberfläche).
// Lizenz: AGPL-3.0 wie OrcaSlicer.
#include "EulenUpdater.hpp"
#include "EulenUpdaterKern.hpp"

#include <algorithm>
#include <atomic>
#include <string>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/log/trivial.hpp>

#include <wx/stdpaths.h>
#include <wx/utils.h>

#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/Utils/Http.hpp"

namespace Slic3r {
namespace GUI {

namespace {

namespace kern = ::Slic3r::Eulen;

// Läuft gerade eine Prüfung oder ein Download? (nicht doppelt anbieten)
std::atomic<bool> g_beschaeftigt{false};

const char* SPAETER_SCHLUESSEL = "eulen_update_spaeter";

wxString utf8(const std::string& s) { return wxString::FromUTF8(s.c_str()); }

wxWindow* fenster() { return wxGetApp().mainframe; }

void melden(const wxString& text, long stil = wxOK | wxICON_INFORMATION)
{
    MessageDialog dlg(fenster(), text, utf8("EulenSlicer"), stil);
    dlg.ShowModal();
}

std::string build_text(int build) { return std::to_string(build); }

// Installation anstoßen: PowerShell wartet, bis wir beendet sind, ruft msiexec auf und
// startet den EulenSlicer danach wieder.
bool installer_starten(const boost::filesystem::path& msi)
{
#ifdef _WIN32
    const boost::filesystem::path log = msi.parent_path() / "installation.log";
    const std::wstring exe            = wxStandardPaths::Get().GetExecutablePath().ToStdWstring();
    const std::wstring skript         = kern::installer_skript(wxGetProcessId(), msi.wstring(), log.wstring(), exe);
    const wxString befehl = wxString("powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -EncodedCommand ") +
                            utf8(kern::powershell_kodiert(skript));
    const long pid = wxExecute(befehl, wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE);
    BOOST_LOG_TRIVIAL(info) << "EulenSlicer-Update: Installation gestartet, PID " << pid << ", MSI " << msi.string();
    return pid != 0;
#else
    (void) msi;
    return false;
#endif
}

void herunterladen_und_installieren(const kern::UpdateInfo& info)
{
    ProgressDialog dlg(utf8("EulenSlicer-Update"), utf8("Lade " + info.name + " …"), 100, fenster(),
                       wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_CAN_ABORT);
    std::string daten;
    std::string fehler;
    bool        abgebrochen = false;
    int         gezeigt     = -1;

    auto http = Http::get(info.msi_url);
    http.tls_verify(info.msi_url.rfind("https://", 0) == 0)
        .timeout_connect(15)
        .timeout_max(30 * 60)
        .size_limit(static_cast<size_t>(kern::MSI_MAX) + 1024)
        .on_progress([&](Http::Progress fortschritt, bool& cancel) {
            const int prozent = fortschritt.dltotal > 0 ? static_cast<int>(fortschritt.dlnow * 99 / fortschritt.dltotal) : 0;
            if (prozent != gezeigt) {
                gezeigt = prozent;
                if (!dlg.Update(std::clamp(prozent, 0, 99))) {
                    abgebrochen = true;
                    cancel      = true;
                }
            }
        })
        .on_complete([&](std::string body, unsigned) { daten = std::move(body); })
        .on_error([&](std::string, std::string error, unsigned status) {
            fehler = !error.empty() ? error : "HTTP " + std::to_string(status);
        })
        .perform_sync();
    dlg.Update(100);

    if (abgebrochen)
        return;
    if (!fehler.empty()) {
        BOOST_LOG_TRIVIAL(error) << "EulenSlicer-Update: Download fehlgeschlagen: " << fehler;
        melden(utf8("Die neue Version ließ sich nicht laden:\n" + fehler + "\n\nIm Portal unter Interner Bereich → EulenSlicer gibt es sie auch zum Herunterladen."),
               wxOK | wxICON_WARNING);
        return;
    }
    const std::string grund = kern::datei_pruefen(daten, info, EULEN_UPDATE_PUBKEY);
    if (!grund.empty()) {
        BOOST_LOG_TRIVIAL(error) << "EulenSlicer-Update: Prüfung fehlgeschlagen: " << grund;
        melden(utf8("Die geladene Datei wird nicht installiert.\n" + grund), wxOK | wxICON_ERROR);
        return;
    }

    boost::system::error_code ec;
    const boost::filesystem::path ordner = boost::filesystem::path(wxStandardPaths::Get().GetTempDir().ToStdWstring()) / "EulenSlicer-Update";
    boost::filesystem::create_directories(ordner, ec);
    const boost::filesystem::path msi = ordner / kern::dateiname_sauber(info.msi_name, info.build);
    {
        boost::filesystem::ofstream out(msi, std::ios::binary | std::ios::trunc);
        out.write(daten.data(), static_cast<std::streamsize>(daten.size()));
        if (!out.good()) {
            melden(utf8("Die Datei ließ sich nicht im Temp-Ordner speichern."), wxOK | wxICON_ERROR);
            return;
        }
    }

    MessageDialog frage(fenster(),
                        utf8(info.name + " ist geladen und geprüft.\n\n"
                             "Der EulenSlicer schließt sich jetzt (ein ungespeichertes Projekt fragt er vorher ab). "
                             "Windows fragt nach Administratorrechten, dann wird installiert und der EulenSlicer startet neu."),
                        utf8("EulenSlicer-Update"), wxOK | wxCANCEL | wxICON_INFORMATION);
    if (frage.ShowModal() != wxID_OK)
        return;

    MainFrame* frame = wxGetApp().mainframe;
    if (frame == nullptr || !frame->Close(false)) {
        BOOST_LOG_TRIVIAL(info) << "EulenSlicer-Update: Schließen abgebrochen, nicht installiert.";
        return;
    }
    if (!installer_starten(msi))
        BOOST_LOG_TRIVIAL(error) << "EulenSlicer-Update: Installation ließ sich nicht starten: " << msi.string();
}

// Ergebnis der Prüfung, im Hauptfaden.
void ergebnis(const kern::UpdateInfo& info, bool vom_nutzer, const std::string& fehler)
{
    struct Frei
    {
        ~Frei() { g_beschaeftigt = false; }
    } frei;

    if (!fehler.empty() || !info.ok) {
        const std::string grund = !fehler.empty() ? fehler : info.fehler;
        BOOST_LOG_TRIVIAL(warning) << "EulenSlicer-Update: " << grund;
        if (vom_nutzer)
            melden(utf8("Im Portal ließ sich nicht nachsehen, ob es eine neue Version gibt:\n" + grund), wxOK | wxICON_WARNING);
        return;
    }
    if (!info.neuer) {
        if (vom_nutzer)
            melden(utf8("Du hast die neueste Version (Build " + build_text(EULEN_BUILD) + ")."));
        return;
    }
    AppConfig* config = wxGetApp().app_config;
    if (!vom_nutzer && config != nullptr && config->get(SPAETER_SCHLUESSEL) == build_text(info.build))
        return;

    std::string text = info.name + " ist da (du hast Build " + build_text(EULEN_BUILD) + ").";
    if (!info.notizen.empty()) {
        std::string notizen = info.notizen;
        if (notizen.size() > 1200)
            notizen = notizen.substr(0, 1200) + " …";
        text += "\n\n" + notizen;
    }
    text += "\n\nJetzt herunterladen und installieren?\n(Nein: erst wieder fragen, wenn es eine neuere Version gibt – oder über „Nach Updates suchen“.)";
    MessageDialog dlg(fenster(), utf8(text), utf8("Neue Version des EulenSlicers"), wxYES_NO | wxICON_INFORMATION);
    if (dlg.ShowModal() != wxID_YES) {
        if (config != nullptr)
            config->set(SPAETER_SCHLUESSEL, build_text(info.build));
        return;
    }
    if (config != nullptr)
        config->set(SPAETER_SCHLUESSEL, "");
    herunterladen_und_installieren(info);
}

} // namespace

void eulen_update_pruefen(bool vom_nutzer)
{
    const std::string basis = EULEN_UPDATE_URL;
    if (basis.empty() || !kern::adresse_erlaubt(basis)) {
        if (vom_nutzer)
            melden(utf8("In diesem EulenSlicer ist kein Portal für Updates eingetragen."), wxOK | wxICON_WARNING);
        return;
    }
    if (g_beschaeftigt.exchange(true))
        return;

    const std::string adresse = kern::update_adresse(basis, EULEN_BUILD);
    auto http = Http::get(adresse);
    http.tls_verify(adresse.rfind("https://", 0) == 0)
        .header("Accept", "application/json")
        .timeout_connect(5)
        .timeout_max(20)
        .on_complete([vom_nutzer](std::string body, unsigned status) {
            kern::UpdateInfo info = kern::update_lesen(body, EULEN_BUILD);
            std::string fehler    = status == 200 ? std::string() : "HTTP " + std::to_string(status);
            wxGetApp().CallAfter([info, vom_nutzer, fehler]() { ergebnis(info, vom_nutzer, fehler); });
        })
        .on_error([vom_nutzer](std::string, std::string error, unsigned status) {
            std::string fehler = !error.empty() ? error : "HTTP " + std::to_string(status);
            wxGetApp().CallAfter([vom_nutzer, fehler]() { ergebnis(kern::UpdateInfo(), vom_nutzer, fehler); });
        })
        .perform();
}

} // namespace GUI
} // namespace Slic3r
