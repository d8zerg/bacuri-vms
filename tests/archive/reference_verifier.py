"""Verify a Bacuri VMS export or case package with standard tools only.

The script is an independent implementation of the published verification algorithm. It uses the Python
standard library and the openssl command line tool and shares no code with the services, so a recipient
can check a package without trusting Bacuri software.

Usage: reference_verifier.py TRUSTED_PUBLIC_KEY.json EXTRACTED_DIRECTORY
"""

import base64
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

# DER prefix of an Ed25519 SubjectPublicKeyInfo. The raw 32-byte key follows it.
ED25519_PUBLIC_KEY_PREFIX = bytes.fromhex("302a300506032b6570032100")
# Exported audio is mono 16-bit PCM at 8000 Hz after a 44-byte WAV header.
MICROSECONDS_PER_SAMPLE = 125
WAV_HEADER_BYTES = 44


class Rejected(Exception):
    pass


def require(condition, reason):
    if not condition:
        raise Rejected(reason)


def digest(path):
    require(path.is_file() and not path.is_symlink(), f"missing file {path.name}")
    data = path.read_bytes()
    return hashlib.sha256(data).hexdigest(), len(data)


def signed_manifest(directory, key):
    """Check the Ed25519 signature over the exact bytes of manifest.json and parse it."""
    description = (directory / "manifest.json").read_bytes()
    signature = bytes.fromhex((directory / "manifest.sig").read_text().strip())
    require(key.get("algorithm") == "Ed25519" and len(signature) == 64, "unsupported signature")
    with tempfile.TemporaryDirectory() as scratch:
        der = ED25519_PUBLIC_KEY_PREFIX + bytes.fromhex(key["public_key"])
        public_key = Path(scratch) / "key.pem"
        public_key.write_text("-----BEGIN PUBLIC KEY-----\n" + base64.encodebytes(der).decode() +
                              "-----END PUBLIC KEY-----\n")
        signature_file = Path(scratch) / "manifest.sig.bin"
        signature_file.write_bytes(signature)
        checked = subprocess.run(["openssl", "pkeyutl", "-verify", "-pubin", "-inkey", str(public_key), "-rawin",
                                  "-in", str(directory / "manifest.json"), "-sigfile", str(signature_file)],
                                 capture_output=True)
    require(checked.returncode == 0, "signature does not match the trusted key")
    return json.loads(description)


def exact_files(directory, expected):
    present = {entry.name for entry in directory.iterdir()}
    require(present == expected, f"unexpected files {sorted(present ^ expected)}")


def verify_export(directory, key):
    manifest = signed_manifest(directory, key)
    v2 = manifest.get("format") == "bacuri.vms.export.v2"
    require(manifest.get("format") in ("bacuri.vms.export.v1", "bacuri.vms.export.v2"), "unknown format")
    require(manifest["site_id"] == key["site_id"], "export of another site")
    files = {"manifest.json", "manifest.sig"}
    cursor, end = manifest["begin_utc_us"], manifest["end_utc_us"]
    require(cursor < end, "empty interval")
    clips = 0
    for period in manifest["periods"]:
        begin, until = period["begin_utc_us"], period["end_utc_us"]
        # Periods cover the exported interval without overlaps or holes.
        require(begin == cursor and begin < until <= end, "periods are not contiguous")
        cursor = until
        if period["state"] == "gap":
            require(bool(period["reason"]), "gap without reason")
            continue
        clips += 1
        name = f"clip-{clips}.mp4"
        require(period["state"] == "recorded" and period["file"] == name, "unexpected period")
        source = period["source"]
        require(source["site_id"] == manifest["site_id"] and source["point_id"] == manifest["point_id"],
                "media of another point")
        require(source["begin_utc_us"] <= begin and until <= source["end_utc_us"], "period outside its block")
        sha256, size = digest(directory / name)
        require(period["sha256"] == sha256 and period["size_bytes"] == size, f"{name} differs from the manifest")
        transform = period["transform"]
        if transform == "original":
            require(source["media"]["sha256"] == sha256 and (source["begin_utc_us"], source["end_utc_us"]) ==
                    (begin, until), "original clip differs from its block")
        elif transform == "original_with_decode_context":
            require(v2 and manifest["mode"] == "original" and source["media"]["sha256"] == sha256,
                    "original clip differs from its block")
        else:
            require(transform == "h264_frame_selection" or (v2 and transform == "h264_masked_selection"),
                    "unknown transform")
            require(period["frame_count"] > 0, "selection without frames")
        if v2:
            original = transform in ("original", "original_with_decode_context")
            require(period["codec"] == (source["media"]["tracks"][0]["codec"] if original else "h264"),
                    "unexpected codec")
            require((transform == "h264_masked_selection") == bool(period["masks"]), "masks differ from transform")
            audio = period["audio"]
            if audio is not None:
                audio_name = f"audio-{clips}.wav"
                require(manifest["audio_requested"] and audio["file"] == audio_name, "unexpected audio")
                require((audio["codec"], audio["channels"], audio["sample_rate"]) == ("pcm_s16le", 1, 8000),
                        "unexpected audio format")
                first, last = audio["begin_utc_us"], audio["end_utc_us"]
                require(begin <= first < last <= until and (last - first) % MICROSECONDS_PER_SAMPLE == 0,
                        "audio outside its period")
                sha256, size = digest(directory / audio_name)
                samples = (last - first) // MICROSECONDS_PER_SAMPLE
                require(audio["sha256"] == sha256 and audio["size_bytes"] == size == WAV_HEADER_BYTES + 2 * samples,
                        f"{audio_name} differs from the manifest")
                files.add(audio_name)
        files.add(name)
    require(clips > 0 and cursor == end, "periods do not cover the interval")
    exact_files(directory, files)
    return manifest


def verify_case(directory, key):
    manifest = signed_manifest(directory, key)
    require(manifest.get("format") == "bacuri.vms.case.v1", "unknown format")
    require(manifest["site_id"] == key["site_id"] and manifest["reason"], "case of another site")
    parts, materials = manifest["parts"], manifest["case"]["materials"]
    require(0 < len(parts) == len(materials) <= 32, "parts differ from case materials")
    files = {"manifest.json", "manifest.sig"}
    for number, (part, material) in enumerate(zip(parts, materials), start=1):
        name = f"material-{number}"
        require(part["directory"] == name and part["point_id"] == material["point_id"], "unexpected part")
        require(digest(directory / name / "manifest.json")[0] == part["manifest_sha256"], f"{name} was replaced")
        inner = verify_export(directory / name, key)
        require(inner.get("mode") == manifest["mode"] and inner["point_id"] == part["point_id"] and
                (inner["begin_utc_us"], inner["end_utc_us"]) == (part["begin_utc_us"], part["end_utc_us"]),
                f"{name} differs from the case")
        files.add(name)
    exact_files(directory, files)
    return manifest


def verify(key_file, directory):
    key = json.loads(Path(key_file).read_text())
    directory = Path(directory)
    kind = json.loads((directory / "manifest.json").read_text()).get("format")
    return verify_case(directory, key) if kind == "bacuri.vms.case.v1" else verify_export(directory, key)


if __name__ == "__main__":
    try:
        print(json.dumps(verify(sys.argv[1], sys.argv[2]), indent=2))
    except (Rejected, KeyError, TypeError, ValueError, OSError) as error:
        sys.exit(f"Rejected: {error}")
