#!/usr/bin/env python3
"""Build the Monaco map package for the server tests from an OpenStreetMap extract.

Downloads the Monaco extract from Geofabrik unless a PBF file is given, renders raster tiles of
zoom levels 12 to 17 in the dark palette of the operator workspace and writes a raster MBTiles
package in the import format of the site: PNG tiles with TMS rows and the name, bounds, format,
region_id and osm_base_timestamp metadata. The server imports the package and serves the style
and the tiles to the web and desktop clients.

Data (c) OpenStreetMap contributors, available under the Open Database License.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
import sqlite3
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
    parser.add_argument("--output", type=Path, default=Path("build/maps"))
    arguments = parser.parse_args()
    output = arguments.output
    output.mkdir(parents=True, exist_ok=True)
    pbf = arguments.pbf or output / "monaco.osm.pbf"
    if arguments.pbf is None:
        urllib.request.urlretrieve(SOURCE, pbf)
    replication = subprocess.run(
        ["osmium", "fileinfo", "-g", "header.option.osmosis_replication_timestamp", str(pbf)],
        capture_output=True, text=True, check=True).stdout.strip()

    features = Features()
    features.apply_file(str(pbf), locations=True)
    package = output / "monaco.mbtiles"
    package.unlink(missing_ok=True)
    tiles = 0
    with sqlite3.connect(package) as db:
        db.execute("CREATE TABLE metadata (name text, value text)")
        db.execute("CREATE TABLE tiles (zoom_level integer, tile_column integer, tile_row integer, tile_data blob)")
        db.execute("CREATE UNIQUE INDEX tile_index ON tiles (zoom_level, tile_column, tile_row)")
        for zoom in ZOOMS:
            west, north = world(BOUNDS[0], BOUNDS[3], zoom)
            east, south = world(BOUNDS[2], BOUNDS[1], zoom)
            for column in range(int(west // TILE), int(east // TILE) + 1):
                for row in range(int(north // TILE), int(south // TILE) + 1):
                    png = io.BytesIO()
                    render(features, zoom, column, row).save(png, "PNG", optimize=True)
                    # MBTiles counts rows from the south.
                    db.execute("INSERT INTO tiles VALUES (?,?,?,?)", (zoom, column, 2**zoom - 1 - row, png.getvalue()))
                    tiles += 1
        db.executemany("INSERT INTO metadata VALUES (?,?)", [
            ("name", "Monaco"),
            ("region_id", "bacuri-test-monaco"),
            ("format", "png"),
            ("bounds", ",".join(str(value) for value in BOUNDS)),
            ("minzoom", str(ZOOMS.start)),
            ("maxzoom", str(ZOOMS.stop - 1)),
            ("osm_base_timestamp", replication),
            ("attribution", "© OpenStreetMap contributors, ODbL"),
        ])
    metadata = {
        "package": package.name,
        "package_sha256": hashlib.file_digest(package.open("rb"), "sha256").hexdigest(),
        "source": SOURCE if arguments.pbf is None else pbf.name,
        "source_sha256": hashlib.file_digest(pbf.open("rb"), "sha256").hexdigest(),
        "osm_base_timestamp": replication,
        "bounds": list(BOUNDS),
        "zooms": [ZOOMS.start, ZOOMS.stop - 1],
        "tiles": tiles,
        "license": "Open Database License 1.0",
        "attribution": "© OpenStreetMap contributors, ODbL",
    }
    (output / "monaco.json").write_text(json.dumps(metadata, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Monaco map package: {tiles} tiles of zoom {ZOOMS.start}-{ZOOMS.stop - 1}, extract {replication}, {package}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
