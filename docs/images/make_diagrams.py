#!/usr/bin/env python3
"""Regenerates the SVG diagrams in this folder.

The remote illustration reads the DEFAULT_MAP table straight from the firmware
(firmware/ble_remote_bridge/ble_remote_bridge.ino), so the colours and codes can never
drift away from what the code really does.

    python3 docs/images/make_diagrams.py

To draw another remote: copy the LAYOUT list below, change the key codes / positions /
labels, and change the output file name.  The drawing is a schematic, not to scale.
"""
import re
from html import escape
from pathlib import Path

HERE = Path(__file__).resolve().parent
INO = HERE.parent.parent / "firmware" / "ble_remote_bridge" / "ble_remote_bridge.ino"

# ----------------------------------------------------------------------------
# Read DEFAULT_MAP: key code -> (code sent to TV, code sent to Chromecast)
# ----------------------------------------------------------------------------
src = INO.read_text()
block = src[src.index("static const MapEntry DEFAULT_MAP"):]
block = block[: block.index("};")]
MAP = {
    int(m.group(1), 16): (int(m.group(2), 16), int(m.group(3), 16))
    for m in re.finditer(r"\{0x([0-9A-Fa-f]{4}), 0x([0-9A-Fa-f]{4}), 0x([0-9A-Fa-f]{4})\}", block)
}

FONT = "'Segoe UI', 'Helvetica Neue', Helvetica, Arial, sans-serif"
MONO = "Consolas, Menlo, 'DejaVu Sans Mono', monospace"
C_BOTH, C_CC, C_TV, C_IR = "#2e8b57", "#2b6cb0", "#b85c00", "#8a8f98"
INK, MUTED = "#1f2933", "#52606d"


def route(code):
    tv, cc = MAP.get(code, (code, 0))  # buttons not in the table go to the TV unchanged
    if tv and cc:
        return C_BOTH
    if cc:
        return C_CC
    if tv:
        return C_TV
    return C_IR


# ----------------------------------------------------------------------------
# TCL RC902V schematic layout: (code, label, centre x, centre y, width, height)
# Position of the "#n" keys is illustrative - n is the order in which the key
# appeared in the capture (top-left to bottom-right).
# ----------------------------------------------------------------------------
LAYOUT = [
    (0x00E2, "Mute", 120, 165, 64, 46),
    (0x0059, "#2", 190, 165, 64, 46),
    (0x0221, "Mic", 260, 165, 64, 46),
    (0x0055, "#4", 330, 165, 64, 46),
    (0x0042, "Up", 225, 250, 70, 46),
    (0x0044, "Left", 150, 305, 70, 46),
    (0x0041, "OK", 225, 305, 70, 46),
    (0x0045, "Right", 300, 305, 70, 46),
    (0x0043, "Down", 225, 360, 70, 46),
    (0x0224, "Back", 150, 440, 80, 46),
    (0x0223, "Home", 300, 440, 80, 46),
    (0x005B, "#12", 150, 505, 80, 46),
    (0x0049, "#13", 300, 505, 80, 46),
    (0x00E9, "Vol +", 150, 580, 80, 46),
    (0x009C, "Ch +", 300, 580, 80, 46),
    (0x0050, "#16", 225, 635, 80, 46),
    (0x00EA, "Vol \u2212", 150, 690, 80, 46),
    (0x009D, "Ch \u2212", 300, 690, 80, 46),
    (0x003D, "#19", 130, 775, 76, 46),
    (0x0052, "#20", 225, 775, 76, 46),
    (0x003A, "#21", 320, 775, 76, 46),
    (0x0051, "#22", 130, 835, 76, 46),
    (0x005F, "#23", 225, 835, 76, 46),
    (0x003F, "#24", 320, 835, 76, 46),
    (0x0058, "#25", 130, 895, 76, 46),
    (0x0057, "#26", 225, 895, 76, 46),
    (0x0053, "#27", 320, 895, 76, 46),
]


def key(code, label, cx, cy, w, h):
    fill = route(code)
    x, y = cx - w / 2, cy - h / 2
    return (
        f'<rect x="{x:.0f}" y="{y:.0f}" width="{w}" height="{h}" rx="9" fill="{fill}"/>'
        f'<text x="{cx}" y="{cy - 3}" text-anchor="middle" font-family="{FONT}" font-size="14" '
        f'font-weight="700" fill="#fff">{escape(label)}</text>'
        f'<text x="{cx}" y="{cy + 14}" text-anchor="middle" font-family="{MONO}" font-size="11.5" '
        f'fill="#fff" fill-opacity="0.92">0x{code:04X}</text>'
    )


