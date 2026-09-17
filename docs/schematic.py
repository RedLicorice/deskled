#!/usr/bin/env python3
"""Draws docs/schematic.svg, the DeskLED wiring diagram.

Hand-placed rather than auto-routed: edit the coordinates below and re-run.
    python3 docs/schematic.py
"""

from pathlib import Path

W, H = 1620, 1070
FONT = "DejaVu Sans, Helvetica, Arial, sans-serif"
out: list[str] = []


def add(s: str) -> None:
    out.append(s)


def wire(*points: tuple[float, float], color: str = "#222", width: float = 2.2) -> None:
    d = " ".join(("M" if i == 0 else "L") + f"{x},{y}" for i, (x, y) in enumerate(points))
    add(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linecap="round"/>')


def dot(x: float, y: float, color: str = "#222") -> None:
    add(f'<circle cx="{x}" cy="{y}" r="4.5" fill="{color}"/>')


def text(x: float, y: float, s: str, size: float = 15, anchor: str = "start",
         weight: str = "normal", color: str = "#111") -> None:
    add(f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" font-weight="{weight}" '
        f'fill="{color}" text-anchor="{anchor}">{s}</text>')


def box(x: float, y: float, w: float, h: float, label: str, sub: str = "", fill: str = "#eef3fb") -> None:
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" fill="{fill}" stroke="#2c4a7c" stroke-width="2.2"/>')
    cy = y + h / 2 + (0 if not sub else -8)
    text(x + w / 2, cy, label, 16, "middle", "bold")
    if sub:
        text(x + w / 2, cy + 20, sub, 14, "middle")


def resistor_v(x: float, y: float, ref: str, value: str, length: float = 60, label_side: int = 1) -> tuple[float, float]:
    """Vertical resistor with its top at (x, y); returns the bottom terminal."""
    body_h, body_w = 34, 14
    top_lead = (length - body_h) / 2
    wire((x, y), (x, y + top_lead))
    add(f'<rect x="{x - body_w / 2}" y="{y + top_lead}" width="{body_w}" height="{body_h}" '
        f'fill="#fff" stroke="#222" stroke-width="2.2"/>')
    wire((x, y + top_lead + body_h), (x, y + length))
    tx = x + (14 if label_side > 0 else -14)
    anchor = "start" if label_side > 0 else "end"
    text(tx, y + top_lead + 14, ref, 13, anchor)
    text(tx, y + top_lead + 30, value, 13, anchor)
    return x, y + length


def resistor_h(x: float, y: float, ref: str, value: str, length: float = 70) -> tuple[float, float]:
    """Horizontal resistor with its left terminal at (x, y); returns the right terminal."""
    body_w, body_h = 34, 14
    lead = (length - body_w) / 2
    wire((x, y), (x + lead, y))
    add(f'<rect x="{x + lead}" y="{y - body_h / 2}" width="{body_w}" height="{body_h}" '
        f'fill="#fff" stroke="#222" stroke-width="2.2"/>')
    wire((x + lead + body_w, y), (x + length, y))
    text(x + length / 2, y - 14, ref, 13, "middle")
    text(x + length / 2, y + 26, value, 13, "middle")
    return x + length, y


def capacitor_v(x: float, y: float, ref: str, value: str, length: float = 56) -> tuple[float, float]:
    """Vertical capacitor, top terminal at (x, y); returns the bottom terminal."""
    gap = 10
    mid = y + length / 2
    wire((x, y), (x, mid - gap / 2))
    wire((x - 18, mid - gap / 2), (x + 18, mid - gap / 2))
    wire((x - 18, mid + gap / 2), (x + 18, mid + gap / 2))
    wire((x, mid + gap / 2), (x, y + length))
    text(x + 24, mid - 2, ref, 13)
    text(x + 24, mid + 15, value, 13)
    return x, y + length


def ground(x: float, y: float) -> None:
    wire((x, y), (x, y + 10))
    for i, half in enumerate((16, 10, 5)):
        yy = y + 10 + i * 6
        wire((x - half, yy), (x + half, yy))


def switch(x: float, y: float, ref: str, length: float = 60) -> tuple[float, float]:
    """Push button, top terminal at (x, y); returns the bottom terminal."""
    wire((x, y), (x, y + 18))
    wire((x, y + 18), (x + 16, y + 6))          # hinged contact
    add(f'<circle cx="{x}" cy="{y + 18}" r="3.2" fill="#222"/>')
    add(f'<circle cx="{x}" cy="{y + 40}" r="3.2" fill="#222"/>')
    wire((x, y + 40), (x, y + length))
    text(x + 20, y + 34, ref, 13)
    return x, y + length


def mosfet(x: float, y: float, ref: str, part: str) -> dict[str, tuple[float, float]]:
    """N-channel MOSFET; (x, y) is the gate terminal. Returns gate/drain/source points."""
    gx, gy = x, y
    bar_x = x + 34          # gate bar
    ch_x = bar_x + 12       # channel line
    wire((gx, gy), (bar_x, gy))
    wire((bar_x, gy - 30), (bar_x, gy + 30))
    for dy in (-22, 0, 22):                      # channel segments
        wire((ch_x, gy + dy - 8), (ch_x, gy + dy + 8))
    wire((ch_x, gy - 30), (ch_x + 34, gy - 30), width=2.2)   # drain lead
    wire((ch_x, gy - 30), (ch_x, gy - 22))
    wire((ch_x, gy + 30), (ch_x + 34, gy + 30))              # source lead
    wire((ch_x, gy + 30), (ch_x, gy + 22))
    wire((ch_x, gy), (ch_x + 34, gy))                        # body tie
    wire((ch_x + 34, gy - 30), (ch_x + 34, gy + 30))
    add(f'<path d="M{ch_x + 14},{gy - 7} L{ch_x + 14},{gy + 7} L{ch_x + 2},{gy} Z" fill="#222"/>')  # body diode arrow
    text(ch_x + 46, gy - 6, ref, 14, "start", "bold")
    text(ch_x + 46, gy + 12, part, 13)
    return {"gate": (gx, gy), "drain": (ch_x + 34, gy - 30), "source": (ch_x + 34, gy + 30)}


# ---------------------------------------------------------------- layout
RAIL12, RAIL33, GNDRAIL = 70, 250, 920
RED, GREEN, BLUE, PWR, V33 = "#d33", "#1a9b4a", "#2266dd", "#d33", "#e07b00"

add(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}">')
add(f'<rect width="{W}" height="{H}" fill="#fff"/>')
text(W / 2, 38, "DeskLED — ESP8266 (TYWE3L) RGB strip controller", 24, "middle", "bold")

# rails
wire((110, RAIL12), (1425, RAIL12), color=PWR, width=2.6)
text(150, RAIL12 - 14, "+12V", 15, "start", "bold", PWR)
wire((150, GNDRAIL), (1400, GNDRAIL), width=2.6)
text(144, GNDRAIL + 6, "GND", 15, "end", "bold")
wire((320, RAIL33), (1010, RAIL33), color=V33, width=2.6)
text(314, RAIL33 - 12, "+3.3V", 15, "end", "bold", V33)

# 12V input terminals
add('<circle cx="72" cy="70" r="6" fill="#fff" stroke="#222" stroke-width="2.2"/>')
add('<circle cx="72" cy="920" r="6" fill="#fff" stroke="#222" stroke-width="2.2"/>')
wire((78, RAIL12), (110, RAIL12), color=PWR)
wire((78, GNDRAIL), (150, GNDRAIL))
text(72, 48, "PS1", 13, "middle", "bold")
text(72, 948, "12V PSU", 12, "middle")

# U2 buck converter
box(170, 120, 150, 80, "U2 buck", "12V → 3.3V")
wire((210, RAIL12), (210, 120), color=PWR)
dot(210, RAIL12, PWR)
wire((280, 200), (280, GNDRAIL))
wire((320, 160), (350, 160), (350, RAIL33), color=V33)
dot(350, RAIL33, V33)

# supply decoupling
x, y = capacitor_v(365, RAIL33, "C1", "470µF")
ground(x, y)
dot(365, RAIL33, V33)
x, y = capacitor_v(415, RAIL33, "C2", "100nF")
ground(x, y)
dot(415, RAIL33, V33)

# U1 module
UX, UY, UW, UH = 760, 380, 250, 350
box(UX, UY, UW, UH, "U1  TYWE3L", "ESP8266 · 2MB")
PIN_L = {"VCC": 410, "EN": 450, "RST": 490, "GPIO0": 530, "GPIO2": 570, "GPIO15": 610, "TXD0": 650, "RXD0": 690}
PIN_R = {"GPIO12": 430, "GPIO13": 490, "GPIO14": 550, "GND": 700}
for name, py in PIN_L.items():
    wire((UX - 20, py), (UX, py))
    text(UX + 10, py + 5, name, 13)
for name, py in PIN_R.items():
    wire((UX + UW, py), (UX + UW + 20, py))
    text(UX + UW - 10, py + 5, name, 13, "end")

# VCC to the 3.3V rail
wire((UX - 20, PIN_L["VCC"]), (710, PIN_L["VCC"]), (710, RAIL33), color=V33)
dot(710, RAIL33, V33)

# strapping pull-ups: leftmost resistor serves the lowest pin, so the wires do not cross
STRAP = [("GPIO2", 500, "R10"), ("GPIO0", 560, "R9"), ("RST", 620, "R8"), ("EN", 680, "R7")]
for pin, rx, ref in STRAP:
    _, by = resistor_v(rx, RAIL33, ref, "10k", 70, label_side=1)
    py = PIN_L[pin]
    wire((rx, by), (rx, py), (UX - 20, py))
    dot(rx, RAIL33, V33)

# EN capacitor, reset and flash buttons hang below their nets
wire((680, PIN_L["EN"]), (680, 740))
dot(680, PIN_L["EN"])
_, cy = capacitor_v(680, 740, "C3", "100nF")
ground(680, cy)

wire((620, PIN_L["RST"]), (620, 770))
dot(620, PIN_L["RST"])
_, sy = switch(620, 770, "SW1 reset")
ground(620, sy)

wire((560, PIN_L["GPIO0"]), (560, 800))
dot(560, PIN_L["GPIO0"])
_, sy = switch(560, 800, "SW2 flash")
ground(560, sy)

# GPIO15 pull-down
wire((UX - 20, PIN_L["GPIO15"]), (390, PIN_L["GPIO15"]))
_, by = resistor_v(390, PIN_L["GPIO15"], "R11", "10k", 70, label_side=-1)
ground(390, by)

# serial header
add('<rect x="160" y="790" width="170" height="120" rx="6" fill="#f6f2e8" stroke="#2c4a7c" stroke-width="2.2"/>')
text(245, 815, "J2 USB-serial", 15, "middle", "bold")
text(245, 833, "(flashing only)", 12, "middle")
for name, py in (("TXD0", 855), ("RXD0", 880), ("GND", 905)):
    wire((330, py), (350, py))
    text(322, py + 5, name, 13, "end")
wire((UX - 20, PIN_L["TXD0"]), (500, PIN_L["TXD0"]), (500, 855), (350, 855), color=BLUE)
wire((UX - 20, PIN_L["RXD0"]), (470, PIN_L["RXD0"]), (470, 880), (350, 880), color=BLUE)
wire((350, 905), (390, 905), (390, GNDRAIL))
dot(390, GNDRAIL)

# module ground
wire((UX + UW + 20, PIN_R["GND"]), (1060, PIN_R["GND"]), (1060, GNDRAIL))
dot(1060, GNDRAIL)

# output channels
CH = [
    ("GPIO12", 430, "R1", "R4", "Q1", RED, "R", 400, 1300, 1330),
    ("GPIO13", 490, "R2", "R5", "Q2", GREEN, "G", 460, 1270, 1360),
    ("GPIO14", 550, "R3", "R6", "Q3", BLUE, "B", 520, 1240, 1390),
]
STRIP_X, STRIP_Y, STRIP_W, STRIP_H = 1440, 300, 150, 280
CH_Y = {"GPIO12": 430, "GPIO13": 600, "GPIO14": 770}

for pin, _, rseries, rpull, qref, color, term, term_y, drain_x, src_x in CH:
    y_ch = CH_Y[pin]
    start_x = UX + UW + 20
    wire((start_x, PIN_R[pin]), (1090, PIN_R[pin]), (1090, y_ch))
    gx, gy = resistor_h(1090, y_ch, rseries, "100Ω", 80)
    q = mosfet(gx, gy, qref, "IRLZ34N")
    # gate pull-down, hanging off the gate wire
    pd_x = gx + 14
    dot(pd_x, gy)
    wire((pd_x, gy), (pd_x, gy + 16))
    _, by = resistor_v(pd_x, gy + 16, rpull, "10k", 56, label_side=-1)
    ground(pd_x, by)
    # drain to the strip terminal, source to ground
    dx, dy = q["drain"]
    wire((dx, dy), (drain_x, dy), (drain_x, term_y), (STRIP_X, term_y), color=color)
    sx, sy2 = q["source"]
    # each source takes its own corridor down to the ground rail, clear of the other channels
    wire((sx, sy2), (sx, sy2 + 34), (src_x, sy2 + 34), (src_x, GNDRAIL))
    dot(src_x, GNDRAIL)

# LED strip
box(STRIP_X, STRIP_Y, STRIP_W, STRIP_H, "RGB strip", "common anode", fill="#f3f0fa")
for name, py, color in (("+12V", 340, PWR), ("R", 400, RED), ("G", 460, GREEN), ("B", 520, BLUE)):
    wire((STRIP_X - 20, py), (STRIP_X, py), color=color)
    text(STRIP_X + 12, py + 5, name, 13, "start", "bold", color)
wire((1415, RAIL12), (1415, 340), (STRIP_X, 340), color=PWR)
dot(1415, RAIL12, PWR)

# notes
notes = [
    "All grounds are common: PSU −, buck −, U1 GND, MOSFET sources, J2 GND.",
    "C1/C2 sit at the module pins — the radio draws 300-400 mA spikes.",
    "GPIO0 low at power-up = flash mode (hold SW2, tap SW1).",
    "Q1-Q3 switch the low side; the strip's +12V goes straight to the PSU.",
]
for i, n in enumerate(notes):
    text(60, 975 + i * 22, "• " + n, 13, color="#333")

add("</svg>")

path = Path(__file__).with_name("schematic.svg")
path.write_text("\n".join(out), encoding="utf-8")
print(f"wrote {path} ({path.stat().st_size} bytes)")
