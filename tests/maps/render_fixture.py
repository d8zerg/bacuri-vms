"""Render one reproducible OSM road tile for the isolated HTTPS browser scenario."""

import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys

import osmium
from PIL import Image, ImageDraw

source, destination = map(Path, sys.argv[1:])
zoom = 13
span = 2**zoom


def mercator(lon, lat):
    return (lon + 180) / 360 * span, (1 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2 * span


x, y = map(int, mercator(7.423, 43.735))
image = Image.new("RGB", (256, 256), "#e8e6dc")
draw = ImageDraw.Draw(image)


class Roads(osmium.SimpleHandler):
    def way(self, way):
        if "highway" not in way.tags:
            return
        points = []
        for node in way.nodes:
            if not node.location.valid():
                return
            u, v = mercator(node.lon, node.lat)
            points.append(((u - x) * 256, (v - y) * 256))
        if len(points) > 1:
            arterial = way.tags["highway"] in ("primary", "secondary", "trunk", "tertiary")
            draw.line(points, fill="#9c7951" if arterial else "#a5a399", width=2 if arterial else 1)


Roads().apply_file(str(source), locations=True)
image.save(destination.with_suffix(".png"), optimize=True)
latitude = lambda row: math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * row / span))))
destination.with_suffix(".json").write_text(
    json.dumps(
        {
            "name": "Monaco",
            "format": "png",
            "z": zoom,
            "x": x,
            "y": y,
            "bounds": ",".join(
                str(n)
                for n in (x / span * 360 - 180, latitude(y + 1), (x + 1) / span * 360 - 180, latitude(y))
            ),
            "osm_base_timestamp": subprocess.check_output(
                ["osmium", "fileinfo", "-g", "header.option.osmosis_replication_timestamp", source], text=True
            ).strip(),
            "source_sha256": hashlib.file_digest(source.open("rb"), "sha256").hexdigest(),
        },
        indent=2,
    )
    + "\n"
)