def remote_svg():
    DY = 40  # vertical room for the Power key above the first row
    W, H = 800, 990 + DY
    p = []
    p.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
             f'role="img" aria-label="Schematic of the TCL RC902V remote with the default routing of each key">')
    p.append(f'<rect width="{W}" height="{H}" fill="#ffffff"/>')
    p.append(f'<text x="30" y="40" font-family="{FONT}" font-size="22" font-weight="700" fill="{INK}">'
             f'TCL RC902V &#8211; default key routing</text>')
    p.append(f'<text x="30" y="62" font-family="{FONT}" font-size="13" fill="{MUTED}">'
             f'Schematic, not to scale. Codes are what the remote sends over Bluetooth.</text>')
    # body
    p.append(f'<rect x="55" y="90" width="340" height="{885 + DY}" rx="46" fill="#eef1f5" stroke="#b8c0cc" stroke-width="2"/>')
    # IR window
    p.append('<rect x="190" y="104" width="70" height="10" rx="5" fill="#3b4252"/>')
    # d-pad ring behind the arrow keys
    p.append(f'<circle cx="225" cy="{305 + DY}" r="102" fill="#dfe4ec" stroke="#c3cad6" stroke-width="2"/>')
    for code, label, cx, cy, w, h in LAYOUT:
        p.append(key(code, label, cx, cy + DY, w, h))
    # callouts on the left of the body would crowd it; use the right-hand panel instead
    px = 430
    p.append(f'<text x="{px}" y="120" font-family="{FONT}" font-size="16" font-weight="700" fill="{INK}">Legend</text>')
    legend = [
        (C_BOTH, "TV and Chromecast", "D-pad and OK reach both devices"),
        (C_CC, "Chromecast only", "Back and Home - the TV stays on its HDMI input"),
        (C_TV, "TV only", "Volume, Mute, Ch\u00b1, mic and the TCL-specific keys"),
        (C_IR, "IR only", "Power - never reaches the bridge"),
    ]
    y = 150
    for color, title, sub in legend:
        p.append(f'<rect x="{px}" y="{y - 14}" width="26" height="20" rx="5" fill="{color}"/>')
        p.append(f'<text x="{px + 38}" y="{y}" font-family="{FONT}" font-size="14" font-weight="700" fill="{INK}">{escape(title)}</text>')
        p.append(f'<text x="{px + 38}" y="{y + 18}" font-family="{FONT}" font-size="12" fill="{MUTED}">{escape(sub)}</text>')
        y += 52
    notes = [
        ("Power", "The remote sends Power as an infrared signal,", "not over Bluetooth, so the bridge never sees it."),
        ("#n", "TCL-specific keys. n is the position in the", "capture order (top-left to bottom-right)."),
        ("", "Their function depends on the TV model; press a", "key and the Serial Monitor prints its code."),
        ("Input", "The Input key is one of the TCL keys. Assign it", "with  inputkey 0x....  (see the README)."),
        ("", "Any TCL key can also become the TV/Chromecast", "switch with  switchkey 0x....  (see the README)."),
    ]
    y += 14
    p.append(f'<text x="{px}" y="{y}" font-family="{FONT}" font-size="16" font-weight="700" fill="{INK}">Notes</text>')
    y += 26
    for head, l1, l2 in notes:
        if head:
            p.append(f'<text x="{px}" y="{y}" font-family="{FONT}" font-size="12.5" font-weight="700" fill="{INK}">{escape(head)}</text>')
        p.append(f'<text x="{px + 52}" y="{y}" font-family="{FONT}" font-size="12.5" fill="{MUTED}">{escape(l1)}</text>')
        p.append(f'<text x="{px + 52}" y="{y + 17}" font-family="{FONT}" font-size="12.5" fill="{MUTED}">{escape(l2)}</text>')
        y += 42
    # power button last so it sits on top of the body edge area
    p.append(f'<rect x="191" y="122" width="68" height="36" rx="18" fill="{C_IR}" stroke="#6b7280" stroke-width="1.5" stroke-dasharray="4 3"/>')
    p.append(f'<text x="225" y="145" text-anchor="middle" font-family="{FONT}" font-size="14" font-weight="700" fill="#fff">Power</text>')
    p.append("</svg>")
    return "\n".join(p)


