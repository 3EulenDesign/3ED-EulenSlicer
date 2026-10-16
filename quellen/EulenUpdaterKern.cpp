// EulenSlicer – Update-Kanal über das 3 Eulen Portal: Kern ohne Oberfläche.
// Lizenz: AGPL-3.0 wie OrcaSlicer.
#include "EulenUpdaterKern.hpp"

#include <algorithm>
#include <cctype>
#include <memory>

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

namespace Slic3r {
namespace Eulen {

namespace {

std::string klein(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool ist_hex64(const std::string& s)
{
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) || (c >= 'a' && c <= 'f'); });
}

std::string text(const nlohmann::json& j, const char* feld)
{
    auto it = j.find(feld);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::int64_t zahl(const nlohmann::json& j, const char* feld)
{
    auto it = j.find(feld);
    if (it == j.end())
        return 0;
    if (it->is_number_integer())
        return it->get<std::int64_t>();
    if (it->is_string()) {
        const std::string s = it->get<std::string>();
        if (!s.empty() && s.size() < 19 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); }))
            return std::stoll(s);
    }
    return 0;
}

} // namespace

std::string update_adresse(const std::string& basis, int eigener_build)
{
    std::string url = basis;
    url += url.find('?') == std::string::npos ? '?' : '&';
    url += "build=" + std::to_string(std::max(0, eigener_build));
    return url;
}

bool adresse_erlaubt(const std::string& url)
{
    const std::string u = klein(url);
    if (u.rfind("https://", 0) == 0)
        return u.size() > 8;
    for (const char* lokal : {"http://localhost:", "http://localhost/", "http://127.0.0.1:", "http://127.0.0.1/"})
        if (u.rfind(lokal, 0) == 0)
            return true;
    return false;
}

UpdateInfo update_lesen(const std::string& json_text, int eigener_build)
{
    UpdateInfo info;
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_text);
    } catch (...) {
        info.fehler = "Die Antwort des Portals ist kein JSON.";
        return info;
    }
    if (!j.is_object()) {
        info.fehler = "Die Antwort des Portals hat ein unbekanntes Format.";
        return info;
    }
    info.build = static_cast<int>(std::clamp<std::int64_t>(zahl(j, "build"), 0, 999999));
    if (info.build <= 0) {
        // Noch keine Version im Portal.
        info.ok = true;
        return info;
    }
    info.version = text(j, "version");
    info.orca    = text(j, "orca");
    info.name    = text(j, "name");
    info.notizen = text(j, "body");
    auto msi     = j.find("msi");
    if (msi == j.end() || !msi->is_object()) {
        info.fehler = "In der Antwort fehlt die MSI.";
        return info;
    }
    info.msi_url  = text(*msi, "url");
    info.msi_name = text(*msi, "name");
    info.groesse  = zahl(*msi, "groesse");
    info.sha256   = klein(text(*msi, "sha256"));
    info.signatur = text(*msi, "signatur");
    if (!adresse_erlaubt(info.msi_url)) {
        info.fehler = "Die Adresse der MSI ist nicht https.";
        return info;
    }
    if (info.groesse < 1024 || info.groesse > MSI_MAX) {
        info.fehler = "Die Größe der MSI ist ungültig.";
        return info;
    }
    if (!ist_hex64(info.sha256)) {
        info.fehler = "Die Prüfsumme der MSI fehlt.";
        return info;
    }
    std::string sig;
    if (!base64_dekodieren(info.signatur, sig) || sig.size() != 64) {
        info.fehler = "Die Version ist nicht signiert.";
        return info;
    }
    if (info.name.empty())
        info.name = "EulenSlicer " + info.version + " (Build " + std::to_string(info.build) + ")";
    info.neuer = info.build > eigener_build;
    info.ok    = true;
    return info;
}

std::string sha256_hex(const std::string& daten)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  laenge = 0;
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx.get(), daten.data(), daten.size()) != 1 ||
        EVP_DigestFinal_ex(ctx.get(), digest, &laenge) != 1)
        return std::string();
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(laenge * 2);
    for (unsigned int i = 0; i < laenge; ++i) {
        out.push_back(hex[digest[i] >> 4]);
        out.push_back(hex[digest[i] & 0x0f]);
    }
    return out;
}

bool base64_dekodieren(const std::string& eingabe, std::string& ergebnis)
{
    ergebnis.clear();
    std::string s;
    s.reserve(eingabe.size());
    for (char c : eingabe) {
        if (std::isspace(static_cast<unsigned char>(c)))
            continue;
        if (c == '-')
            c = '+';
        else if (c == '_')
            c = '/';
        s.push_back(c);
    }
    while (!s.empty() && s.back() == '=')
        s.pop_back();
    if (s.empty() || s.size() % 4 == 1)
        return false;
    auto wert = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    unsigned int puffer = 0;
    int          bits   = 0;
    for (char c : s) {
        const int v = wert(c);
        if (v < 0)
            return false;
        puffer = (puffer << 6) | static_cast<unsigned int>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            ergebnis.push_back(static_cast<char>((puffer >> bits) & 0xff));
        }
    }
    return true;
}

