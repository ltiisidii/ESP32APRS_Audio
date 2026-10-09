"""Draws docs/images/test-results-{light,dark}.png from the host test results.

Data: test/host/interop.sh (Dire Wolf cross test) and the WA8LMF TNC Test CD run documented in
test/host/README.md. Update the numbers below when those results change, then run:
    docker run --rm -v "${PWD}:/src" -w /src python:3.12-slim \
        sh -c "pip -q install matplotlib && python docs/images/make_test_charts.py"
"""
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle

# (label, firmware, Dire Wolf)
INTEROP = [  # 100 frames from Dire Wolf gen_packets, noise increasing frame by frame
    ("1200 AFSK", 75, 67),
    ("300 AFSK (HF)", 66, 68),
    ("9600 G3RUH", 44, 61),
    ("1200 FX.25", 97, 76),
]
WA8LMF = [  # frames decoded; Dire Wolf = its default decoder ("-P E", no bit fix-up)
    ("Track 1 (flat)", 1006, 993),
    ("Track 2 (de-emphasized)", 997, 988),
]

THEMES = {
    "light": dict(surface="#fcfcfb", ink="#0b0b0b", ink2="#52514e", muted="#898781",
                  grid="#e1e0d9", base="#c3c2b7", fw="#2a78d6", dw="#eb6834"),
    "dark": dict(surface="#1a1a19", ink="#ffffff", ink2="#c3c2b7", muted="#898781",
                 grid="#2c2c2a", base="#383835", fw="#3987e5", dw="#d95926"),
}

DPI = 100
BAR_PX = 18   # bar thickness (<= 24 px)
GAP_PX = 2    # surface gap between the two bars of a group
RADIUS_PX = 4  # rounded data end


def bar(ax, y, value, color, h, rx, aspect, surface):
    """Horizontal bar from 0 to value, rounded at the data end, square at the baseline."""
    r = min(rx, value / 2)
    ax.add_patch(FancyBboxPatch((0, y - h / 2), value, h, boxstyle=f"round,pad=0,rounding_size={r}",
                                mutation_aspect=aspect, facecolor=color, edgecolor="none", zorder=3))
    ax.add_patch(Rectangle((0, y - h / 2), min(value, 2 * r), h, facecolor=color, edgecolor="none", zorder=3))


def panel(fig, rect, rows, xmax, xticks, title, subtitle, t):
    ax = fig.add_axes(rect)
    ax.set_facecolor(t["surface"])
    n = len(rows)
    ax.set_xlim(0, xmax)
    ax.set_ylim(n - 0.5, -0.5)
    fig.canvas.draw()
    px_x = ax.transData.transform((1, 0))[0] - ax.transData.transform((0, 0))[0]
    px_y = abs(ax.transData.transform((0, 1))[1] - ax.transData.transform((0, 0))[1])
    h = BAR_PX / px_y
    off = (BAR_PX + GAP_PX) / 2 / px_y
    rx = RADIUS_PX / px_x
    aspect = px_x / px_y
    for i, (label, fw, dw) in enumerate(rows):
        for y, v, c in ((i - off, fw, t["fw"]), (i + off, dw, t["dw"])):
            bar(ax, y, v, c, h, rx, aspect, t["surface"])
            ax.text(v + xmax * 0.012, y, f"{v:,}", va="center", ha="left", fontsize=10, color=t["ink2"], zorder=4)
    ax.set_yticks(range(n))
    ax.set_yticklabels([r[0] for r in rows], fontsize=10.5, color=t["ink"])
    ax.set_xticks(xticks)
    ax.set_xticklabels([f"{x:,}" for x in xticks], fontsize=9, color=t["muted"])
    ax.tick_params(length=0, pad=6)
    ax.grid(axis="x", color=t["grid"], linewidth=0.8, zorder=0)
    for s in ("top", "right", "bottom"):
        ax.spines[s].set_visible(False)
    ax.spines["left"].set_color(t["base"])
    ax.text(0, -0.5 - 0.62, title, transform=ax.transData, fontsize=12, fontweight="bold", color=t["ink"],
            va="bottom", ha="left")
    ax.text(0, -0.5 - 0.22, subtitle, transform=ax.transData, fontsize=9.5, color=t["ink2"], va="bottom", ha="left")
    return ax


def draw(theme, path):
    t = THEMES[theme]
    plt.rcParams["font.family"] = "DejaVu Sans"
    fig = plt.figure(figsize=(9.6, 6.4), dpi=DPI, facecolor=t["surface"])
    fig.text(0.03, 0.955, "ESP32APRS_Audio demodulator vs. Dire Wolf", fontsize=14.5, fontweight="bold",
             color=t["ink"])
    # legend (identity is never color alone: swatch + name)
    lx, ly = 0.03, 0.905
    for color, name in ((t["fw"], "ESP32APRS_Audio firmware"), (t["dw"], "Dire Wolf 1.6")):
        fig.patches.append(FancyBboxPatch((lx, ly), 0.018, 0.024, boxstyle="round,pad=0,rounding_size=0.004",
                                          transform=fig.transFigure, facecolor=color, edgecolor="none"))
        fig.text(lx + 0.026, ly + 0.012, name, fontsize=10.5, color=t["ink"], va="center")
        lx += 0.30
    panel(fig, [0.25, 0.47, 0.69, 0.30], INTEROP, 110, [0, 25, 50, 75, 100],
          "Packets from another TNC, noise rising",
          "Frames decoded out of 100 (Dire Wolf gen_packets -n 100)", t)
    panel(fig, [0.25, 0.10, 0.69, 0.17], WA8LMF, 1150, [0, 250, 500, 750, 1000],
          "WA8LMF TNC Test CD, real traffic",
          "Frames decoded; Dire Wolf default decoder (-P E)", t)
    fig.text(0.03, 0.025, "Demodulator only (ESP32 ADC not included).  Source: test/host/interop.sh, "
             "test/host/README.md", fontsize=8.5, color=t["muted"])
    fig.savefig(path, dpi=DPI * 1.5, facecolor=t["surface"])
    plt.close(fig)


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    for theme in ("light", "dark"):
        out = os.path.join(here, f"test-results-{theme}.png")
        draw(theme, out)
        print("wrote", out)
