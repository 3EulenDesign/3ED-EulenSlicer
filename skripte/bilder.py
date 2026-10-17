#!/usr/bin/env python3
"""Bilder des EulenSlicers erzeugen: Startbild (Splash), App-Symbol, Symbole in allen Größen,
Kopf von „Über EulenSlicer“.

    python skripte/bilder.py            # schreibt nach bilder/

Die Eulen kommen aus dem Website-Theme „drei-eulen“ (template-parts/owls.php,
gleiche Formen und Farben), der Schriftzug „EulenSlicer“ ist Poppins Bold (SIL OFL)
als Pfad – der Slicer zeichnet SVG mit NanoSVG, das kennt weder Text noch <use>,
Muster oder Beschnitt. Deshalb steht hier alles ausgeschrieben, auch die
Druckschichten der Eulen (Streifen, auf den Körper zugeschnitten).

Braucht nur zum Neuerzeugen: Python mit Pillow, fontTools, Playwright (Chromium)
und die Schrift Poppins Bold. Die erzeugten Dateien liegen im Repo (bilder/);
skripte/anwenden.py kopiert sie nach resources/images/ von OrcaSlicer.
"""

from __future__ import annotations

import io
import math
import sys
from pathlib import Path

HIER = Path(__file__).resolve().parent.parent
ZIEL = HIER / "bilder"
SCHRIFT = Path("/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf")
SCHRIFT_MITTEL = Path("/usr/share/fonts/truetype/google-fonts/Poppins-Medium.ttf")  # Zeile unter dem Schriftzug

# Farben aus drei-eulen/style.css
HELL = {
    "owl_a": "#D69A3F", "owl_b": "#4F6D8F", "owl_c": "#7C8A52",
    "face": "#F4F0E8", "beak": "#C2662B", "eye": "#22303D",
    "branch": "#6E5340", "leaf": "#93A86B", "ink": "#1E2B38", "akzent": "#4F6D8F",
}
DUNKEL = dict(HELL, face="#DCD6CB", branch="#8C6B51", ink="#E5ECF2", akzent="#9DBCE0", eye="#16202B")

KOERPER = "M -32,0 C -38,-30 -36,-60 -30,-78 L -24,-92 L -12,-80 Q 0,-84 12,-80 L 24,-92 L 30,-78 C 36,-60 38,-30 32,0 Q 0,8 -32,0 Z"


# --------------------------------------------------------------------------
# Körper als Vieleck (für die Druckschichten)
# --------------------------------------------------------------------------

def _bezier(p0, p1, p2, p3, n=24):
    out = []
    for i in range(1, n + 1):
        t = i / n
        a = (1 - t) ** 3
        b = 3 * (1 - t) ** 2 * t
        c = 3 * (1 - t) * t ** 2
        d = t ** 3
        out.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0], a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))
    return out


