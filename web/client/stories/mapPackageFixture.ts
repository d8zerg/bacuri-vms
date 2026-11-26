import ids from "./fixture-ids.json";
import type {
  MapManagement,
  MapScene,
  MapSource,
  Services,
} from "../src/contracts/workspace";

export function mapPackageFixture(
  scene: MapScene,
  managementStory: boolean,
): Pick<
  Services,
  | "mapManagement"
  | "mapSource"
  | "inspectMapSource"
  | "uploadMapChunk"
  | "pauseMapDownload"
  | "queueMapRegions"
> {
  const available = ["Север", "Юг"].map((name, index) => ({
    id: String(index),
    name,
    source_time: "2020-02-01T00:00:00Z",
    bytes: 8388608,
    bounds: [0, 0, 10, 10],
  }));
  if (managementStory) {
    // An installed region does not carry the download size of the catalog entry.
    const region = {
      id: available[0]!.id,
      name: available[0]!.name,
      source_time: "2020-01-01T00:00:00Z",
      bounds: [0, 0, 20, 20],
      revision: ids.previousRegionRevision,
      min_zoom: 0,
      max_zoom: 1,
      attribution: "OSM",
      style_url: "/style",
    };
    scene.region = region;
    scene.regions = [region];
    scene.placements.push({
      point_id: ids.dockPoint,
      plan_id: null,
      x: 15,
      y: 15,
    });
  }
  const management: MapManagement = { catalog_url: "", jobs: [] };
  let source: MapSource = {
    state: "ready",
    error: "",
    received: 0,
    regions: available,
  };
  let online = false;
  return {
    mapManagement: () => Promise.resolve(structuredClone(management)),
    mapSource: (_site, body) => {
      online = body.kind === "online";
      const failed =
        body.kind === "online" && body.url.endsWith("unavailable.json");
      source = {
        state: failed ? "failed" : "ready",
        error: failed ? "catalog_unavailable" : "",
        received: 0,
        regions: available,
      };
      return Promise.resolve({ id: "source" });
    },
    inspectMapSource: () => Promise.resolve(structuredClone(source)),
    uploadMapChunk: (_site, _id, offset, chunk) => {
      source.received = offset + chunk.size;
      return Promise.resolve();
    },
    pauseMapDownload: (_site, id, paused) => {
      const job = management.jobs.find((j) => j.id === id);
      if (job) job.state = paused ? "paused" : "downloading";
      return Promise.resolve();
    },
    queueMapRegions: (_site, body) => {
      for (const region of available.filter((r) =>
        body.regions.includes(r.id),
      )) {
        management.jobs = management.jobs.filter(
          (j) => j.region_id !== region.id,
        );
        management.jobs.push({
          id: region.id,
          region_id: region.id,
          name: region.name,
          source_id: "source",
          mode: body.mode,
          online,
          state: online ? "retrying" : "installed",
          received: online ? 1048576 : region.bytes,
          bytes: region.bytes,
          error: "",
        });
      }
      return Promise.resolve();
    },
  };
}