def box(x, y, w, h, title, lines, fill, stroke):
    out = [f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="14" fill="{fill}" stroke="{stroke}" stroke-width="2"/>',
           f'<text x="{x + w / 2}" y="{y + 30}" text-anchor="middle" font-family="{FONT}" font-size="17" font-weight="700" fill="{INK}">{escape(title)}</text>']
    for i, ln in enumerate(lines):
        out.append(f'<text x="{x + w / 2}" y="{y + 54 + i * 18}" text-anchor="middle" font-family="{FONT}" font-size="12.5" fill="{MUTED}">{escape(ln)}</text>')
    return "\n".join(out)


def arrow(x1, y1, x2, y2, color="#3b4252", dashed=False, label=None, lx=None, ly=None):
    dash = ' stroke-dasharray="7 5"' if dashed else ""
    out = [f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" stroke-width="2.5"{dash} marker-end="url(#ah)"/>']
    if label:
        out.append(f'<text x="{lx}" y="{ly}" text-anchor="middle" font-family="{FONT}" font-size="12" fill="{INK}">{escape(label)}</text>')
    return "\n".join(out)


def architecture_svg():
    W, H = 1010, 470
    p = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
         f'role="img" aria-label="Architecture: remote to ESP32-C3 to TV and Chromecast">',
         '<defs><marker id="ah" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" orient="auto-start-reverse">'
         '<path d="M0 0 L10 5 L0 10 z" fill="#3b4252"/></marker></defs>',
         f'<rect width="{W}" height="{H}" fill="#ffffff"/>',
         f'<text x="30" y="40" font-family="{FONT}" font-size="22" font-weight="700" fill="{INK}">How the bridge sits between the remote and the screens</text>']
    p.append(box(30, 130, 230, 150, "Bluetooth remote",
                 ["TCL RC902V", "sends key codes as", "BLE HID reports", "(Consumer Control, ID 3)"], "#eef1f5", "#b8c0cc"))
    p.append(box(375, 90, 270, 230, "ESP32-C3 (the bridge)",
                 ["towards the remote: BLE central", "(HID host - looks like the TV)", "",
                  "mapping table: key  \u2192  TV / Chromecast", "",
                  "towards the screens: BLE peripheral", "(HID remote - looks like a keyboard)"], "#e6f4ec", "#2e8b57"))
    p.append(box(760, 70, 190, 110, "TCL TV", ["receives the keys", "sent to it"], "#fdf1e3", "#b85c00"))
    p.append(box(760, 240, 190, 110, "Chromecast", ["receives the keys", "sent to it"], "#e6eef8", "#2b6cb0"))
    p.append(arrow(260, 205, 375, 205, label="BLE (paired)", lx=318, ly=192))
    p.append(arrow(645, 150, 760, 125, label="BLE", lx=703, ly=120))
    p.append(arrow(645, 260, 760, 295, label="BLE", lx=703, ly=292))
    # IR path: remote -> TV, dashed, around the bottom
    p.append(f'<path d="M145 280 L145 400 L968 400 L968 125 L952 125" fill="none" stroke="{C_IR}" stroke-width="2.5" '
             f'stroke-dasharray="7 5" marker-end="url(#ah)"/>')
    p.append(f'<text x="500" y="392" text-anchor="middle" font-family="{FONT}" font-size="13" fill="{INK}">'
             f'Power: infrared, straight to the TV (the bridge is not involved)</text>')
    p.append(f'<text x="500" y="428" text-anchor="middle" font-family="{FONT}" font-size="12.5" fill="{MUTED}">'
             f'All other keys travel over Bluetooth while the remote is paired to the bridge.</text>')
    p.append("</svg>")
    return "\n".join(p)


if __name__ == "__main__":
    (HERE / "remote-rc902v.svg").write_text(remote_svg(), encoding="utf-8")
    (HERE / "architecture.svg").write_text(architecture_svg(), encoding="utf-8")
    print("wrote remote-rc902v.svg and architecture.svg;", len(MAP), "keys read from the firmware table")