def _quad(p0, p1, p2, n=16):
    out = []
    for i in range(1, n + 1):
        t = i / n
        out.append(((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t ** 2 * p2[0], (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t ** 2 * p2[1]))
    return out


def koerper_vieleck():
    pt = lambda x, y: (float(x), float(y))  # noqa: E731
    p = [pt(-32, 0)]
    p += _bezier(p[-1], pt(-38, -30), pt(-36, -60), pt(-30, -78))
    p += [pt(-24, -92), pt(-12, -80)]
    p += _quad(p[-1], pt(0, -84), pt(12, -80))
    p += [pt(24, -92), pt(30, -78)]
    p += _bezier(p[-1], pt(36, -60), pt(38, -30), pt(32, 0))
    p += _quad(p[-1], pt(0, 8), pt(-32, 0))
    return p[:-1]


def _schneiden(vieleck, y, oben):
    """Sutherland-Hodgman gegen die Halbebene y' >= y (oben=False) bzw. y' <= y (oben=True)."""
    drin = (lambda q: q[1] <= y) if oben else (lambda q: q[1] >= y)
    out = []
    for i, a in enumerate(vieleck):
        b = vieleck[(i + 1) % len(vieleck)]
        if drin(a):
            out.append(a)
        if drin(a) != drin(b):
            t = (y - a[1]) / (b[1] - a[1])
            out.append((a[0] + t * (b[0] - a[0]), y))
    return out


def schichten(farbe="#000000", deckkraft=0.13):
    """Streifen wie im Theme (alle 4 Einheiten 1 hoch, bei y = 3 mod 4), auf den Körper zugeschnitten."""
    v = koerper_vieleck()
    teile = []
    for k in range(-25, 3):
        y0 = 3 + 4 * k
        s = _schneiden(_schneiden(v, y0, False), y0 + 1, True)
        if len(s) >= 3:
            punkte = " ".join(f"{x:.2f},{y:.2f}" for x, y in s)
            teile.append(f'<polygon points="{punkte}" fill="{farbe}" fill-opacity="{deckkraft}"/>')
    return "".join(teile)


def eule(farbe, f, wach=True, transform=""):
    """Eine Eule in ihren eigenen Koordinaten (-40 … 40, -96 … 10)."""
    t = f' transform="{transform}"' if transform else ""
    augen = ""
    for x in (-12, 12):
        if wach:
            augen += (f'<circle cx="{x}" cy="-57" r="4.6" fill="{f["eye"]}"/>'
                      f'<circle cx="{x + 1.6}" cy="-58.6" r="1.4" fill="#FFFFFF"/>')
        else:
            augen += (f'<path d="M {x - 6},-57 Q {x},-52 {x + 6},-57" fill="none" stroke="{f["eye"]}" '
                      f'stroke-width="2.4" stroke-linecap="round"/>')
    return (
        f"<g{t}>"
        f'<path d="{KOERPER}" fill="{farbe}"/>'
        f"{schichten()}"
        f'<ellipse cx="0" cy="-26" rx="18" ry="20" fill="#FFFFFF" fill-opacity="0.18"/>'
        f'<circle cx="-12" cy="-58" r="11" fill="{f["face"]}"/>'
        f'<circle cx="12" cy="-58" r="11" fill="{f["face"]}"/>'
        f'<path d="M -4,-49 L 4,-49 L 0,-41 Z" fill="{f["beak"]}"/>'
        f'<path d="M -14,-1 L -14,3 M -10,-1 L -10,4 M -6,-1 L -6,3 M 6,-1 L 6,3 M 10,-1 L 10,4 M 14,-1 L 14,3" '
        f'fill="none" stroke="{f["beak"]}" stroke-width="2.2" stroke-linecap="round"/>'
        f"{augen}"
        "</g>"
    )


# --------------------------------------------------------------------------
# Schriftzug als Pfad
# --------------------------------------------------------------------------

def schriftzug(teile, groesse, mitte_x, grundlinie, schrift=SCHRIFT, links=None):
    """teile = [(text, farbe), …] → (SVG, Breite). Zentriert um mitte_x (oder linksbündig ab links)."""
    from fontTools.pens.svgPathPen import SVGPathPen
    from fontTools.pens.transformPen import TransformPen
    from fontTools.ttLib import TTFont

    font = TTFont(str(schrift))
    glyphen = font.getGlyphSet()
    cmap = font.getBestCmap()
    upm = font["head"].unitsPerEm
    mass = groesse / upm
    breite = sum(font["hmtx"][cmap[ord(c)]][0] for text, _ in teile for c in text) * mass
    x = mitte_x - breite / 2 if links is None else links
    svg = ""
    for text, farbe in teile:
        pen = SVGPathPen(glyphen, ntos=lambda v: f"{v:.2f}")
        for c in text:
            name = cmap[ord(c)]
            glyphen[name].draw(TransformPen(pen, (mass, 0, 0, -mass, x, grundlinie)))
            x += font["hmtx"][name][0] * mass
        svg += f'<path d="{pen.getCommands()}" fill="{farbe}"/>'
    return svg, breite


# --------------------------------------------------------------------------
# Bilder
# --------------------------------------------------------------------------

def splash(f):
    """480 × 480, durchsichtig: drei Eulen auf dem Ast, darunter „EulenSlicer“.
    OrcaSlicer schreibt die Version bei 72 % und den Fortschritt bei 88 % der Höhe darunter."""
    s = 1.06
    dx, dy = 240 - 180 * s, 70
    szene = (
        f'<g transform="translate({dx:.2f} {dy}) scale({s})">'
        f'<path d="M 14,164 C 95,156 200,171 346,157" fill="none" stroke="{f["branch"]}" stroke-width="9" stroke-linecap="round"/>'
        f'<path d="M 318,159 Q 326,150 334,145" fill="none" stroke="{f["branch"]}" stroke-width="4" stroke-linecap="round"/>'
        f'<ellipse cx="339" cy="142" rx="7" ry="3.5" fill="{f["leaf"]}" transform="rotate(-35 339 142)"/>'
        + eule(f["owl_a"], f, wach=False, transform="translate(88 160)")
        + eule(f["owl_b"], f, wach=True, transform="translate(180 161) scale(1.15)")
        + eule(f["owl_c"], f, wach=False, transform="translate(272 160)")
        + "</g>"
    )
    text, _ = schriftzug([("Eulen", f["ink"]), ("Slicer", f["akzent"])], 46, 240, 322)
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="480" height="480" viewBox="0 0 480 480">{szene}{text}</svg>'


def ueber(f):
    """560 × 125 für „Über EulenSlicer“ (ersetzt OrcaSlicer_about*.svg): Eule auf der Kachel,
    „EulenSlicer“ und „3 Eulen Design“, unten eine blaue Linie. Rechts bleibt frei – dort
    schreibt der Dialog die Version hin."""
    kachel = '<rect x="20" y="21.22" width="80.56" height="80.56" rx="9.64" ry="9.64" fill="#E3ECF3"/>'
    s = 80.56 / 64
    owl = eule(HELL["owl_b"], HELL, wach=True, transform=f"translate({20 + 40.28:.2f} {21.22 + 58 * s:.2f}) scale({0.5 * s:.4f})")
    name, _ = schriftzug([("Eulen", f["ink"]), ("Slicer", f["akzent"])], 40, 0, 68, links=120)
    unter, _ = schriftzug([("3 Eulen Design", f["unter"])], 16, 0, 92, schrift=SCHRIFT_MITTEL, links=121)
    linie = f'<rect x="20" y="123" width="520" height="2" fill="{f["linie"]}"/>'
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="560" height="125" viewBox="0 0 560 125">'
            f"{linie}{kachel}{owl}{name}{unter}</svg>")


def symbol(kachel=True, grau=False):
    """App-Symbol 64 × 64: wache Schiefer-Eule auf heller Kachel (oder ohne Kachel)."""
    f = dict(HELL)
    koerper = f["owl_b"]
    kachel_farbe = "#E3ECF3"
    if grau:
        f.update(face="#EEEEEE", beak="#8A8A8A", eye="#2B2B2B")
        koerper = "#5E5E5E"
        kachel_farbe = "#E6E6E6"
    hinter = f'<rect width="64" height="64" rx="14" fill="{kachel_farbe}"/>' if kachel else ""
    # Eule (Höhe ~106 Einheiten) auf ~54 px, unten bündig mit etwas Rand
    m = 0.5 if kachel else 0.56
    ty = 58 if kachel else 60
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">'
        f"{hinter}"
        + eule(koerper, f, wach=True, transform=f"translate(32 {ty}) scale({m})")
        + "</svg>"
    )


