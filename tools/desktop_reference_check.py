#!/usr/bin/env python3
"""Check a reference recording of the desktop workstation.

Reads the boxes of a fragmented MP4 file and prints the codec, the profile, the frame size,
the frame rate, the number of frames and the keyframe interval. Fails when they differ from
the expected values given on the command line.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

CONTAINERS = {b"moov", b"trak", b"mdia", b"minf", b"stbl", b"moof", b"traf", b"mvex"}
PROFILES = {
    "avc1": {66: "baseline", 77: "main", 100: "high"},
    "hvc1": {1: "main", 2: "main-10"},
}


def boxes(data: bytes, start: int, end: int):
    position = start
    while position + 8 <= end:
        size, kind = struct.unpack_from(">I4s", data, position)
        header = 8
        if size == 1:
            size = struct.unpack_from(">Q", data, position + 8)[0]
            header = 16
        elif size == 0:
            size = end - position
        yield kind, position + header, position + size
        position += size


def walk(data: bytes, start: int, end: int, found: dict) -> None:
    for kind, body, finish in boxes(data, start, end):
        if kind in CONTAINERS:
            walk(data, body, finish, found)
        elif kind == b"tkhd":
            version = data[body]
            offset = body + (88 if version == 1 else 76)
            width, height = struct.unpack_from(">II", data, offset)
            found["size"] = (width >> 16, height >> 16)
        elif kind == b"mdhd":
            version = data[body]
            found["timescale"] = struct.unpack_from(">I", data, body + (20 if version == 1 else 12))[0]
        elif kind == b"stsd":
            entry = body + 8
            codec = data[entry + 4:entry + 8].decode()
            found["codec"] = "hvc1" if codec in ("hvc1", "hev1") else codec
            for child, child_body, _ in boxes(data, entry + 8 + 78, entry + struct.unpack_from(">I", data, entry)[0]):
                if child == b"avcC":
                    found["profile"] = data[child_body + 1]
                elif child == b"hvcC":
                    found["profile"] = data[child_body + 1] & 0x1F
        elif kind == b"trex":
            found["default_duration"] = struct.unpack_from(">I", data, body + 12)[0]
            found["default_flags"] = struct.unpack_from(">I", data, body + 20)[0]
        elif kind == b"tfhd":
            flags = struct.unpack_from(">I", data, body)[0] & 0xFFFFFF
            offset = body + 8 + (8 if flags & 0x1 else 0) + (4 if flags & 0x2 else 0)
            if flags & 0x8:
                found["fragment_duration"] = struct.unpack_from(">I", data, offset)[0]
                offset += 4
            if flags & 0x10:
                offset += 4
            found["fragment_flags"] = struct.unpack_from(">I", data, offset)[0] if flags & 0x20 else None
        elif kind == b"trun":
            read_trun(data, body, found)


def read_trun(data: bytes, body: int, found: dict) -> None:
    flags = struct.unpack_from(">I", data, body)[0] & 0xFFFFFF
    count = struct.unpack_from(">I", data, body + 4)[0]
    offset = body + 8 + (4 if flags & 0x1 else 0)
    first_flags = None
    if flags & 0x4:
        first_flags = struct.unpack_from(">I", data, offset)[0]
        offset += 4
    default_flags = found.get("fragment_flags")
    if default_flags is None:
        default_flags = found.get("default_flags", 0)
    for index in range(count):
        duration = found.get("fragment_duration", found.get("default_duration"))
        sample_flags = first_flags if index == 0 and first_flags is not None else default_flags
        if flags & 0x100:
            duration = struct.unpack_from(">I", data, offset)[0]
            offset += 4
        if flags & 0x200:
            offset += 4
        if flags & 0x400:
            sample_flags = struct.unpack_from(">I", data, offset)[0]
            offset += 4
        if flags & 0x800:
            offset += 4
        found["total_duration"] = found.get("total_duration", 0) + (duration or 0)
        if not sample_flags & 0x00010000:
            found.setdefault("keyframes", []).append(found.get("frames", 0))
        found["frames"] = found.get("frames", 0) + 1


def inspect(path: Path) -> dict:
    data = path.read_bytes()
    found: dict = {}
    walk(data, 0, len(data), found)
    codec = found["codec"]
    keyframes = found.get("keyframes", [])
    intervals = {later - earlier for earlier, later in zip(keyframes, keyframes[1:])}
    return {
        "codec": "h265" if codec == "hvc1" else "h264",
        "profile": PROFILES.get(codec, {}).get(found.get("profile"), str(found.get("profile"))),
        "size": "{}x{}".format(*found["size"]),
        "fps": round(found.get("frames", 0) * found["timescale"] / found["total_duration"], 3),
        "frames": found.get("frames", 0),
        "keyframe_interval": max(intervals) if intervals else None,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", type=Path)
    parser.add_argument("--codec", required=True)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--size", required=True)
    parser.add_argument("--fps", type=float, required=True)
    parser.add_argument("--frames", type=int, required=True)
    parser.add_argument("--keyframe-interval", type=int, required=True)
    arguments = parser.parse_args()
    actual = inspect(arguments.file)
    expected = {
        "codec": arguments.codec, "profile": arguments.profile, "size": arguments.size,
        "fps": arguments.fps, "frames": arguments.frames, "keyframe_interval": arguments.keyframe_interval,
    }
    differences = [f"{key}: expected {value}, found {actual[key]}" for key, value in expected.items() if actual[key] != value]
    print(f"{arguments.file.name}: " + ", ".join(f"{key} {value}" for key, value in actual.items()))
    for difference in differences:
        print(f"{arguments.file.name}: {difference}", file=sys.stderr)
    return 1 if differences else 0


if __name__ == "__main__":
    sys.exit(main())
