// EulenSlicer – Update-Kanal über das 3 Eulen Portal: Kern ohne Oberfläche.
//
// Liest die Antwort von GET …/eulen/v1/slicer/update?build=N, prüft die geladene
// MSI (Größe, SHA-256) und die Ed25519-Signatur über den SHA-256 als Hex-Text.
// Nur Standardbibliothek, nlohmann/json und OpenSSL (ab 1.1.1) – damit lässt
// sich der Kern ohne wxWidgets testen (tests/kern_test.cpp).
//
// Lizenz: AGPL-3.0 wie OrcaSlicer.
#pragma once

#include <cstdint>
#include <string>

namespace Slic3r {
namespace Eulen {

// So groß darf eine MSI höchstens sein (wie im Portal).
constexpr std::int64_t MSI_MAX = 600LL * 1024 * 1024;

struct UpdateInfo
{
    bool          ok    = false; // Antwort verstanden
    bool          neuer = false; // Portal hat einen neueren Build als wir
    int           build = 0;
    std::string   version;       // z. B. 2.4.2
    std::string   orca;          // Version von OrcaSlicer
    std::string   name;          // „EulenSlicer 2.4.2 (Build 17)“
    std::string   notizen;
    std::string   msi_url;
    std::string   msi_name;
    std::int64_t  groesse = 0;
    std::string   sha256;        // 64 Zeichen hex, klein
    std::string   signatur;      // Base64, 64 Byte
    std::string   fehler;        // warum nicht ok
};

// Adresse der Abfrage: basis + „?build=N“ (oder „&build=N“, wenn basis schon „?“ hat,
// z. B. bei …/?rest_route=/eulen/v1/slicer/update).
std::string update_adresse(const std::string& basis, int eigener_build);

// Antwort des Portals lesen und prüfen. eigener_build entscheidet über „neuer“
// (das Feld „neuer“ der Antwort zählt nicht – wir vergleichen selbst).
UpdateInfo update_lesen(const std::string& json_text, int eigener_build);

// https:// – oder http:// nur für localhost/127.0.0.1 (Tests).
bool adresse_erlaubt(const std::string& url);

// SHA-256 als Hex-Text (klein).
std::string sha256_hex(const std::string& daten);

// Ed25519-Signatur (Base64, 64 Byte) über den SHA-256-Hex-Text prüfen,
// öffentlicher Schlüssel als Base64 (32 Byte).
bool signatur_gueltig(const std::string& sha256_hex_text, const std::string& signatur_b64, const std::string& pubkey_b64);

// Base64 (auch URL-Variante, mit oder ohne „=“) dekodieren. false bei Fehlern.
bool base64_dekodieren(const std::string& text, std::string& ergebnis);

// Dateiname für die MSI im Temp-Ordner: nur [A-Za-z0-9._-], endet auf .msi.
std::string dateiname_sauber(const std::string& name, int build);

// Base64 (Standard, mit „=“).
std::string base64_kodieren(const std::string& daten);

// PowerShell-Skript für die Installation: wartet, bis der EulenSlicer (pid)
// beendet ist, installiert die MSI mit msiexec /passive (Protokoll in log) und
// startet danach exe wieder – auch wenn die Installation abgebrochen wurde.
std::wstring installer_skript(unsigned long pid, const std::wstring& msi, const std::wstring& log, const std::wstring& exe);

// Text für „powershell -EncodedCommand“: Base64 von UTF-16LE. So braucht es
// keine Anführungszeichen-Akrobatik für Pfade mit Leerzeichen oder Apostroph.
std::string powershell_kodiert(const std::wstring& skript);

// Alles zusammen: passt die geladene Datei zur Antwort und zum Schlüssel?
// Liefert leer bei Erfolg, sonst den Grund (Deutsch).
std::string datei_pruefen(const std::string& daten, const UpdateInfo& info, const std::string& pubkey_b64);

} // namespace Eulen
} // namespace Slic3r
