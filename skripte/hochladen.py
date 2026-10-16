#!/usr/bin/env python3
"""MSI signieren und als neue Version ins 3 Eulen Portal hochladen.

Läuft in GitHub Actions nach dem Bauen, geht aber auch von Hand:

    PORTAL_API=https://example.org/wp-json/eulen/v1/ \\
    PORTAL_ZUGANG='eulenslicer-build:abcd efgh ...' \\
    SIGNATUR_SCHLUESSEL="$(cat schluessel.pem)" \\
    python skripte/hochladen.py --msi EulenSlicer.msi --version 2.4.2 --build 17 --orca 2.4.2

Ablauf (Modul „slicer“ im Portal):
  1. SHA-256 der MSI, Ed25519-Signatur über den SHA-256 als Hex-Text
  2. POST {api}slicer/releases            Version anmelden, Antwort mit teil_max
  3. POST {api}slicer/releases/{id}/teil  Stücke der Reihe nach (Wiederholung bei Abbruch)
  4. POST {api}slicer/releases/{id}/fertig  Portal prüft Größe, SHA-256 und Signatur

Umgebungsvariablen:
  PORTAL_API           REST-Adresse des Portals bis einschließlich „eulen/v1/“
  PORTAL_ZUGANG        Benutzername:Anwendungspasswort des Build-Kontos
  SIGNATUR_SCHLUESSEL  privater Ed25519-Schlüssel (PEM), aus dem Portal oder eigener
  UPDATE_PUBKEY        optional: öffentlicher Schlüssel (Base64) zur Gegenprobe
  NOTIZEN              optional: „Was ist neu?“ (statt --notizen)

Braucht nur die Standardbibliothek und „cryptography“ (sonst openssl ab 3.0).
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request


def fehler(text: str) -> None:
    print(f"::error::{text}" if os.environ.get("GITHUB_ACTIONS") else f"Fehler: {text}", file=sys.stderr)
    sys.exit(1)


def sha256_datei(pfad: str) -> str:
    h = hashlib.sha256()
    with open(pfad, "rb") as f:
        for stueck in iter(lambda: f.read(1024 * 1024), b""):
            h.update(stueck)
    return h.hexdigest()


def signieren(text: bytes, pem: str) -> bytes:
    """Ed25519-Signatur über text (64 Byte)."""
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
        from cryptography.hazmat.primitives.serialization import load_pem_private_key

        schluessel = load_pem_private_key(pem.encode(), password=None)
        if not isinstance(schluessel, Ed25519PrivateKey):
            fehler("SIGNATUR_SCHLUESSEL ist kein Ed25519-Schlüssel.")
        return schluessel.sign(text)
    except ImportError:
        pass
    with tempfile.TemporaryDirectory() as tmp:
        key = os.path.join(tmp, "k.pem")
        dat = os.path.join(tmp, "d.txt")
        with open(key, "w", encoding="ascii") as f:
            f.write(pem)
        with open(dat, "wb") as f:
            f.write(text)
        r = subprocess.run(["openssl", "pkeyutl", "-sign", "-inkey", key, "-rawin", "-in", dat], capture_output=True, check=False)
        if r.returncode != 0 or len(r.stdout) != 64:
            fehler("Signieren mit openssl ging nicht: " + r.stderr.decode(errors="replace"))
        return r.stdout


def pruefen(text: bytes, signatur: bytes, pubkey_b64: str) -> bool:
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
    except ImportError:
        print("Hinweis: Gegenprobe übersprungen (cryptography fehlt).")
        return True
    try:
        Ed25519PublicKey.from_public_bytes(base64.b64decode(pubkey_b64)).verify(signatur, text)
        return True
    except Exception:  # noqa: BLE001 – jede Abweichung heißt: passt nicht
        return False


class Portal:
    def __init__(self, api: str, zugang: str):
        self.api = api if api.endswith("/") else api + "/"
        nutzer, _, passwort = zugang.partition(":")
        if not nutzer or not passwort:
            fehler("PORTAL_ZUGANG muss so aussehen: benutzername:anwendungspasswort")
        token = base64.b64encode(f"{nutzer}:{passwort.replace(' ', '')}".encode()).decode()
        self.kopf = {"Authorization": f"Basic {token}", "User-Agent": "EulenSlicer-Build/1.0"}

    def url(self, pfad: str, **query: object) -> str:
        # Mit Permalinks: …/wp-json/eulen/v1/ + pfad. Ohne: …/?rest_route=/eulen/v1/ + pfad.
        if "rest_route=" in self.api:
            basis, _, rest = self.api.partition("rest_route=")
            ziel = basis + "rest_route=" + urllib.parse.quote(urllib.parse.unquote(rest) + pfad, safe="/")
            return ziel + ("&" + urllib.parse.urlencode(query) if query else "")
        return self.api + pfad + ("?" + urllib.parse.urlencode(query) if query else "")

    def anfrage(self, methode: str, pfad: str, daten: bytes | None = None, typ: str = "application/json", **query: object) -> dict:
        kopf = dict(self.kopf)
        if daten is not None:
            kopf["Content-Type"] = typ
        req = urllib.request.Request(self.url(pfad, **query), data=daten, method=methode, headers=kopf)
        try:
            with urllib.request.urlopen(req, timeout=300) as antwort:
                return json.loads(antwort.read().decode() or "{}")
        except urllib.error.HTTPError as e:
            text = e.read().decode(errors="replace")
            try:
                meldung = json.loads(text).get("message", text)
            except ValueError:
                meldung = text[:500]
            raise PortalFehler(e.code, meldung) from None


class PortalFehler(Exception):
    def __init__(self, status: int, meldung: str):
        super().__init__(f"HTTP {status}: {meldung}")
        self.status = status


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--msi", required=True)
    ap.add_argument("--version", required=True, help="z. B. 2.4.2")
    ap.add_argument("--build", required=True, type=int, help="fortlaufende Nummer")
    ap.add_argument("--orca", default="", help="Version von OrcaSlicer")
    ap.add_argument("--notizen", default=os.environ.get("NOTIZEN", ""), help="Was ist neu? (Standard: Umgebungsvariable NOTIZEN)")
    ap.add_argument("--notizen-datei", help="Was ist neu? aus einer Datei (UTF-8)")
    ap.add_argument("--nicht-freigeben", action="store_true", help="hochladen, aber zurückgezogen lassen (zum Testen)")
    ap.add_argument("--signatur-datei", help="Signatur (Base64) zusätzlich hierhin schreiben")
    args = ap.parse_args()

    api = os.environ.get("PORTAL_API", "").strip()
    zugang = os.environ.get("PORTAL_ZUGANG", "").strip()
    pem = os.environ.get("SIGNATUR_SCHLUESSEL", "").strip()
    if not api or not zugang:
        fehler("PORTAL_API und PORTAL_ZUGANG fehlen (GitHub: Variable PORTAL_API, Secret PORTAL_ZUGANG).")
    if not pem:
        fehler("SIGNATUR_SCHLUESSEL fehlt (GitHub: Secret SIGNATUR_SCHLUESSEL).")
    if "BEGIN" not in pem:
        fehler("SIGNATUR_SCHLUESSEL muss der PEM-Text sein (mit den Zeilen BEGIN/END PRIVATE KEY).")

    notizen = args.notizen
    if args.notizen_datei:
        with open(args.notizen_datei, encoding="utf-8") as f:
            notizen = f.read()
    notizen = notizen.strip()[:4000]

    groesse = os.path.getsize(args.msi)
    sha = sha256_datei(args.msi)
    signatur = signieren(sha.encode("ascii"), pem)
    sig_b64 = base64.b64encode(signatur).decode()
    pub = os.environ.get("UPDATE_PUBKEY", "").strip()
    if pub and not pruefen(sha.encode("ascii"), signatur, pub):
        fehler("Die Signatur passt nicht zu UPDATE_PUBKEY – privater und öffentlicher Schlüssel gehören nicht zusammen.")
    if args.signatur_datei:
        with open(args.signatur_datei, "w", encoding="ascii") as f:
            f.write(sig_b64 + "\n")
    print(f"MSI: {args.msi} ({groesse} Byte), SHA-256 {sha}")

    portal = Portal(api, zugang)
    try:
        r = portal.anfrage(
            "POST",
            "slicer/releases",
            json.dumps(
                {
                    "version": args.version,
                    "build": args.build,
                    "orca": args.orca,
                    "name": os.path.basename(args.msi),
                    "groesse": groesse,
                    "sha256": sha,
                    "signatur": sig_b64,
                    "notizen": notizen,
                }
            ).encode(),
        )
    except PortalFehler as e:
        fehler(f"Version anmelden: {e}")
    rid = r["id"]
    teil_max = int(r.get("teil_max") or 4 * 1024 * 1024)
    print(f"Angemeldet: {rid}, Stücke zu {teil_max} Byte")

    with open(args.msi, "rb") as f:
        nr = 0
        while True:
            stueck = f.read(teil_max)
            if not stueck:
                break
            for versuch in range(1, 6):
                try:
                    stand = portal.anfrage("POST", f"slicer/releases/{rid}/teil", stueck, "application/octet-stream", nr=nr)
                    break
                except (PortalFehler, urllib.error.URLError, TimeoutError, ConnectionError) as e:
                    if isinstance(e, PortalFehler) and e.status in (400, 401, 403, 404):
                        fehler(f"Stück {nr}: {e}")
                    print(f"Stück {nr}, Versuch {versuch}: {e}")
                    if versuch == 5:
                        fehler(f"Stück {nr} ging fünfmal nicht durch.")
                    time.sleep(5 * versuch)
            nr += 1
            print(f"  {stand['bytes']} / {groesse} Byte")

    try:
        fertig = portal.anfrage("POST", f"slicer/releases/{rid}/fertig", json.dumps({"freigeben": not args.nicht_freigeben}).encode())
    except PortalFehler as e:
        fehler(f"Abschließen: {e}")
    print(f"Fertig: Version {fertig['version']} (Build {fertig['build']}) ist {fertig['status']}.")


if __name__ == "__main__":
    main()
