#!/usr/bin/env python3
"""Logo von 3 Eulen Design (Strichzeichnung, schwarz auf weiß) als SVG-Pfad für das Startbild.

    python skripte/logo_vektor.py bilder/marke/3-eulen-design.jpg 0 1000 bilder/marke/abzeichen.svg
    python skripte/logo_vektor.py bilder/marke/3-eulen-design.jpg 1010 1254 bilder/marke/schriftzug.svg

Argumente: Bild, erste und letzte Zeile (Abzeichen bzw. Schriftzug „3 EULEN DESIGN®“), Ziel.
Der Slicer zeichnet das Startbild mit NanoSVG, das keine Pixelbilder kennt – darum Pfade:
Schwellwert, Konturen mit OpenCV, leicht vereinfacht (fill-rule evenodd für die Löcher).
Nur zum Neuerzeugen nötig (Python mit OpenCV); die SVGs liegen im Repo.
"""
import sys, cv2, numpy as np
img = cv2.imread(sys.argv[1], cv2.IMREAD_GRAYSCALE)
y0, y1 = int(sys.argv[2]), int(sys.argv[3])
teil = img[y0:y1]
_, bw = cv2.threshold(teil, 128, 255, cv2.THRESH_BINARY_INV)
ys, xs = np.where(bw > 0)
x_min, x_max, y_min, y_max = xs.min(), xs.max(), ys.min(), ys.max()
bw = bw[y_min:y_max + 1, x_min:x_max + 1]
h, w = bw.shape
konturen, _ = cv2.findContours(bw, cv2.RETR_CCOMP, cv2.CHAIN_APPROX_NONE)
teile = []
for k in konturen:
    if cv2.contourArea(k) < 4:
        continue
    k = cv2.approxPolyDP(k, 0.7, True)
    if len(k) < 3:
        continue
    pts = k.reshape(-1, 2)
    teile.append("M" + " L".join(f"{x},{y}" for x, y in pts) + "Z")
d = "".join(teile)
open(sys.argv[4], "w").write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}"><path fill-rule="evenodd" fill="#000" d="{d}"/></svg>')
print(w, h, len(teile), len(d))
