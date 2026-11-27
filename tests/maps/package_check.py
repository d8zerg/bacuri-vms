"""Check that the site serves the imported Monaco map package.

Signs in as an operator, reads the map scene, the style and one tile of every zoom level over
HTTPS and compares the decoded tiles with the tiles of the package.
"""

import argparse
import io
import json
import sqlite3
import ssl
import urllib.request
from http.cookiejar import CookieJar

from PIL import Image, ImageChops

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("package")
parser.add_argument("--site", required=True)
parser.add_argument("--ca", required=True)
parser.add_argument("--user", required=True)
parser.add_argument("--password", required=True)
arguments = parser.parse_args()

tls = ssl.create_default_context(cafile=arguments.ca)
opener = urllib.request.build_opener(urllib.request.HTTPSHandler(context=tls),
                                     urllib.request.HTTPCookieProcessor(CookieJar()))


def get(path, body=None):
    request = urllib.request.Request(
        path if path.startswith("https://") else arguments.site + path,
        data=json.dumps(body).encode() if body is not None else None,
        headers={"Content-Type": "application/json", "Origin": arguments.site},
    )
    with opener.open(request, timeout=10) as reply:
        return reply.status, reply.headers.get("Content-Type", ""), reply.read()


status, _, body = get("/api/v1/session", {"username": arguments.user, "password": arguments.password})
assert status == 201, status
site = json.loads(body)["site_roles"][0]["site_id"]
scene = json.loads(get(f"/api/v1/sites/{site}/maps")[2])
region = scene["region"]
package = sqlite3.connect(arguments.package)
metadata = dict(package.execute("SELECT name,value FROM metadata"))
assert region["id"] == metadata["region_id"] and region["name"] == metadata["name"], region
assert (region["min_zoom"], region["max_zoom"]) == (int(metadata["minzoom"]), int(metadata["maxzoom"])), region
assert [round(value, 6) for value in region["bounds"]] == [float(v) for v in metadata["bounds"].split(",")], region

style = json.loads(get(region["style_url"] if region["style_url"].startswith("https://")
                       else arguments.site + region["style_url"])[2])
sources = [source for source in style["sources"].values() if source["type"] == "raster"]
assert len(sources) == 1, style
template = sources[0]["tiles"][0]
checked = 0
for zoom, column, row, data in package.execute(
        "SELECT zoom_level,tile_column,tile_row,tile_data FROM tiles t WHERE tile_column=(SELECT min(tile_column) "
        "FROM tiles WHERE zoom_level=t.zoom_level) AND tile_row=(SELECT min(tile_row) FROM tiles WHERE "
        "zoom_level=t.zoom_level AND tile_column=t.tile_column) ORDER BY zoom_level"):
    y = 2**zoom - 1 - row
    url = template.replace("{z}", str(zoom)).replace("{x}", str(column)).replace("{y}", str(y))
    status, content_type, served = get(url)
    assert status == 200 and content_type == "image/png", (url, status, content_type)
    expected = Image.open(io.BytesIO(data)).convert("RGBA")
    actual = Image.open(io.BytesIO(served)).convert("RGBA")
    assert actual.size == expected.size and ImageChops.difference(actual, expected).getbbox() is None, url
    checked += 1
assert checked == int(metadata["maxzoom"]) - int(metadata["minzoom"]) + 1, checked
tiles = package.execute("SELECT count(*) FROM tiles").fetchone()[0]
print(f"Monaco package on the site: region {region['id']} revision {region['revision']}, zoom "
      f"{region['min_zoom']}-{region['max_zoom']}, {tiles} tiles imported, {checked} served tiles equal to the package")
