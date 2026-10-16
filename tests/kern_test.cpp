// Test für den Kern des Update-Kanals (ohne wxWidgets, ohne OrcaSlicer).
//
//   g++ -std=c++17 -I quellen -I <orca>/deps_src tests/kern_test.cpp quellen/EulenUpdaterKern.cpp -lcrypto -o kern_test && ./kern_test
//
// Läuft in GitHub Actions („Prüfen“) gegen OpenSSL 3; OrcaSlicer baut mit
// OpenSSL 1.1.1 – der Kern nutzt nur Funktionen, die es dort schon gibt.
#include "EulenUpdaterKern.hpp"

#include <cstdio>
#include <memory>
#include <string>

#include <openssl/evp.h>

using namespace Slic3r::Eulen;

static int fehler = 0;
#define PRUEFE(bed)                                                                 \
    do {                                                                            \
        if (!(bed)) {                                                               \
            std::fprintf(stderr, "FEHLER Zeile %d: %s\n", __LINE__, #bed);         \
            ++fehler;                                                               \
        }                                                                           \
    } while (0)

static std::string b64(const std::string& roh)
{
    std::string out(4 * ((roh.size() + 2) / 3) + 1, '\0');
    int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&out[0]), reinterpret_cast<const unsigned char*>(roh.data()), static_cast<int>(roh.size()));
    out.resize(n);
    return out;
}

struct Schluessel
{
    std::string pub;  // Base64
    EVP_PKEY*   pkey = nullptr;
    ~Schluessel() { EVP_PKEY_free(pkey); }
};

static void erzeugen(Schluessel& s)
{
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr), &EVP_PKEY_CTX_free);
    EVP_PKEY_keygen_init(ctx.get());
    EVP_PKEY_keygen(ctx.get(), &s.pkey);
    unsigned char roh[32];
    size_t        len = sizeof(roh);
    EVP_PKEY_get_raw_public_key(s.pkey, roh, &len);
    s.pub = b64(std::string(reinterpret_cast<char*>(roh), len));
}

static std::string signieren(EVP_PKEY* pkey, const std::string& text)
{
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, pkey);
    unsigned char sig[64];
    size_t        len = sizeof(sig);
    EVP_DigestSign(ctx.get(), sig, &len, reinterpret_cast<const unsigned char*>(text.data()), text.size());
    return b64(std::string(reinterpret_cast<char*>(sig), len));
}

