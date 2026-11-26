# Archive and export format

[Русская версия](ru/archive-format.md)

The archive and the signed exports belong to the site, not to Bacuri. Their formats are a public contract: recorded video plays in standard players, and a recipient checks an export with standard tools without trusting Bacuri software. The JSON Schemas are in [storage/archive](../storage/archive).

## Archive volume

| Path | Content |
| --- | --- |
| `journal.db` | SQLite journal of the recording node. It indexes the blocks and is rebuilt from the block directories when lost |
| `blocks/<block_id>/manifest.json` | Block manifest. [Schema](../storage/archive/block-manifest-v1.schema.json) |
| `blocks/<block_id>/media.mp4` | Fragmented MP4 with the camera stream as received, H.264 or H.265, starting at a keyframe |
| `blocks/<block_id>/audio.pcm` | Optional camera sound: mono signed 16-bit little-endian PCM at 8000 Hz without a header |

The media file contains one video track at 90 kHz: `ftyp`, `moov` with the decoder configuration (`avcC` or `hvcC`), `moof` and `mdat` fragments of about 500 ms and an `mfra` index of the keyframes. Access units are stored without re-encoding. Their parameter sets move to the decoder configuration and access unit delimiters are removed. When the camera reorders frames, an edit list removes the decoding delay from the presentation.

A block covers one group of pictures, from a keyframe to the next one. Its manifest names the site, the observation point, the camera binding and the recording node, the interval `[begin_utc_us, end_utc_us)` in UTC microseconds, the time basis with its uncertainty, and the size and SHA-256 of every file. The sound starts at `audio.start_us` and each sample covers 125 microseconds.

Without Bacuri:

```sh
sha256sum blocks/<block_id>/media.mp4      # equals media.sha256 of the manifest
ffplay blocks/<block_id>/media.mp4
ffplay -f s16le -ar 8000 -ch_layout mono blocks/<block_id>/audio.pcm
```

To play a longer interval, list the blocks of a point ordered by `begin_utc_us` in an ffmpeg concat file.

## Signed export

| File | Content |
| --- | --- |
| `manifest.json` | Export manifest. [Schema](../storage/archive/export-manifest.schema.json) |
| `manifest.sig` | Ed25519 signature of the exact bytes of `manifest.json`, 128 lowercase hexadecimal characters |
| `clip-N.mp4` | Video of the N-th recorded period |
| `audio-N.wav` | Sound of the N-th recorded period, mono 16-bit PCM at 8000 Hz, when sound was exported |

The periods of the manifest cover the exported interval without overlaps. A `gap` period names the reason why no video exists, for example `source_offline` or `no_frame`. A `recorded` period names its clip, the manifest of the archive block it comes from and the transform:

| Transform | Meaning |
| --- | --- |
| `original` | The unchanged archive block. Its SHA-256 equals `source.media.sha256` |
| `original_with_decode_context` | The unchanged block that also contains frames outside the period, because decoding needs them |
| `h264_frame_selection` | Only the frames of the period, re-encoded |
| `h264_masked_selection` | Only the frames of the period, re-encoded with the privacy masks of the site |

Format `bacuri.vms.export.v2` adds the export mode, the reason, the codec, the masks and the sound of each period.

## Investigation case

A case package holds a signed root manifest and one signed export per material in the directories `material-1`, `material-2` and so on. The root manifest records the SHA-256 of each material manifest. [Schema](../storage/archive/case-manifest-v1.schema.json)

## Trusted key

The recipient obtains the public key of the site through a trusted channel, for example from the site administrator in person. [Schema](../storage/archive/trusted-key.schema.json)

```json
{"site_id": "10000000-0000-4000-8000-000000000001", "algorithm": "Ed25519", "public_key": "<64 hexadecimal characters>"}
```

## Verification algorithm

1. Verify the Ed25519 signature in `manifest.sig` over the exact bytes of `manifest.json` with the trusted key. Accept only a manifest of the site named by the key.
2. For a case: check that each material directory exists, that the SHA-256 of its `manifest.json` equals `manifest_sha256`, and verify the material as an export. Its point, interval and mode equal the case part.
3. For an export: check that the periods start at `begin_utc_us`, follow each other without holes and end at `end_utc_us`.
4. For each recorded period: the SHA-256 and size of the clip equal the manifest, the period lies inside its source block, and an `original` clip equals the source block byte for byte.
5. For each exported sound: the WAV header is 44 bytes, the size equals 44 plus two bytes per 125 microseconds of the period, and the SHA-256 equals the manifest.
6. The directory contains no files other than those named by the manifest.

A package that fails any step is rejected as a whole.

## Verification tools

The supported tool is `bacuri-vms-verify-export TRUSTED_PUBLIC_KEY.json EXTRACTED_DIRECTORY`. It prints the verified manifest and returns a non-zero status for a rejected package. It depends only on libsodium and JSON and builds without the archive volume, SQLite and GStreamer: `cmake --build build/gcc-release --target bacuri-vms-verify-export`.

[reference_verifier.py](../tests/archive/reference_verifier.py) implements the same algorithm with the Python standard library and the openssl command line tool and shares no code with Bacuri. The export scenarios require both tools to accept every genuine package and to reject every altered one.

The signature alone can be checked with OpenSSL 3:

```sh
key=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["public_key"])' trusted-key.json)
printf '302a300506032b6570032100%s' "$key" | xxd -r -p | openssl pkey -pubin -inform DER -out key.pem
xxd -r -p manifest.sig > manifest.sig.bin
openssl pkeyutl -verify -pubin -inkey key.pem -rawin -in manifest.json -sigfile manifest.sig.bin
sha256sum clip-*.mp4 audio-*.wav    # compare with sha256 of the periods in manifest.json
```
