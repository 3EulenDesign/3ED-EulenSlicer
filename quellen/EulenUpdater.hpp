// EulenSlicer – Update-Kanal über das 3 Eulen Portal (Oberfläche).
//
// Ersetzt die Versionsprüfung von OrcaSlicer (GitHub): GUI_App::check_new_version_sf()
// ruft eulen_update_pruefen() auf. Gibt es im Portal einen neueren Build, fragt der
// EulenSlicer, lädt die MSI, prüft Größe, SHA-256 und Signatur, beendet sich und
// startet die Installation (msiexec /passive, Windows fragt nach Administratorrechten).
// Danach startet er sich neu.
//
// Lizenz: AGPL-3.0 wie OrcaSlicer.
#pragma once

#include "EulenKonfig.hpp"

namespace Slic3r {
namespace GUI {

// Im Portal nachsehen. vom_nutzer = über das Menü „Nach Updates suchen“: dann
// auch „ist aktuell“ und Fehler anzeigen und eine übersprungene Version anbieten.
void eulen_update_pruefen(bool vom_nutzer);

} // namespace GUI
} // namespace Slic3r