bool signatur_gueltig(const std::string& sha256_hex_text, const std::string& signatur_b64, const std::string& pubkey_b64)
{
    std::string sig, key;
    if (!ist_hex64(sha256_hex_text) || !base64_dekodieren(signatur_b64, sig) || !base64_dekodieren(pubkey_b64, key) ||
        sig.size() != 64 || key.size() != 32)
        return false;
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(
        EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char*>(key.data()), key.size()),
        &EVP_PKEY_free);
    if (!pkey)
        return false;
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!ctx || EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, pkey.get()) != 1)
        return false;
    return EVP_DigestVerify(ctx.get(), reinterpret_cast<const unsigned char*>(sig.data()), sig.size(),
                            reinterpret_cast<const unsigned char*>(sha256_hex_text.data()), sha256_hex_text.size()) == 1;
}

std::string dateiname_sauber(const std::string& name, int build)
{
    std::string out;
    for (char c : name)
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-')
            out.push_back(c);
    while (!out.empty() && out.front() == '.')
        out.erase(out.begin());
    if (out.size() > 100)
        out.erase(0, out.size() - 100);
    if (out.size() < 5 || klein(out.substr(out.size() - 4)) != ".msi")
        out = "EulenSlicer-b" + std::to_string(build) + ".msi";
    return out;
}

std::string base64_kodieren(const std::string& daten)
{
    static const char* zeichen = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(4 * ((daten.size() + 2) / 3));
    size_t i = 0;
    for (; i + 2 < daten.size(); i += 3) {
        const unsigned int n = (static_cast<unsigned char>(daten[i]) << 16) | (static_cast<unsigned char>(daten[i + 1]) << 8) | static_cast<unsigned char>(daten[i + 2]);
        out.push_back(zeichen[(n >> 18) & 63]);
        out.push_back(zeichen[(n >> 12) & 63]);
        out.push_back(zeichen[(n >> 6) & 63]);
        out.push_back(zeichen[n & 63]);
    }
    if (i + 1 == daten.size()) {
        const unsigned int n = static_cast<unsigned char>(daten[i]) << 16;
        out.push_back(zeichen[(n >> 18) & 63]);
        out.push_back(zeichen[(n >> 12) & 63]);
        out += "==";
    } else if (i + 2 == daten.size()) {
        const unsigned int n = (static_cast<unsigned char>(daten[i]) << 16) | (static_cast<unsigned char>(daten[i + 1]) << 8);
        out.push_back(zeichen[(n >> 18) & 63]);
        out.push_back(zeichen[(n >> 12) & 63]);
        out.push_back(zeichen[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

namespace {

// Für PowerShell in einfachen Anführungszeichen: ' wird zu ''.
std::wstring ps_text(const std::wstring& s)
{
    std::wstring out = L"'";
    for (wchar_t c : s) {
        if (c == L'\'')
            out += L"''";
        else
            out.push_back(c);
    }
    out += L"'";
    return out;
}

} // namespace

std::wstring installer_skript(unsigned long pid, const std::wstring& msi, const std::wstring& log, const std::wstring& exe)
{
    std::wstring s;
    s += L"$ErrorActionPreference = 'SilentlyContinue'\n";
    s += L"Wait-Process -Id " + std::to_wstring(pid) + L" -Timeout 120\n";
    s += L"Start-Sleep -Seconds 1\n";
    s += L"Start-Process -FilePath 'msiexec.exe' -ArgumentList @('/i', " + ps_text(L"\"" + msi + L"\"") + L", '/passive', '/norestart', '/l*v', " +
         ps_text(L"\"" + log + L"\"") + L") -Wait\n";
    s += L"Start-Process -FilePath " + ps_text(exe) + L"\n";
    return s;
}

std::string powershell_kodiert(const std::wstring& skript)
{
    std::string bytes;
    bytes.reserve(skript.size() * 2);
    auto einheit = [&bytes](unsigned int u) {
        bytes.push_back(static_cast<char>(u & 0xff));
        bytes.push_back(static_cast<char>((u >> 8) & 0xff));
    };
    for (wchar_t c : skript) {
        const unsigned long cp = static_cast<unsigned long>(c);
        if (sizeof(wchar_t) > 2 && cp > 0xFFFF) { // Linux (UTF-32): als Ersatzpaar
            const unsigned long v = cp - 0x10000;
            einheit(0xD800 + static_cast<unsigned int>(v >> 10));
            einheit(0xDC00 + static_cast<unsigned int>(v & 0x3FF));
        } else {
            einheit(static_cast<unsigned int>(cp));
        }
    }
    return base64_kodieren(bytes);
}

std::string datei_pruefen(const std::string& daten, const UpdateInfo& info, const std::string& pubkey_b64)
{
    if (pubkey_b64.empty())
        return "In diesem EulenSlicer steckt kein Schlüssel für Updates – bitte die neue Version von Hand aus dem Portal installieren.";
    if (static_cast<std::int64_t>(daten.size()) != info.groesse)
        return "Die Datei ist nicht vollständig angekommen.";
    if (sha256_hex(daten) != info.sha256)
        return "Die Prüfsumme der Datei stimmt nicht.";
    if (!signatur_gueltig(info.sha256, info.signatur, pubkey_b64))
        return "Die Signatur passt nicht – die Datei stammt nicht aus unserem Build.";
    return std::string();
}

} // namespace Eulen
} // namespace Slic3r
