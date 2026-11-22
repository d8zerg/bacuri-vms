import type { MapScene } from "../contracts/workspace";

type Region = MapScene["regions"][number];
export function coveringRegion(
  regions: Region[],
  current: Region | null | undefined,
  center: number[] | undefined,
) {
  if (!center) return current;
  const [lng, lat] = center;
  function covers(r: Region) {
    const [w, s, e, n] = r.bounds;
    return (
      lat! >= s! &&
      lat! <= n! &&
      (w! <= e! ? lng! >= w! && lng! <= e! : lng! >= w! || lng! <= e!)
    );
  }
  if (current && covers(current)) return current;
  return regions.filter(covers).sort((a, b) => b.max_zoom - a.max_zoom)[0];
}
