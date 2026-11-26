"""Verify signed files, inclusive start/exclusive end and every frame outside VMS."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import struct
import sys
import tarfile
import wave
from tempfile import TemporaryDirectory

import jsonschema
from referencing import Registry
from referencing.jsonschema import DRAFT202012

sys.path.insert(0, str(Path(__file__).resolve().parent))
import reference_verifier

SCHEMAS = {path.name: json.loads(path.read_text()) for path in Path("storage/archive").glob("*.schema.json")}
REGISTRY = Registry().with_resources((s["$id"], DRAFT202012.create_resource(s)) for s in SCHEMAS.values())


def conforms(document, schema):
    jsonschema.Draft202012Validator(SCHEMAS[schema], registry=REGISTRY).validate(document)


def independently(key, root):
    """Result of the independent reference verifier: the manifest or None when it rejects the package."""
    try:
        return reference_verifier.verify(key, root)
    except (reference_verifier.Rejected, KeyError, TypeError, ValueError, OSError):
        return None


def decode_ordinals(clip):
    raw = subprocess.check_output(["gst-launch-1.0", "-q", "filesrc", "location=" + str(clip),
        "!", "qtdemux", "!", "h264parse", "!", "avdec_h264", "!", "videoconvert",
        "!", "video/x-raw,format=RGB", "!", "fdsink", "fd=1"], timeout=15)
    stride = 320 * 3
    frame_bytes = stride * 240
    assert len(raw) % frame_bytes == 0
    return [sum((raw[offset + 40*stride + (bit*64+32)*3] > 128) << bit for bit in range(5))
            for offset in range(0, len(raw), frame_bytes)]


def mp4_durations(path):
    # Track and movie duration in non-fragmented boundary clips.
    data = path.read_bytes()
    def duration(box):
        p = data.index(box) + 4
        version = data[p]
        assert version in (0,1)
        scale, ticks = struct.unpack_from(">IQ" if version else ">II", data, p + (20 if version else 12))
        assert scale == 1_000_000
        return ticks
    return duration(b"mdhd"), duration(b"mvhd")


def verify_cases(binary, report):
    key = report / "trusted-key.json"
    masked = 0
    for mode in ("original", "derivative"):
        with TemporaryDirectory() as unpacked:
            root = Path(unpacked)
            with tarfile.open(report / f"case-{mode}.tar") as package:
                package.extractall(root, filter="data")
            case = json.loads(subprocess.check_output([binary, key, root]))
            assert len(case["parts"]) == 2, case
            conforms(case, "case-manifest-v1.schema.json")
            assert independently(key, root) == case
            for part in case["parts"]:
                directory = root / part["directory"]
                manifest = json.loads((directory / "manifest.json").read_text())
                conforms(manifest, "export-manifest.schema.json")
                clip, = [p for p in manifest["periods"] if p["state"] == "recorded"]
                file = directory / clip["file"]
                raw = subprocess.check_output(["gst-launch-1.0","-q","filesrc","location="+str(file),"!",
                    "qtdemux","!","decodebin","!","videoconvert","!","video/x-raw,format=GRAY8","!","fdsink","fd=1"],timeout=15)
                assert raw and len(raw) % (320*240) == 0
                if mode == "derivative" and max(raw) <= 18:
                    masked += 1
                    assert len(raw) == 15*320*240
                else:
                    frames = [sum((raw[i+40*320+32+64*b]>128)<<b for b in range(5)) for i in range(0,len(raw),320*240)]
                    assert frames == (list(range(25)) if mode=="original" else list(range(5,20))), (mode, frames)
                with wave.open(str(directory / clip["audio"]["file"])) as wav:
                    assert wav.getnchannels()==1 and wav.getframerate()==8000
                    assert wav.readframes(8000) == struct.pack("<4800h", *range(-2400,2400))
            signature=root/"manifest.sig"
            signature.write_bytes(b"0"*128)
            assert subprocess.run([binary,key,root],capture_output=True).returncode
            assert independently(key, root) is None
    assert masked==1, masked
    print("Disconnected verification passed: both case signatures, H.264/HEVC frames, masks and audio samples")


def run():
    binary = Path("build") / sys.argv[1] / "bacuri-vms-verify-export"
    report = Path(sys.argv[2])
    assert os.listdir("/sys/class/net") == ["lo"], "Offline verifier requires network_mode: none"
    if sys.argv[3:] == ["investigations"]:
        return verify_cases(binary,report)
    browser = json.loads((report / "browser.json").read_text())
    key = report / "trusted-key.json"
    with TemporaryDirectory() as tmp:
        root = Path(tmp) / "episode"
        root.mkdir()
        with tarfile.open(report / "episode.tar", "r:") as package:
            package.extractall(root, filter="data")
        def verify(trust=key, selection=()):
            return subprocess.run([binary, trust, root, *selection], capture_output=True, text=True, timeout=10)

        good = verify()
        assert good.returncode == 0, good.stderr
        m = json.loads(good.stdout)
        conforms(m, "export-manifest.schema.json")
        conforms(json.loads(key.read_text()), "trusted-key.schema.json")
        assert independently(key, root) == m, "The reference verifier disagrees with bacuri-vms-verify-export"
        for field in ("site_id", "point_id", "alert_id"):
            assert m[field] == browser[field]
        assert [row["export"]["id"] for row in browser["audit"] if row["outcome"] == "applied"] == [m["export_id"]]
        origin = browser["origin_us"]
        expected = (browser["point_id"], str(origin+221000), str(origin+3779000))
        assert verify(selection=expected).returncode == 0
        assert verify(selection=(expected[0], str(origin+221001), expected[2])).returncode
        assert verify(selection=(browser["alert_id"], *expected[1:])).returncode
        assert (m["begin_utc_us"]-origin, m["end_utc_us"]-origin) == (221000, 3779000)
        spans = [(p["state"], p["begin_utc_us"]-origin, p["end_utc_us"]-origin) for p in m["periods"]]
        assert spans == [("gap",221000,240000),("recorded",240000,1000000),("recorded",1000000,2000000),
                         ("gap",2000000,3000000),("recorded",3000000,3779000)], spans
        assert [p["reason"] for p in m["periods"] if p["state"]=="gap"] == ["no_frame","source_offline"]
        clips = [p for p in m["periods"] if p["state"]=="recorded"]
        assert [p["source"] for p in clips] == browser["source_manifests"]
        frames = [decode_ordinals(root / p["file"]) for p in clips]
        assert frames == [list(range(6,25)),list(range(25)),list(range(20))], frames
        assert clips[0]["frame_count"] == len(frames[0]) and clips[2]["frame_count"] == len(frames[2])
        assert clips[1]["transform"]=="original"
        for clip in (clips[0],clips[2]):
            length = clip["end_utc_us"] - clip["begin_utc_us"]
            actual = mp4_durations(root / clip["file"])
            assert actual == (length,length), (clip["file"], actual, length)
        rejected = []
        for name in ("clip-1.mp4", "manifest.json", "manifest.sig"):
            path = root / name
            contents = path.read_bytes()
            changed = bytearray(contents)
            if name=="manifest.json":
                changed = contents.replace(m["point_id"].encode(), b"00000000-0000-4000-8000-000000000000")
            else:
                changed[len(changed)//2] ^= 1
            assert changed != contents
            path.write_bytes(changed)
            assert verify().returncode, name
            assert independently(key, root) is None, name
            path.write_bytes(contents)
            rejected.append(name)
        extra = root / "extra.mp4"
        extra.touch()
        assert verify().returncode
        assert independently(key, root) is None
        extra.unlink()
        trusted = json.loads(key.read_text())
        trusted["public_key"] = "00" * 32
        wrong = Path(tmp) / "other-key.json"
        wrong.write_text(json.dumps(trusted))
        assert verify(wrong).returncode and verify().returncode == 0
        assert independently(wrong, root) is None and independently(key, root) == m
        report_data = {"export_id": m["export_id"], "site_id": m["site_id"], "point_id": m["point_id"],
                       "network_interfaces": ["lo"], "package_sha256": hashlib.sha256((report / "episode.tar").read_bytes()).hexdigest(),
                       "verified_periods_us_from_origin": spans, "decoded_frame_ordinals": frames,
                       "rejected": rejected + ["extra_file", "untrusted_key", "wrong_point", "wrong_interval"], "status": "passed"}
        (report / "verification.json").write_text(json.dumps(report_data, indent=2))
        print("Verified", sum(map(len, frames)), "frames, signed intervals, source manifests and tamper refusals")
if __name__ == "__main__":
    run()

