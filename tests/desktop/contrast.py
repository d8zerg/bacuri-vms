"""Check the colors exported by the running Qt singleton against the common contrast contract."""

import csv
import ctypes
import itertools
import json
import sys
from pathlib import Path

# Full-severity linear RGB transforms from Machado, Oliveira and Fernandes, DOI 10.1109/TVCG.2009.113.
VISION = {
    "standard": ((1, 0, 0), (0, 1, 0), (0, 0, 1)),
    "protanopia": ((.152286, 1.052583, -.204868), (.114503, .786281, .099216), (-.003882, -.048116, 1.051998)),
    "deuteranopia": ((.367322, .860646, -.227968), (.280085, .672501, .047413), (-.011820, .042940, .968881)),
    "tritanopia": ((1.255528, -.076749, -.178779), (-.078411, .930809, .147602), (.004733, .691367, .303900)),
}


def linear_rgb(color, matrix):
    srgb = [int(color[i:i + 2], 16) / 255 for i in (1, 3, 5)]
    linear = [v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4 for v in srgb]
    transformed = [max(0, min(1, sum(a * b for a, b in zip(row, linear)))) for row in matrix]
    return transformed


def luminance(color, matrix):
    return sum(v * w for v, w in zip(linear_rgb(color, matrix), (.2126, .7152, .0722)))


class Lab(ctypes.Structure):
    _fields_ = [(key, ctypes.c_double) for key in ("L", "a", "b")]


def lab(color, matrix):
    r, g, b = linear_rgb(color, matrix)
    xyz = ((.4124564*r + .3575761*g + .1804375*b) / .95047,
           .2126729*r + .7151522*g + .0721750*b,
           (.0193339*r + .1191920*g + .9503041*b) / 1.08883)
    x, y, z = (v**(1/3) if v > (6/29)**3 else v/(3*(6/29)**2) + 4/29 for v in xyz)
    return Lab(116*y - 16, 500*(x-y), 200*(y-z))


def separation(palettes):
    delta = ctypes.CDLL("liblcms2.so.2").cmsCIE2000DeltaE
    delta.argtypes = [ctypes.POINTER(Lab)]*2 + [ctypes.c_double]*3
    delta.restype = ctypes.c_double
    return [{"theme": theme, "vision": vision, "pair": [a, b],
             "deltaE00": round(delta(ctypes.byref(lab(palette[a], matrix)),
                                     ctypes.byref(lab(palette[b], matrix)), 1, 1, 1), 3)}
            for theme, palette in palettes.items() for vision, matrix in VISION.items()
            for a, b in itertools.combinations(("info", "success", "warning", "danger"), 2)]


def token(name):
    parts = name.split("_")
    if parts[0] == "on":
        parts.insert(0, "text")
    return parts[0] + "".join(word.title() for word in parts[1:])


def main():
    report = Path(sys.argv[1])
    contract = Path(__file__).resolve().parents[2] / "design/bacuri/contrast.csv"
    palettes = {theme: json.loads((report / f"palette-{theme}.json").read_text()) for theme in ("light", "dark", "contrast-light", "contrast-dark")}
    rows = []
    base=list(csv.DictReader(contract.read_text().splitlines()))
    for pair in base + [dict(row, theme="contrast-"+row["theme"]) for row in base]:
        palette = palettes[pair["theme"]]
        fg, bg = (palette[token(pair[key])] for key in ("foreground", "background"))
        for vision, matrix in VISION.items():
            if vision != "standard" and pair["background"] not in {"success_bg", "warning_bg", "danger_bg", "info_bg"}:
                continue
            lo, hi = sorted([luminance(fg, matrix), luminance(bg, matrix)])
            ratio = (hi + .05) / (lo + .05)
            rows.append({**pair, "vision": vision, "ratio": round(ratio, 3)})
            if ratio < float(pair["minimum"]):
                raise ValueError(f"Insufficient contrast: {rows[-1]}")
    distances = separation(palettes)
    (report / "distinguishability.json").write_text(json.dumps(distances, indent=2) + "\n")
    worst = min(distances, key=lambda item: item["deltaE00"])
    if worst["deltaE00"] < 8:
        raise ValueError(f"Severity colors need a CIEDE2000 distance of 8: {worst}")
    (report / "contrast.json").write_text(json.dumps(rows, indent=2) + "\n")


if __name__ == "__main__":
    main()
