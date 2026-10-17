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
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--orca", required=True, help="Checkout von OrcaSlicer (mit resources/profiles)")
    ap.add_argument("--version", default="", help="Version von OrcaSlicer für die Tabelle (z. B. v2.4.2)")
    args = ap.parse_args()

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
