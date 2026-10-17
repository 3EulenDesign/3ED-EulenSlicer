#!/usr/bin/env python3
"""Tabelle der Filament-Profile von Bambu Lab je Drucker erzeugen (für das Portal).

    python skripte/filamente.py --orca ../OrcaSlicer > orca-filamente.json

Das Portal (Modul „slicer“ ab 1.4.0, daten/orca-filamente.json) legt für jede Sorte im
Filamentlager ein eigenes Filament-Profil an, das von einem Profil von Bambu Lab erbt –
„Bambu PLA Basic @BBL A1“ für den A1, „Bambu PLA Basic @BBL X1C“ für den P1S. Welches
Profil zu welchem Drucker passt, steht nur in den Profilen von OrcaSlicer
(compatible_printers) und folgt keiner festen Regel. Darum diese Tabelle:

    {"orca": "v2.4.2", "drucker": {"Bambu Lab A1": {"Bambu PLA Basic": "Bambu PLA Basic @BBL A1", …}}}

Je Drucker (Modell aus BBL.json, Düse 0.4) alle Profile mit instantiation = true, deren
compatible_printers „<Modell> 0.4 nozzle“ enthält. Schlüssel ist der Name vor „ @“; gibt es
mehrere, gewinnt der ohne Düse im Namen. Nach einem Update von OrcaSlicer neu erzeugen und
ins Repo 3ED-App kopieren.

Zwei Tabellen vergleichen (macht „OrcaSlicer-Update“ bei jeder neuen Version):

    python skripte/filamente.py --vergleichen alt.json neu.json

Gibt die Unterschiede aus; Rückgabe 0 = gleich, 1 = geändert, 2 = Fehler.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


def vergleichen(alt_datei: str, neu_datei: str, zeilen_max: int = 60) -> int:
    """Unterschiede zweier Tabellen ausgeben. 0 = gleich, 1 = geändert, 2 = Fehler."""
    try:
        alt = json.loads(Path(alt_datei).read_text(encoding="utf-8"))["drucker"]
        neu = json.loads(Path(neu_datei).read_text(encoding="utf-8"))["drucker"]
    except (OSError, ValueError, KeyError, TypeError) as fehler:
        print(f"Tabelle nicht lesbar: {fehler}")
        return 2
    if alt == neu:
        print("Keine Unterschiede.")
        return 0
    zeilen: list[str] = []
    for drucker in sorted(set(alt) | set(neu)):
        a, n = alt.get(drucker), neu.get(drucker)
        if a is None:
            zeilen.append(f"+ {drucker}: neuer Drucker ({len(n)} Profile)")
            continue
        if n is None:
            zeilen.append(f"- {drucker}: Drucker fehlt jetzt")
            continue
        for sorte in sorted(set(a) | set(n)):
            if sorte not in a:
                zeilen.append(f"+ {drucker} / {sorte}: {n[sorte]}")
            elif sorte not in n:
                zeilen.append(f"- {drucker} / {sorte}: war {a[sorte]}")
            elif a[sorte] != n[sorte]:
                zeilen.append(f"~ {drucker} / {sorte}: {a[sorte]} → {n[sorte]}")
    for zeile in zeilen[:zeilen_max]:
        print(zeile)
    if len(zeilen) > zeilen_max:
        print(f"… und {len(zeilen) - zeilen_max} weitere")
    return 1


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--orca", help="Checkout von OrcaSlicer (mit resources/profiles)")
    ap.add_argument("--version", default="", help="Version von OrcaSlicer für die Tabelle (z. B. v2.4.2)")
    ap.add_argument("--vergleichen", nargs=2, metavar=("ALT", "NEU"), help="zwei Tabellen vergleichen statt eine zu erzeugen")
    args = ap.parse_args()
    if args.vergleichen:
        try:
            code = vergleichen(*args.vergleichen)
        except Exception as fehler:  # noqa: BLE001 – „Fehler“ (2), nicht „geändert“ (1)
            print(f"Vergleich fehlgeschlagen: {fehler}")
            code = 2
        sys.exit(code)
    if not args.orca:
        ap.error("--orca fehlt")

    bbl = Path(args.orca) / "resources" / "profiles"
    vendor = json.loads((bbl / "BBL.json").read_text(encoding="utf-8"))
    modelle = [m["name"] for m in vendor.get("machine_model_list", [])]

    presets: dict[str, dict] = {}
    for datei in sorted((bbl / "BBL" / "filament").glob("*.json")):
        try:
            j = json.loads(datei.read_text(encoding="utf-8"))
        except (ValueError, UnicodeDecodeError):
            continue
        presets[j.get("name", datei.stem)] = j

    tabelle: dict[str, dict[str, str]] = {}
    for modell in modelle:
        drucker = f"{modell} 0.4 nozzle"
        eintraege: dict[str, str] = {}
        for name in sorted(presets):
            j = presets[name]
            if j.get("instantiation") != "true" or drucker not in (j.get("compatible_printers") or []):
                continue
            basis = re.split(r" @", name, maxsplit=1)[0]
            alt = eintraege.get(basis)
            # Kürzere Namen (ohne „ 0.4 nozzle“) gehen vor.
            if alt is None or ("nozzle" in alt and "nozzle" not in name) or (len(name) < len(alt) and ("nozzle" in name) == ("nozzle" in alt)):
                eintraege[basis] = name
        if eintraege:
            tabelle[modell] = dict(sorted(eintraege.items()))

    if not tabelle:
        sys.exit("Keine Profile gefunden – stimmt --orca?")
    json.dump({"orca": args.version, "drucker": tabelle}, sys.stdout, ensure_ascii=False, indent=1, sort_keys=False)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
