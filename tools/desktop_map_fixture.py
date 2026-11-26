#!/usr/bin/env python3
"""Build the test map of the desktop client from an OpenStreetMap extract.

Downloads the Monaco extract from Geofabrik unless a PBF file is given, renders raster tiles
of zoom levels 12 to 17 in the dark palette of the operator workspace and writes a MapLibre
style that reads the tiles from the local directory. The map is small, works without
network access and is used by the screens and tests of the desktop client.

Data (c) OpenStreetMap contributors, available under the Open Database License.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import subprocess
import sys
import urllib.request
from pathlib import Path

import osmium
from PIL import Image, ImageDraw

SOURCE = "https://download.geofabrik.de/europe/monaco-latest.osm.pbf"
BOUNDS = (7.405, 43.723, 7.440, 43.753)  # west, south, east, north
ZOOMS = range(12, 18)
TILE = 256
PALETTE = {
    "background": "#1d1c1a",
    "park": "#232a20",
    "water": "#1c2630",
    "coastline": "#3a5266",
    "building": "#2f2c28",
    "minor": "#46423b",
    "major": "#8b7d5c",
    "rail": "#5a5650",
}
MAJOR = {"motorway", "trunk", "primary", "secondary", "tertiary"}
MINOR = {"residential", "unclassified", "service", "living_street", "pedestrian", "footway", "steps", "path"}


def world(lon: float, lat: float, zoom: int) -> tuple[float, float]:
    span = TILE * 2**zoom
    x = (lon + 180) / 360 * span
    y = (1 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2 * span
    return x, y


class Features(osmium.SimpleHandler):
    """Collects the drawn features as longitude and latitude lists."""

    def __init__(self) -> None:
        super().__init__()
        self.areas: dict[str, list[list[tuple[float, float]]]] = {"park": [], "water": [], "building": []}
        self.lines: dict[str, list[list[tuple[float, float]]]] = {"major": [], "minor": [], "rail": [], "coastline": []}

    def area(self, area) -> None:
        tags = area.tags
        kind = None
        if "building" in tags:
            kind = "building"
        elif tags.get("leisure") in ("park", "garden") or tags.get("landuse") in ("grass", "forest"):
            kind = "park"
        elif tags.get("natural") == "water" or tags.get("leisure") == "marina":
            kind = "water"
        if kind is None:
            return
        for ring in area.outer_rings():
            self.areas[kind].append([(node.lon, node.lat) for node in ring if node.location.valid()])

    def way(self, way) -> None:
        tags = way.tags
        highway = tags.get("highway")
        kind = None
        if highway in MAJOR:
            kind = "major"
        elif highway in MINOR:
            kind = "minor"
        elif tags.get("railway") == "rail":
            kind = "rail"
        elif tags.get("natural") == "coastline":
            kind = "coastline"
        if kind is not None:
            self.lines[kind].append([(node.lon, node.lat) for node in way.nodes if node.location.valid()])


def render(features: Features, zoom: int, column: int, row: int) -> Image.Image:
    image = Image.new("RGB", (TILE, TILE), PALETTE["background"])
    draw = ImageDraw.Draw(image)

    def local(points):
        return [(x - column * TILE, y - row * TILE) for x, y in (world(lon, lat, zoom) for lon, lat in points)]

    for kind in ("park", "water", "building"):
        if kind == "building" and zoom < 15:
            continue
        for ring in features.areas[kind]:
            if len(ring) > 2:
                draw.polygon(local(ring), fill=PALETTE[kind])
    widths = {"coastline": 2, "rail": 1, "minor": max(1, zoom - 14), "major": max(1, zoom - 12)}
    for kind in ("coastline", "rail", "minor", "major"):
        if kind == "minor" and zoom < 14:
            continue
        for line in features.lines[kind]:
            if len(line) > 1:
                draw.line(local(line), fill=PALETTE[kind], width=widths[kind], joint="curve")
    return image


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pbf", type=Path, help="use this extract instead of downloading it")
    parser.add_argument("--output", type=Path, default=Path("build/desktop/map/monaco"))
    arguments = parser.parse_args()
    output = arguments.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    pbf = arguments.pbf or output / "monaco.osm.pbf"
    if arguments.pbf is None:
        urllib.request.urlretrieve(SOURCE, pbf)

    features = Features()
    features.apply_file(str(pbf), locations=True)
    tiles = 0
    for zoom in ZOOMS:
        west, north = world(BOUNDS[0], BOUNDS[3], zoom)
        east, south = world(BOUNDS[2], BOUNDS[1], zoom)
        for column in range(int(west // TILE), int(east // TILE) + 1):
            for row in range(int(north // TILE), int(south // TILE) + 1):
                path = output / "tiles" / str(zoom) / str(column) / f"{row}.png"
                path.parent.mkdir(parents=True, exist_ok=True)
                render(features, zoom, column, row).save(path, optimize=True)
                tiles += 1

    attribution = "© OpenStreetMap contributors, ODbL"
    style = {
        "version": 8,
        "name": "Bacuri test map, Monaco",
        "sources": {
            "monaco": {
                "type": "raster",
                "tiles": [f"file://{output}/tiles/{{z}}/{{x}}/{{y}}.png"],
                "tileSize": TILE,
                "minzoom": ZOOMS.start,
                "maxzoom": ZOOMS.stop - 1,
                "bounds": list(BOUNDS),
                "attribution": attribution,
            }
        },
        "layers": [
            {"id": "background", "type": "background", "paint": {"background-color": PALETTE["background"]}},
            {"id": "monaco", "type": "raster", "source": "monaco"},
        ],
    }
    (output / "style.json").write_text(json.dumps(style, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    replication = subprocess.run(
        ["osmium", "fileinfo", "-g", "header.option.osmosis_replication_timestamp", str(pbf)],
        capture_output=True, text=True).stdout.strip()
    metadata = {
        "name": "Monaco",
        "source": SOURCE if arguments.pbf is None else pbf.name,
        "source_sha256": hashlib.file_digest(pbf.open("rb"), "sha256").hexdigest(),
        "osm_base_timestamp": replication,
        "bounds": list(BOUNDS),
        "zooms": [ZOOMS.start, ZOOMS.stop - 1],
        "tiles": tiles,
        "license": "Open Database License 1.0",
        "attribution": attribution,
    }
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Monaco test map: {tiles} tiles of zoom {ZOOMS.start}-{ZOOMS.stop - 1}, extract {replication}, "
          f"{output.relative_to(Path.cwd()) if output.is_relative_to(Path.cwd()) else output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
