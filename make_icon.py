#!/usr/bin/env python3
"""
make_icon.py - Dessine assets/ax25chess.ico et ax25chess.png.

L'icone est construite par programme plutot que dessinee a la main : chaque
taille du .ico est rendue a sa propre resolution puis suréchantillonnée, au
lieu d'etre reduite depuis un seul bitmap. Le detail disparait
progressivement quand la place manque, sinon les petites tailles tournent a
la bouillie.

Motif : un fragment d'echiquier surmonte d'un mat d'antenne et de ses ondes,
dans l'ambre du retro-eclairage de cadran qui sert d'accent a toute
l'application.
"""

from __future__ import annotations

import os
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("Pillow est requis :  pip install pillow")

CHASSIS = (19, 26, 33, 255)
LIGHT_SQ = (228, 220, 201, 255)
DARK_SQ = (92, 124, 111, 255)
AMBER = (232, 163, 61, 255)

SIZES = [16, 24, 32, 48, 64, 128, 256]
SS = 4                      # facteur de suréchantillonnage


def draw_icon(size: int) -> Image.Image:
    px = size * SS
    img = Image.new("RGBA", (px, px), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    radius = int(px * 0.20)
    d.rounded_rectangle([0, 0, px - 1, px - 1], radius=radius, fill=CHASSIS)

    # --- fragment d'echiquier, en bas a gauche ---------------------------
    margin = px * 0.12
    board = px * 0.52
    cell = board / 4.0
    top = px - margin - board
    for row in range(4):
        for col in range(4):
            light = (row + col) % 2 == 0
            x0 = margin + col * cell
            y0 = top + row * cell
            d.rectangle([x0, y0, x0 + cell, y0 + cell],
                        fill=LIGHT_SQ if light else DARK_SQ)

    # --- mat d'antenne ---------------------------------------------------
    mast_x = px * 0.75
    mast_top = px * 0.30
    mast_bottom = px - margin
    width = max(SS, int(px * 0.045))
    d.line([(mast_x, mast_top), (mast_x, mast_bottom)], fill=AMBER, width=width)
    # embase
    foot = px * 0.055
    d.line([(mast_x - foot, mast_bottom), (mast_x + foot, mast_bottom)],
           fill=AMBER, width=width)

    # --- ondes : abandonnees quand elles ne seraient plus lisibles -------
    arcs = []
    if size >= 24:
        arcs.append(px * 0.10)
    if size >= 32:
        arcs.append(px * 0.175)
    stroke = max(SS, int(px * 0.035))
    for r in arcs:
        box = [mast_x - r, mast_top - r + px * 0.03,
               mast_x + r, mast_top + r + px * 0.03]
        d.arc(box, start=205, end=335, fill=AMBER, width=stroke)

    return img.resize((size, size), Image.LANCZOS)


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    assets = os.path.join(here, "assets")
    os.makedirs(assets, exist_ok=True)

    frames = [draw_icon(s) for s in SIZES]
    ico = os.path.join(assets, "ax25chess.ico")
    frames[-1].save(ico, format="ICO",
                    sizes=[(s, s) for s in SIZES], append_images=frames[:-1])
    png = os.path.join(assets, "ax25chess.png")
    frames[-1].save(png, format="PNG")

    print(f"ecrit : {ico}")
    print(f"ecrit : {png}")
    print("tailles :", ", ".join(f"{s}x{s}" for s in SIZES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
