"""Compare Qt captures and accessibility trees with the reviewed workstation references."""

import argparse
import json
from pathlib import Path
import shutil

from PIL import Image, ImageChops, ImageDraw, ImageStat

ROOT = Path(__file__).resolve().parents[2]
REFERENCES = ROOT / "tests/desktop/references"
HELP = ROOT / "web/client/src/help/screenshots"


def reference_image(directory, scene):
    if directory.name == "1" and scene in {"settings-en-100", "settings-ru-100"}:
        return HELP / f"{scene}.png"
    return directory / f"{scene}.png"


def pixels(actual, expected, video_rect):
    with Image.open(actual) as capture, Image.open(expected) as baseline:
        if capture.size != baseline.size:
            raise ValueError(f"size {capture.size} != {baseline.size}")
        diff = ImageChops.difference(capture.convert("RGB"), baseline.convert("RGB"))
    if video_rect:
        x, y, w, h = video_rect
        ImageDraw.Draw(diff).rectangle((x, y, x+w-1, y+h-1), fill=(0, 0, 0))
    channels = diff.split()
    maximum = ImageChops.lighter(ImageChops.lighter(channels[0], channels[1]), channels[2])
    changed = maximum.point(lambda v: 255 if v > 12 else 0)
    fraction = ImageStat.Stat(changed).mean[0] / 255
    mean = max(ImageStat.Stat(diff).mean)
    # A local check catches changes to small icons in an otherwise identical window.
    tile = max(8, round(diff.width / 24))
    coarse = changed.reduce(tile)
    worst = coarse.getextrema()[1] / 255
    if fraction > .002 or mean > .25 or worst > .18:
        diff.save(actual.with_suffix(".diff.png"))
        raise ValueError(f"changed {fraction:.3%}, mean {mean:.3f}, worst tile {worst:.1%}")


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("scale")
    parser.add_argument("--update", action="store_true", help="replace references after an intentional UI change")
    args = parser.parse_args()
    directory = REFERENCES / args.scale
    scenes = {p.stem for p in args.report.glob("*.png") if not p.name.endswith(".diff.png")}
    if not scenes:
        parser.error("no captures")
    trees = {s: json.loads((args.report / f"{s}.json").read_text()) for s in sorted(scenes)}
    manifest = directory / "trees.json"
    if args.update:
        directory.mkdir(parents=True, exist_ok=True)
        for scene in scenes:
            path = reference_image(directory, scene)
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(args.report / f"{scene}.png", path)
        manifest.write_text(json.dumps(trees, ensure_ascii=False, indent=2) + "\n")
        return
    expected = json.loads(manifest.read_text())
    if scenes != expected.keys():
        parser.error(f"missing {sorted(expected.keys()-scenes)}, unexpected {sorted(scenes-expected.keys())}")
    failures = []
    for scene in sorted(scenes):
        if trees[scene] != expected[scene]:
            failures.append(f"{scene}: accessibility tree, focus order or video bounds changed")
        try:
            pixels(args.report/f"{scene}.png", reference_image(directory, scene), expected[scene].get("videoRect"))
        except ValueError as exc:
            failures.append(f"{scene}: {exc}")
    if failures:
        raise SystemExit("\n".join(failures))
    print(f"{len(scenes)} pixel and accessibility references matched at scale {args.scale}")


if __name__ == "__main__":
    main()