int main()
{
    // Adresse der Abfrage
    PRUEFE(update_adresse("https://x.de/wp-json/eulen/v1/slicer/update", 5) == "https://x.de/wp-json/eulen/v1/slicer/update?build=5");
    PRUEFE(update_adresse("https://x.de/?rest_route=/eulen/v1/slicer/update", 7) == "https://x.de/?rest_route=/eulen/v1/slicer/update&build=7");
    PRUEFE(adresse_erlaubt("https://x.de/a"));
    PRUEFE(adresse_erlaubt("HTTPS://X.DE/a"));
    PRUEFE(adresse_erlaubt("http://localhost:8099/a"));
    PRUEFE(!adresse_erlaubt("http://x.de/a"));
    PRUEFE(!adresse_erlaubt("http://localhost.boese.de/a"));
    PRUEFE(!adresse_erlaubt("ftp://x.de/a"));
    PRUEFE(!adresse_erlaubt("https://"));

    // SHA-256 (bekannter Wert)
    PRUEFE(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // Base64
    std::string roh;
    PRUEFE(base64_dekodieren("aGFsbG8=", roh) && roh == "hallo");
    PRUEFE(base64_dekodieren("aGFsbG8", roh) && roh == "hallo");
    PRUEFE(base64_dekodieren("-_-_", roh) && roh.size() == 3);
    PRUEFE(!base64_dekodieren("a", roh));
    PRUEFE(!base64_dekodieren("ab$c", roh));

    // Fester Prüfwert: vom Portal-Test (Schlüssel aus dem Portal, signiert mit Python/cryptography)
    PRUEFE(signatur_gueltig("e0a00244608c977ae1304ef59b196518737aa9f38e72c06db4a2412fbe6102a0",
                            "rkoat3jwTub/qW8gTxgN/RflkDgJaJBA/eshgWXahUdH55wKQXSJgl6uXYweX97IpKGcBGKeK3i9uUvGbfpMDA==",
                            "hq2MLrXbM3flyP51ulSJs7Fk2kg/GH/tB5EtnJDdcLY="));
    PRUEFE(!signatur_gueltig("e0a00244608c977ae1304ef59b196518737aa9f38e72c06db4a2412fbe6102a1",
                             "rkoat3jwTub/qW8gTxgN/RflkDgJaJBA/eshgWXahUdH55wKQXSJgl6uXYweX97IpKGcBGKeK3i9uUvGbfpMDA==",
                             "hq2MLrXbM3flyP51ulSJs7Fk2kg/GH/tB5EtnJDdcLY="));

    // Eigene Schlüssel
    Schluessel a, b;
    erzeugen(a);
    erzeugen(b);
    const std::string msi  = std::string(5000, 'x') + "EulenSlicer";
    const std::string sha  = sha256_hex(msi);
    const std::string sigA = signieren(a.pkey, sha);
    PRUEFE(signatur_gueltig(sha, sigA, a.pub));
    PRUEFE(!signatur_gueltig(sha, sigA, b.pub));
    PRUEFE(!signatur_gueltig(sha, signieren(a.pkey, sha + "x"), a.pub));
    PRUEFE(!signatur_gueltig("KEINHEX", sigA, a.pub));

    // Antwort lesen
    const std::string json = R"J({"tag_name":"v2.4.2-b17","name":"EulenSlicer 2.4.2 (Build 17)","body":"Neu: X","version":"2.4.2","orca":"2.4.2","build":17,"neuer":true,
        "msi":{"url":"https://x.de/wp-json/eulen/v1/slicer/laden/abc/EulenSlicer.msi","name":"EulenSlicer-2.4.2-b17.msi","groesse":)J" +
                             std::to_string(msi.size()) + R"(,"sha256":")" + sha + R"(","signatur":")" + sigA + R"("}})";
    UpdateInfo info = update_lesen(json, 16);
    PRUEFE(info.ok);
    PRUEFE(info.neuer);
    PRUEFE(info.build == 17);
    PRUEFE(info.notizen == "Neu: X");
    PRUEFE(info.msi_name == "EulenSlicer-2.4.2-b17.msi");
    PRUEFE(update_lesen(json, 17).ok && !update_lesen(json, 17).neuer);
    PRUEFE(update_lesen(json, 99).ok && !update_lesen(json, 99).neuer);
    PRUEFE(datei_pruefen(msi, info, a.pub).empty());
    PRUEFE(!datei_pruefen(msi + "y", info, a.pub).empty());
    PRUEFE(!datei_pruefen(msi, info, b.pub).empty());
    PRUEFE(!datei_pruefen(msi, info, "").empty());
    std::string kaputt = msi;
    kaputt[10]         = 'y';
    PRUEFE(datei_pruefen(kaputt, info, a.pub) == "Die Prüfsumme der Datei stimmt nicht.");

    // Ohne Version, kaputt, unsicher
    UpdateInfo leer = update_lesen(R"({"build":0,"neuer":false,"kanal":"stabil"})", 3);
    PRUEFE(leer.ok && !leer.neuer && leer.build == 0);
    PRUEFE(!update_lesen("<html>", 3).ok);
    PRUEFE(!update_lesen("[]", 3).ok);
    std::string http = json;
    http.replace(http.find("https://x.de/wp-json"), 8, "http://");
    PRUEFE(!update_lesen(http, 3).ok);
    PRUEFE(!update_lesen(R"({"build":5,"msi":{"url":"https://x.de/a.msi","groesse":2000,"sha256":")" + sha + R"("}})", 3).ok);
    PRUEFE(!update_lesen(R"({"build":5,"msi":{"url":"https://x.de/a.msi","groesse":99999999999,"sha256":")" + sha + R"(","signatur":")" + sigA + R"("}})", 3).ok);

    // Dateiname
    PRUEFE(dateiname_sauber("EulenSlicer-2.4.2-b17.msi", 17) == "EulenSlicer-2.4.2-b17.msi");
    PRUEFE(dateiname_sauber("..\\..\\boese.exe", 17) == "EulenSlicer-b17.msi");
    PRUEFE(dateiname_sauber("../a b.msi", 3) == "ab.msi");

    // Base64 kodieren (Rückweg)
    for (const std::string t : {"", "a", "ab", "abc", "abcd", "EulenSlicer\x01\xff"}) {
        std::string zurueck;
        PRUEFE(base64_kodieren(t) == b64(t));
        PRUEFE(t.empty() || (base64_dekodieren(base64_kodieren(t), zurueck) && zurueck == t));
    }

    // Installer-Skript für PowerShell
    const std::wstring skript = installer_skript(4242, L"C:\\Users\\O'Brien\\AppData\\Local\\Temp\\EulenSlicer-Update\\EulenSlicer-2.4.2-b17.msi",
                                                 L"C:\\Temp\\install.log", L"C:\\Program Files\\EulenSlicer\\orca-slicer.exe");
    PRUEFE(skript.find(L"Wait-Process -Id 4242") != std::wstring::npos);
    PRUEFE(skript.find(L"'\"C:\\Users\\O''Brien\\") != std::wstring::npos);
    PRUEFE(skript.find(L"-FilePath 'C:\\Program Files\\EulenSlicer\\orca-slicer.exe'") != std::wstring::npos);
    PRUEFE(skript.find(L"'/passive', '/norestart'") != std::wstring::npos);
    // „ab“ als UTF-16LE: 61 00 62 00
    PRUEFE(powershell_kodiert(L"ab") == "YQBiAA==");

    if (fehler == 0)
        std::printf("Kern: alle Prüfungen bestanden.\n");
    return fehler == 0 ? 0 : 1;
}