def rendern(svgs: dict[str, tuple[str, int]]) -> dict[str, bytes]:
    """SVG → PNG in Chromium (wie im Browser, mit Kantenglättung), durchsichtiger Hintergrund."""
    from playwright.sync_api import sync_playwright

    out = {}
    with sync_playwright() as p:
        b = p.chromium.launch()
        page = b.new_page(device_scale_factor=1)
        for name, (svg, groesse) in svgs.items():
            page.set_viewport_size({"width": groesse, "height": groesse})
            page.set_content(
                f'<html><body style="margin:0;background:transparent">'
                f'<img src="data:image/svg+xml;base64,{__import__("base64").b64encode(svg.encode()).decode()}" '
                f'width="{groesse}" height="{groesse}" style="display:block"></body></html>'
            )
            page.wait_for_timeout(50)
            out[name] = page.screenshot(omit_background=True, clip={"x": 0, "y": 0, "width": groesse, "height": groesse})
        b.close()
    return out


def main() -> None:
    try:
        from PIL import Image
    except ImportError:
        sys.exit("Pillow fehlt (pip install pillow).")
    for schrift in (SCHRIFT, SCHRIFT_MITTEL):
        if not schrift.is_file():
            sys.exit(f"Schrift fehlt: {schrift} (Poppins, SIL OFL)")
    ZIEL.mkdir(exist_ok=True)

    dateien = {
        "splash_logo.svg": splash(HELL),
        "splash_logo_dark.svg": splash(DUNKEL),
        "OrcaSlicer.svg": symbol(),
        "OrcaSlicer_gray.svg": symbol(grau=True),
        "OrcaSlicer_about.svg": ueber(dict(HELL, unter="#6B7886", linie="#50708F")),
        "OrcaSlicer_about_dark.svg": ueber(dict(DUNKEL, unter="#A7B5C2", linie="#50708F")),
    }
    for name, svg in dateien.items():
        (ZIEL / name).write_text(svg, encoding="utf-8")
        print(f"  {name}")

    kachel = symbol()
    ohne = symbol(kachel=False)
    grau = symbol(grau=True)
    pngs = rendern({
        "k256": (kachel, 256), "k192": (kachel, 192), "k184": (kachel, 184), "k154": (kachel, 154), "k128": (kachel, 128),
        "k64": (kachel, 64), "k48": (kachel, 48), "k32": (kachel, 32), "k24": (kachel, 24), "k16": (kachel, 16),
        "o192": (ohne, 192), "g192": (grau, 192),
        "vorschau_splash": (splash(HELL), 480), "vorschau_splash_dunkel": (splash(DUNKEL), 480),
    })
    bild = {k: Image.open(io.BytesIO(v)).convert("RGBA") for k, v in pngs.items()}

    def speichern(name, img):
        img.save(ZIEL / name)
        print(f"  {name} {img.size[0]}×{img.size[1]}")

    def gepolstert(img, groesse, rand):
        """Kachel mit Rand wie bei OrcaSlicer_192px.png (≈ 9 %)."""
        innen = groesse - 2 * rand
        neu = Image.new("RGBA", (groesse, groesse), (0, 0, 0, 0))
        neu.paste(img.resize((innen, innen), Image.LANCZOS), (rand, rand))
        return neu

    speichern("OrcaSlicer.png", bild["k154"])
    speichern("OrcaSlicerTitle.png", bild["k154"])
    speichern("OrcaSlicer_154.png", bild["k154"])
    speichern("OrcaSlicer_154_title.png", bild["k184"])
    speichern("OrcaSlicer_128px.png", bild["k128"])
    speichern("OrcaSlicer-mac_128px.png", bild["k128"])
    speichern("OrcaSlicer_64.png", bild["k64"])
    speichern("OrcaSlicer_32px.png", bild["k32"])
    speichern("OrcaSlicer_192px.png", gepolstert(bild["k256"], 192, 17))
    durch = bild["o192"].copy()
    durch.putalpha(durch.getchannel("A").point(lambda a: int(a * 0.75)))
    speichern("OrcaSlicer_192px_transparent.png", durch)
    speichern("OrcaSlicer_192px_grayscale.png", bild["g192"])

    # Windows-Symbole (mehrere Größen in einer Datei; die 256er als PNG)
    groessen = [16, 24, 32, 48, 64, 128, 256]
    bild["k256"].save(ZIEL / "OrcaSlicer.ico", sizes=[(g, g) for g in groessen],
                      append_images=[bild[f"k{g}"] for g in groessen if f"k{g}" in bild and g != 256])
    print("  OrcaSlicer.ico " + ", ".join(map(str, groessen)))
    bild["k154"].save(ZIEL / "OrcaSlicerTitle.ico", sizes=[(154, 154)])
    print("  OrcaSlicerTitle.ico 154")
    bild["k256"].save(ZIEL / "OrcaSlicer-mac_256px.ico", sizes=[(256, 256)])
    print("  OrcaSlicer-mac_256px.ico 256")

    # Vorschau (nicht ins Programm)
    vorschau = HIER / "bilder" / "vorschau"
    vorschau.mkdir(exist_ok=True)
    for name in ("vorschau_splash", "vorschau_splash_dunkel"):
        hinter = Image.new("RGBA", (480, 480), (255, 255, 255, 255) if name.endswith("splash") else (45, 45, 49, 255))
        hinter.alpha_composite(bild[name])
        hinter.convert("RGB").save(vorschau / f"{name}.png")
    print("  vorschau/…")


if __name__ == "__main__":
    main()
