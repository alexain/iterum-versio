#!/usr/bin/env python3
"""Generates the Iterum Versio panel (10HP Eurorack) as SVG, in millimetres.

Knob, jack, switch, LED and button positions follow the Versio layout
(measured from a reference panel photo): check them against a real panel
or an official drill template before having a panel made.

Usage: python3 make_panel.py  ->  iterum_versio_panel.svg (preview with
components) and iterum_versio_panel_print.svg (artwork + hole guides only).
No dependencies beyond the Python standard library.
"""
import math

W, H = 50.5, 128.5          # 10HP
BG, FG, DIM = "#0d0d0d", "#f2efe8", "#8a8780"
ACCENT = "#e8b04a"          # amber accent
FONT = "Inter, 'DejaVu Sans', sans-serif"

# --- positions (mm) --------------------------------------------------------
KNOBS = {  # n: (x, y, name, knob cap radius)
    1: (8.0, 22.0, "MIX", 4.3),
    5: (42.5, 22.0, "FEEDBACK", 4.3),
    3: (25.25, 32.0, "TIME", 4.3),
    2: (8.0, 42.0, "SKEW", 4.3),
    6: (42.5, 42.0, "RANGE", 4.3),
    4: (25.25, 52.0, "FILTER", 4.3),
    7: (42.5, 62.5, "REVERSE", 4.3),
}
LEDS = [(16.6, 15.3), (21.6, 15.3), (29.0, 15.3), (34.0, 15.3)]
SWITCHES = [(8.0, 59.0, "ROUTE", ("PING", "STEREO", "CROSS")),
            (8.0, 68.5, "CHANGE", ("TAPE", "FADE", "JUMP"))]
BUTTON = (25.25, 70.0)
JX = [6.2, 18.9, 31.6, 44.3]
JY = [85.5, 98.5, 112.0]
JACK_LABELS = [["MIX", "TIME", "FDBK", "REV"],
               ["SKEW", "FILTER", "RANGE", "CLOCK"],
               ["IN L", "IN R", "OUT L", "OUT R"]]
HOLES = [(7.5, 3.0), (43.0, 3.0), (7.5, 125.5), (43.0, 125.5)]


def text(x, y, s, size, weight=500, fill=FG, anchor="middle", ls=0.0,
         italic=False):
    st = ' font-style="italic"' if italic else ""
    return (f'<text x="{x:.2f}" y="{y:.2f}" font-family="{FONT}" '
            f'font-size="{size}" font-weight="{weight}" fill="{fill}" '
            f'text-anchor="{anchor}" letter-spacing="{ls}"{st}>{s}</text>')


def arc(cx, cy, r, a0, a1):
    """Arc in degrees (0 = 12 o'clock, clockwise)."""
    def p(a):
        t = math.radians(a - 90)
        return cx + r * math.cos(t), cy + r * math.sin(t)
    x0, y0 = p(a0)
    x1, y1 = p(a1)
    large = 1 if (a1 - a0) % 360 > 180 else 0
    return f"M{x0:.3f},{y0:.3f} A{r},{r} 0 {large} 1 {x1:.3f},{y1:.3f}"


def polar(cx, cy, r, a):
    t = math.radians(a - 90)
    return cx + r * math.cos(t), cy + r * math.sin(t)


