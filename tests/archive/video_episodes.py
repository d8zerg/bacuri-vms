"""Data and ground truth for archive simulation of local video rules."""

from datetime import datetime, timezone
import hashlib
import json
import subprocess
import uuid

import playback

COUNT = 100
PER_BLOCK = 25
# Position and luminance of the two changed areas in each eight-frame episode.
CLASSES = [
    {"positive": True, "y": 16, "size": 8, "levels": 235, "left": 16, "right": 40},
    {"positive": False, "y": 24, "size": 4, "levels": 32, "left": 16, "right": 20},
]


def prepare(camera):
    labels = [
        {**kind, "offset": group * 300 + i * 2} for group, kind in enumerate(CLASSES) for i in range(COUNT)
    ]
    digest = hashlib.sha256(json.dumps(labels, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    assert digest == "68e873469726a53b4ef00283d9091c9ad06882d747cc71eb322f52aac7cd7ea9", digest
    anchor = -(playback.BASE.second + 1200)
    ids = []
    for group, kind in enumerate(CLASSES):
        frames = bytearray()
        for phase in range(8):
            im = bytearray(64 * 48)
            for x in [kind["left"]] if phase == 1 else [kind["left"], kind["right"]] if phase == 2 else []:
                for y in range(kind["y"], kind["y"] + kind["size"]):
                    im[y * 64 + x : y * 64 + x + kind["size"]] = bytes([kind["levels"]]) * kind["size"]
            frames.extend(im)
        for i in range(0, COUNT, PER_BLOCK):
            directory = playback.ROOT / "blocks" / str(uuid.uuid4())
            directory.mkdir()
            subprocess.run(
                [
                    "gst-launch-1.0",
                    "-q",
                    "fdsrc",
                    "fd=0",
                    "!",
                    "rawvideoparse",
                    "format=gray8",
                    "width=64",
                    "height=48",
                    "framerate=4/1",
                    "!",
                    "videoconvert",
                    "!",
                    "video/x-raw,format=I420",
                    "!",
                    "x264enc",
                    "tune=zerolatency",
                    "key-int-max=8",
                    "!",
                    "h264parse",
                    "!",
                    "mp4mux",
                    "fragment-duration=1000",
                    "!",
                    "filesink",
                    "location=" + str(directory / "media.mp4"),
                ],
                input=bytes(frames) * PER_BLOCK,
                check=True,
                timeout=15,
            )
            ids.append(
                playback.register_fixture(camera, directory, anchor + group * 300 + i * 2, PER_BLOCK * 2)[
                    "block_id"
                ]
            )
    return anchor, ids, digest


def evaluate(dataset, api, point, definition):
    anchor, ids, digest = dataset
    c = playback.control
    literals = ",".join("'" + i + "'" for i in ids)
    def completed():
        return int(c.query(f"SELECT count(*) FROM bacuri_archive.motion_indexes "
                           f"WHERE block_id IN ({literals}) AND samples IS NOT NULL"))

    count = completed()
    while count < len(ids):
        def progress():
            indexed = completed()
            return indexed if indexed > count else None

        count = c.wait_for(progress, f"progress indexing labeled video after {count}/{len(ids)} blocks")
    upper = dict(left=0, top=0, right=1, bottom=0.5)
    lower = dict(left=0, top=0.5, right=1, bottom=1)
    schedule = []
    for minute in range(4):
        t = datetime.fromtimestamp(playback.BASE.timestamp() + anchor + minute * 60, timezone.utc)
        at = t.hour * 60 + t.minute
        schedule.append(dict(day=t.isoweekday(), start=at, end=at + 1))
    rules = {
        "zone": {"include": [upper]},
        "exclusion": {"exclude": [lower]},
        "line": {"line": [0.5, 0, 0.5, 0.5]},
        "amplitude": {"threshold": 96},
        "area": {"min_cells": 3},
        "schedule": {"schedule": schedule},
    }
    metrics = {}
    for name, params in rules.items():
        predicted = []
        for offset in (0, 300):
            response = api(
                "POST",
                f"points/{point}/video-rule/simulation",
                {
                    "interval": playback.interval(anchor + offset, anchor + offset + 200),
                    "rule": {**definition, "cooldown": 1, "min_cells": 1, "threshold": 10, **params},
                },
            )
            assert response["complete"], (name, offset)
            predicted.extend(
                round(
                    datetime.fromisoformat(t.replace("Z", "+00:00")).timestamp()
                    - playback.BASE.timestamp()
                    - anchor,
                    3,
                )
                for t in response["hits"]
            )
        expected = [2 * i + 0.5 for i in range(COUNT)]
        unmatched = predicted.copy()
        tp = 0
        for t in expected:
            near = next((p for p in unmatched if abs(p - t) <= 1), None)
            if near is not None:
                unmatched.remove(near)
                tp += 1
        metrics[name] = {"TP": tp, "FP": len(unmatched), "FN": COUNT - tp}
        assert tp >= 90 and tp / max(1, len(predicted)) >= 0.95, (name, metrics[name], predicted[:8])
    print("Labeled video", digest, json.dumps(metrics), flush=True)
    return {"ground_truth_sha256": digest, "rules": metrics}
