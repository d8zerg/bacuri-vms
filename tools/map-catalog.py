"""Build an HTTPS catalog and optional offline ZIP from rendered regional MBTiles."""

import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import shutil
import sqlite3
import tempfile
import zipfile


def publish(directory, files, archive):
    directory.mkdir(parents=True, exist_ok=True)
    if len(files) > 1024:
        raise ValueError("Expected at most 1024 regions")
    regions = {}
    with tempfile.TemporaryDirectory(dir=directory) as tmp:
        scratch = Path(tmp)
        processed = []
        for path in files:
            snapshot = scratch / "region.mbtiles"
            shutil.copyfile(path, snapshot)
            with sqlite3.connect(snapshot.resolve().as_uri() + "?mode=ro&immutable=1", uri=True) as db:
                meta = dict(db.execute("SELECT name,value FROM metadata"))
            size = snapshot.stat().st_size
            region_id = meta.get("region_id", meta["name"])
            bounds = list(map(float, meta["bounds"].split(",")))
            stamp = datetime.fromisoformat(meta["osm_base_timestamp"])
            if (
                region_id in regions
                or not region_id
                or meta["format"] != "png"
                or stamp.tzinfo is None
                or size > 8 * 1024**3
                or len(bounds) != 4
                or not -180 <= bounds[0] < bounds[2] <= 180
                or not -85.051129 <= bounds[1] < bounds[3] <= 85.051129
            ):
                raise ValueError(f"Invalid or duplicate region in {path}")
            with snapshot.open("rb") as f:
                sha = hashlib.file_digest(f, "sha256").hexdigest()
            final = directory / (sha + ".mbtiles")
            snapshot.replace(final)
            processed.append(final)
            regions[region_id] = dict(
                id=region_id,
                name=meta["name"],
                bytes=size,
                bounds=bounds,
                source_time=meta["osm_base_timestamp"],
                sha256=sha,
            )
        if archive:
            if sum(path.stat().st_size for path in processed) > 32 * 1024**3:
                raise ValueError("Selected regions exceed the offline package limit")
            with tempfile.TemporaryDirectory(dir=archive.parent) as zip_tmp:
                target = Path(zip_tmp) / "regions.zip"
                with zipfile.ZipFile(target, "w", allowZip64=True) as z:
                    for file in processed:
                        z.write(file, file.name)
                if target.stat().st_size > 32 * 1024**3:
                    raise ValueError("ZIP exceeds the upload limit")
                target.replace(archive)
        index = scratch / "catalog.json"
        index.write_text(
            json.dumps(dict(version=1, regions=list(regions.values())), ensure_ascii=False, indent=2) + "\n"
        )
        index.replace(directory / "catalog.json")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("regions", type=Path, nargs="+")
    parser.add_argument("--zip", type=Path, help="also produce a root archive for disconnected sites")
    args = parser.parse_args()
    publish(args.directory, args.regions, args.zip)
