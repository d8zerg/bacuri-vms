"""Attach Geofabrik source metadata to a rendered raster MBTiles package before deployment."""

import argparse
from pathlib import Path
import sqlite3
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("extract", type=Path)
parser.add_argument("package", type=Path)
parser.add_argument("region")
args = parser.parse_args()
timestamp = subprocess.check_output(
    ["osmium", "fileinfo", "-g", "header.option.osmosis_replication_timestamp", args.extract], text=True
).strip()
if not timestamp or not args.region.strip():
    parser.error("A region name and replication timestamp are required")
with sqlite3.connect(args.package.resolve().as_uri() + "?mode=rw", uri=True) as db:
    if db.execute("SELECT value FROM metadata WHERE name='format'").fetchall() != [("png",)]:
        parser.error("Expected a rendered PNG MBTiles package")
    db.execute("DELETE FROM metadata WHERE name IN ('name', 'osm_base_timestamp')")
    db.executemany(
        "INSERT INTO metadata(name,value) VALUES (?,?)",
        [("name", args.region), ("osm_base_timestamp", timestamp)],
    )