def knob_scale(n, x, y, r):
    """Scale around a knob: 300° of travel (from -150° to +150°)."""
    out = []
    R = r + 1.25
    out.append(f'<path d="{arc(x, y, R, -150, 150)}" fill="none" stroke="{DIM}"'
               f' stroke-width="0.25"/>')
    if n == 4:     # FILTER: LP on the left, off at centre, HP on the right
        out.append(f'<path d="{arc(x, y, R, -150, -10)}" fill="none" '
                   f'stroke="{FG}" stroke-width="0.45"/>')
        out.append(f'<path d="{arc(x, y, R, 10, 150)}" fill="none" '
                   f'stroke="{FG}" stroke-width="0.45"/>')
        # labels below the arc ends (clear of the ROUTE switch)
        out.append(text(x - 3.9, y + 7.7, "LP", 2.1, 700))
        out.append(text(x + 3.9, y + 7.7, "HP", 2.1, 700))
    elif n == 2:   # SKEW, bipolar
        cx, cy = polar(x, y, R, 0)
        out.append(f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="0.45" fill="{FG}"/>')
        lx, ly = polar(x, y, R + 2.2, -128)
        rx, ry = polar(x, y, R + 2.2, 128)
        out.append(text(lx, ly + 0.7, "R−", 1.95, 700))
        out.append(text(rx, ry + 0.7, "L−", 1.95, 700))
    elif n == 6:   # RANGE: 4 zones
        for k in range(4):
            a0 = -150 + k * 75 + 4
            a1 = a0 + 67
            out.append(f'<path d="{arc(x, y, R, a0, a1)}" fill="none" '
                       f'stroke="{FG}" stroke-width="0.45"/>')
        for a, s in [(-150, "ms"), (150, "60s")]:
            px, py = polar(x, y, R + 2.4, a + (14 if a < 0 else -14))
            out.append(text(px, py + 0.7, s, 1.9, 600, DIM))
    elif n == 7:   # REVERSE: probability
        for a, s in [(-150, "FWD"), (150, "REV")]:
            px, py = polar(x, y, R + 2.6, a + (16 if a < 0 else -16))
            out.append(text(px, py + 0.7, s, 1.9, 700))
        # dots growing towards REV
        for k in range(9):
            a = -150 + k * 37.5
            px, py = polar(x, y, R, a)
            out.append(f'<circle cx="{px:.2f}" cy="{py:.2f}" r="{0.18 + k * 0.04:.2f}"'
                       f' fill="{FG}"/>')
    elif n == 5:   # FEEDBACK: zone above unity
        out.append(f'<path d="{arc(x, y, R, 118, 150)}" fill="none" '
                   f'stroke="{ACCENT}" stroke-width="0.7"/>')
        px, py = polar(x, y, R + 2.3, 136)
        out.append(text(px, py + 0.7, "∞", 2.4, 700, ACCENT))
    elif n == 3:   # TIME: notch at noon (1/1 when clocked)
        cx, cy = polar(x, y, R, 0)
        out.append(f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="0.45" fill="{FG}"/>')
        for a, s in [(-150, "÷"), (150, "×")]:
            px, py = polar(x, y, R + 2.2, a + (14 if a < 0 else -14))
            out.append(text(px, py + 0.8, s, 2.2, 700))
    elif n == 1:   # MIX (labels pulled in to stay inside the edge)
        out.append(text(x - 4.6, y + 6.3, "DRY", 1.9, 700))
        out.append(text(x + 4.6, y + 6.3, "WET", 1.9, 700))
    return "\n".join(out)


def knob_label(n, x, y, r, name):
    ty = y + r + 4.3
    return (f'<rect x="{x - 1.6:.2f}" y="{ty - 2.55:.2f}" width="3.2" height="3.2"'
            f' rx="0.5" fill="{FG}"/>' + text(x, ty - 0.3, str(n), 2.3, 800, BG) +
            text(x, ty + 3.3, name, 2.3, 700, FG, ls=0.15))


def knob_hw(x, y, r):
    """Knob cap (preview only)."""
    return (f'<circle cx="{x}" cy="{y}" r="{r}" fill="#1c1c1c" stroke="{FG}" '
            f'stroke-width="0.45"/>'
            f'<line x1="{x}" y1="{y}" x2="{x}" y2="{y - r + 0.8}" stroke="{FG}" '
            f'stroke-width="0.6" stroke-linecap="round"/>')


def hexnut(x, y, r, fill="#9a9a9a"):
    pts = " ".join(f"{x + r * math.cos(math.radians(60 * i)):.2f},"
                   f"{y + r * math.sin(math.radians(60 * i)):.2f}" for i in range(6))
    return f'<polygon points="{pts}" fill="{fill}" stroke="#555" stroke-width="0.2"/>'


def build(preview=True):
    s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" '
         f'viewBox="0 0 {W} {H}">',
         f'<rect width="{W}" height="{H}" fill="{BG}"/>']

    # title
    s.append(text(W / 2, 7.4, "ITERUM", 5.2, 800, FG, ls=0.9))
    s.append(text(W / 2, 10.9, "versio", 3.0, 400, DIM, ls=1.6, italic=True))

    # LEDs: L R (repeats), FILT, FRZ
    s.append(text((LEDS[0][0] + LEDS[1][0]) / 2, 12.9, "L · R", 1.85, 600, DIM, ls=0.2))
    s.append(text(LEDS[2][0], 12.9, "FILT", 1.75, 600, DIM))
    s.append(text(LEDS[3][0], 12.9, "FRZ", 1.75, 600, DIM))
    for (x, y) in LEDS:
        if preview:
            s.append(f'<circle cx="{x}" cy="{y}" r="1.55" fill="#cfcfcf" '
                     f'stroke="#777" stroke-width="0.2"/>')
        else:
            s.append(f'<circle cx="{x}" cy="{y}" r="1.55" fill="none" '
                     f'stroke="#ff2a6d" stroke-width="0.1"/>')

    # knob
    for n, (x, y, name, r) in KNOBS.items():
        s.append(knob_scale(n, x, y, r))
        s.append(knob_label(n, x, y, r, name))
        s.append(knob_hw(x, y, r) if preview else
                 f'<circle cx="{x}" cy="{y}" r="3.6" fill="none" stroke="#ff2a6d" '
                 f'stroke-width="0.1"/>')

    # 3-position switches
    for (x, y, title, opts) in SWITCHES:
        s.append(text(x + 4.3, y - 2.7, title, 1.8, 800, FG, "start", 0.1))
        for k, o in enumerate(opts):
            s.append(text(x + 4.3, y - 0.4 + k * 2.05, o, 1.7, 500,
                          FG if k != 1 else DIM, "start", 0.05))
        s.append(f'<line x1="{x + 3.2}" y1="{y - 1.0}" x2="{x + 3.2}" '
                 f'y2="{y + 3.8}" stroke="{DIM}" stroke-width="0.2"/>')
        if preview:
            s.append(hexnut(x, y, 3.0))
            s.append(f'<circle cx="{x}" cy="{y}" r="1.1" fill="#e9e9e9"/>')
        else:
            s.append(f'<circle cx="{x}" cy="{y}" r="3.0" fill="none" '
                     f'stroke="#ff2a6d" stroke-width="0.1"/>')

    # FSU button = FREEZE
    bx, by = BUTTON
    s.append(f'<rect x="{bx - 5.2}" y="{by + 4.0}" width="10.4" height="3.2" '
             f'rx="1.6" fill="{ACCENT}"/>')
    s.append(text(bx, by + 6.35, "FREEZE", 2.0, 800, BG, ls=0.25))
    s.append(f'<circle cx="{bx}" cy="{by}" r="3.4" fill="none" stroke="{FG}" '
             f'stroke-width="0.35"/>')
    if preview:
        s.append(f'<circle cx="{bx}" cy="{by}" r="2.7" fill="#111" '
                 f'stroke="#666" stroke-width="0.25"/>')

    # separator above the jack area
    s.append(f'<line x1="4" y1="77.2" x2="{W - 4}" y2="77.2" stroke="{DIM}" '
             f'stroke-width="0.2"/>')

    # jack
    for row, y in enumerate(JY):
        for col, x in enumerate(JX):
            lab = JACK_LABELS[row][col]
            is_out = row == 2 and col >= 2
            is_clk = lab == "CLOCK"
            ly = y - 5.2
            if is_out:
                s.append(f'<rect x="{x - 5.0:.2f}" y="{ly - 2.45:.2f}" width="10.0" '
                         f'height="3.3" rx="0.5" fill="{FG}"/>')
                s.append(text(x, ly, lab, 2.15, 800, BG, ls=0.1))
            else:
                col_ = ACCENT if is_clk else FG
                s.append(text(x, ly, lab, 2.15, 700, col_, ls=0.1))
            if preview:
                s.append(hexnut(x, y, 3.9, "#a8a8a8"))
                s.append(f'<circle cx="{x}" cy="{y}" r="2.4" fill="#2a2a2a" '
                         f'stroke="#cfcfcf" stroke-width="0.35"/>')
                s.append(f'<circle cx="{x}" cy="{y}" r="1.4" fill="#050505"/>')
            else:
                s.append(f'<circle cx="{x}" cy="{y}" r="3.0" fill="none" '
                         f'stroke="#ff2a6d" stroke-width="0.1"/>')
    # normalisation IN L -> IN R
    y = JY[2]
    s.append(f'<path d="M{JX[0] + 4.3},{y + 1.6} L{JX[1] - 4.6},{y + 1.6}" '
             f'stroke="{FG}" stroke-width="0.35"/>')
    s.append(f'<path d="M{JX[1] - 4.6},{y + 1.6} l-1.1,-0.7 l0,1.4 z" fill="{FG}"/>')


    # mounting holes
    for (x, y) in HOLES:
        if preview:
            s.append(f'<ellipse cx="{x}" cy="{y}" rx="2.6" ry="1.6" fill="#3a3a3a"/>')
            s.append(f'<circle cx="{x}" cy="{y}" r="1.4" fill="#9a9a9a"/>')
            s.append(f'<line x1="{x - 0.9}" y1="{y}" x2="{x + 0.9}" y2="{y}" '
                     f'stroke="#555" stroke-width="0.3"/>')
        else:
            s.append(f'<ellipse cx="{x}" cy="{y}" rx="2.6" ry="1.6" fill="none" '
                     f'stroke="#ff2a6d" stroke-width="0.1"/>')

    s.append("</svg>")
    return "\n".join(x for x in s if x)


if __name__ == "__main__":
    import os
    d = os.path.dirname(os.path.abspath(__file__))
    open(os.path.join(d, "iterum_versio_panel.svg"), "w").write(build(True))
    open(os.path.join(d, "iterum_versio_panel_print.svg"), "w").write(build(False))
    print("ok")
