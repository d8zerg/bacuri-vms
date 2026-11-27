import { mapPackageFixture } from "./mapPackageFixture";
import ids from "./fixture-ids.json";
import { alertWorkFixture } from "./alertWorkFixture";
import floor from "./floor.png";
import type { MapScene } from "../src/contracts/workspace";
import liveClip from "./yard-live.webm";
import archiveClip from "./yard.webm";
import type {
  Services,
  Session,
  AlertItem,
  Camera,
  Feed,
  Entry,
  AlertLink,
  PtzCommand,
  CameraControls,
  Layout,
} from "../src/contracts/workspace";
import { HttpFailure } from "../src/adapters/api";
import { pause } from "../src/workflows/pause";
export const utc = (n: number) =>
  new Date(Date.UTC(2026, 0, 1, 14, 20) + n * 1000).toISOString();
const range = (a: number, b: number) => ({ start: utc(a), end: utc(b) });
const site = ids.site,
  point = ids.dockPoint;
const rows: Entry[] = [
  {
    state: "recorded",
    interval: range(0, 115),
    time_basis: "source_clock",
    uncertainty_us: "0",
  },
  { state: "gap", interval: range(115, 160), reason: "source_offline" },
  {
    state: "recorded",
    interval: range(160, 260),
    time_basis: "source_clock",
    uncertainty_us: "8000000",
  },
  { state: "pending_catalog", interval: range(260, 360) },
];
const initialAlerts: AlertItem[] = [82, 132, 236, 301].map((n, i) => ({
  id: ids.alerts[i]!,
  point_id: point,
  work_state: "new",
  observed_at: utc(n),
  // Motion alerts at a recording, a gap, a recording with estimated time and a block that is still cataloged.
  type: "motion",
  archive_interval: range(n - 15, n + 15),
  time_basis: "source_clock",
  uncertainty_us: "0",
}));
export function fixture(condition = "normal"): Services {
  const caseRows: import("../src/contracts/workspace").Investigation[] = [];
  let ruleVersion: Awaited<ReturnType<Services["videoRule"]>> = {
    revision: "0",
    rule: null,
  };
  let layouts: Layout[] = [];
  const capture: import("../src/contracts/workspace").CapturePolicies = {
    policies: [],
    effective: {},
  };
  let storage: import("../src/contracts/workspace").CaptureStorage = {
    days: 30,
    reserve_percent: 20,
    revision: "1",
    volume: { state: "normal", available_bytes: 400e9, capacity_bytes: 1000e9 },
    points: [],
  };
  function resolveCapture() {
    const policy =
      capture.policies.find((p) => p.scope === "point" && p.target === point) ??
      capture.policies.find(
        (p) => p.scope === "group" && p.target === ids.dockGroup,
      );
    capture.effective = policy
      ? {
          [point]: {
            id: policy.id,
            revision: Number(policy.revision),
            since: policy.since,
            rule: policy.rule,
            windows: [],
          },
        }
      : {};
  }
  if (condition === "recording") {
    const now = Date.now() * 1000;
    capture.policies.push({
      id: ids.dockPolicy,
      target: ids.dockGroup,
      scope: "group",
      priority: 50,
      revision: "1",
      since: now,
      rule: {
        continuous: false,
        manual: true,
        event: true,
        pre: 5,
        post: 10,
        audio: false,
        days: 14,
        zone: "Europe/Moscow",
        schedule: [1, 2, 3, 4, 5].map((day) => ({
          day,
          start: 480,
          end: 1080,
        })),
      },
    });
    resolveCapture();
    storage.points = [{ point, oldest_us: now - 13 * 86400e6, newest_us: now }];
    if (storage.volume) storage.volume.point_states = { [point]: "selected" };
  }
  let recording = false;
  const cameraHistory: CameraControls["history"] = [
    {
      actor: "Оператор",
      command: "zoom_in",
      target: "",
      outcome: "unknown",
      at: utc(0),
    },
  ];
  let cameras: Camera[] =
    condition === "empty"
      ? []
      : [
          {
            id: ids.previousCamera,
            point_id: point,
            name: "Ворота",
            binding_revision: "1",
            main_url: "rtsps://previous/main",
            secondary_url: "rtsps://previous/sub",
            has_credentials: true,
            desired_revision: "1",
            applied_revision: "1",
            state: "applied",
          },
        ];
  if (condition === "catalog") {
    const device = {
      endpoint_id: "warehouse-encoder",
      model: "Энкодер склада",
      serial: "WH-204",
      firmware: "2.1",
      manufacturer: "Bacuri",
    };
    for (const [index, name] of ["Разгрузка", "Склад"].entries()) {
      cameras.push({
        ...cameras[0]!,
        ...device,
        id: ids.storageCamera[index]!,
        point_id: ids.storagePoint[index]!,
        name,
        channel_token: "channel" + (index + 1),
        desired_revision: "2",
        applied_revision: index === 0 ? "2" : null,
        state: index === 0 ? "applied" : "applying",
      });
    }
  }
  let alerts = condition === "empty" ? [] : initialAlerts;
  const subscribers = new Set<(feed: Feed) => void>();
  const session: Session = {
    username: "operator.demo",
    user_id: ids.user,
    // A CSRF token has the length of the server token.
    csrf_token: "fixture-csrf-token-000000000000000000000000000",
    expires_at: utc(3600),
    site_roles: [{ site_id: site, role: "administrator" }],
  };
  if (condition === "multiple-sites")
    session.site_roles.push({
      site_id: ids.otherSite,
      role: "operator",
    });
  let signedIn = condition !== "login";
  let disconnected = condition === "session-offline";
  let attempt = 0,
    revision = 0;
  const footprint = {
    bearing: 90,
    width_m: 40,
    center: [7.4, 43.73],
    corners: [
      [7.4, 43.73],
      [7.4, 43.7296],
      [7.3997, 43.7296],
      [7.3997, 43.73],
    ],
  };
  const scene: MapScene = {
    region: null,
    regions: [],
    fonts: [],
    symbols: "",
    plans:
      condition === "empty"
        ? []
        : [
            {
              id: ids.floorPlan,
              name: "Первый этаж",
              width: 640,
              height: 360,
              image_url: floor,
            },
          ],
    placements:
      condition === "empty"
        ? []
        : [
            {
              point_id: point,
              plan_id: ids.floorPlan,
              x: 0.34,
              y: 0.52,
              camera: { device_id: ids.camera, ptz: false, profile: "" },
              view: { state: "fixed", bearing: 55, hfov: 65 },
              calibration: {
                device_id: ids.camera,
                ptz: false,
                bearing: 55,
                hfov: 65,
              },
            },
          ],
  };
  if (condition === "linked-floors") {
    const region = {
      id: "city",
      revision: ids.cityRevision,
      name: "Территория",
      source_time: utc(0),
      min_zoom: 0,
      max_zoom: 19,
      bounds: [7.399, 43.729, 7.401, 43.731],
      style_url:
        "data:application/json," +
        encodeURIComponent(
          JSON.stringify({
            version: 8,
            sources: {},
            layers: [
              {
                id: "ground",
                type: "background",
                paint: { "background-color": "#62686d" },
              },
            ],
          }),
        ),
      attribution: "OpenStreetMap",
    };
    scene.region = region;
    scene.regions = [region];
    const floor = scene.plans[0]!;
    floor.building = "Корпус";
    floor.footprint = footprint;
    floor.definition = {
      anchor: { x: 0.5, y: 0.5, lng: 7.4, lat: 43.73 },
      width_m: 40,
      bearing: 90,
      building: floor.building,
    };
    scene.plans.push({ ...floor, id: ids.upperPlan, name: "Второй этаж" });
    scene.placements.push({
      point_id: point,
      plan_id: null,
      x: 7.4,
      y: 43.73,
      source_plan_id: floor.id,
      view: { state: "fixed", bearing: 145, hfov: 65 },
    });
  }
  if (condition === "ptz-plan") {
    const placed = scene.placements[0]!;
    placed.camera = {
      device_id: ids.camera,
      profile: "main",
      ptz: true,
      fresh: true,
      pan: 0.15,
      zoom: 0,
      space: "generic",
      hfov: 80,
    };
    placed.view = { state: "unknown" };
    delete placed.calibration;
  }
  return {
    videoRule: () => Promise.resolve(ruleVersion),
    saveRule: (_site, _point, body) => {
      ruleVersion = {
        revision: String(Number(ruleVersion.revision) + 1),
        rule: body.rule,
      };
      return Promise.resolve(ruleVersion);
    },
    simulateRule: (_site, _point, { interval }) =>
      Promise.resolve({ hits: [interval.start], complete: true }),
    caseRecipients: () =>
      Promise.resolve([{ id: ids.user, name: "Монтажник" }]),
    exportCase: () => Promise.reject(new HttpFailure(403, "forbidden")),
    searchFrames: (_site, { points, interval }) =>
      Promise.resolve(
        points.map((point_id) => ({
          point_id,
          coverage: [
            {
              interval,
              indexed: true,
              basis: "source_clock",
              uncertainty_us: 1000,
              binding_revision: "1",
            },
          ],
          hits: [
            {
              at: interval.start,
              cells: 4,
              preview: btoa(String.fromCharCode(80).repeat(64 * 48)),
            },
          ],
          frames: [
            interval.start,
            new Date(Date.parse(interval.start) + 40).toISOString(),
          ],
          gaps: [],
          events: [],
          next: null,
        })),
      ),
    cases: () => Promise.resolve([...caseRows]),
    saveCase: (_site, id, body) => {
      const revision = String(Number(body.revision) + 1);
      const saved = {
        ...body,
        id,
        revision,
        materials: body.materials.map((m) => ({
          ...m,
          hold_id: crypto.randomUUID(),
        })),
      };
      const index = caseRows.findIndex((c) => c.id === id);
      if (index >= 0) caseRows[index] = saved;
      else caseRows.push(saved);
      return Promise.resolve({ id, revision });
    },
    exportFragment: () => Promise.reject(new HttpFailure(403, "forbidden")),
    ...alertWorkFixture(point, utc(150), (id, state) => {
      alerts = alerts.map((item) =>
        item.id === id ? { ...item, work_state: state } : item,
      );
      for (const publish of subscribers)
        publish({ status: "online", items: alerts });
    }),
    maps: () =>
      condition === "map-error"
        ? Promise.reject(Error("503"))
        : Promise.resolve(structuredClone(scene)),
    previewPlan: (_site, body) =>
      Promise.resolve(
        body.definition
          ? {
              token: "preview",
              footprint,
              cameras: [
                {
                  point_id: point,
                  projected: [7.4, 43.73],
                  existing: [7.4001, 43.73],
                  distance_m: 8,
                },
              ],
            }
          : { token: "preview", cameras: [] },
      ),
    linkPlan: (_site, body) => {
      const plan = scene.plans.find((p) => p.id === body.plan_id);
      if (!plan) return Promise.reject(Error("404"));
      if (body.definition) {
        plan.definition = body.definition;
        plan.footprint = footprint;
        if (body.definition.building) plan.building = body.definition.building;
      } else {
        delete plan.footprint;
        delete plan.definition;
        delete plan.building;
      }
      return Promise.resolve();
    },
    calibrate: (_site, b) => {
      const row = scene.placements.find(
        (p) => p.point_id === b.point_id && p.plan_id === b.plan_id,
      );
      if (!row) return Promise.reject(Error("404"));
      row.view = {
        state: row.camera?.ptz ? "live" : "fixed",
        bearing: b.bearing,
        hfov: b.hfov,
      };
      row.calibration = {
        device_id: b.device_id,
        ptz: false,
        bearing: b.bearing,
        hfov: b.hfov,
      };
      return Promise.resolve();
    },
    place: (_site, body) => {
      scene.placements = [{ ...scene.placements[0], ...body }];
      return Promise.resolve();
    },
    ...mapPackageFixture(scene, condition === "map-management"),
    addPlan: (_site, name) => {
      scene.plans.push({
        id: "new",
        name,
        width: 640,
        height: 360,
        image_url: floor,
      });
      return Promise.resolve({ id: "new" });
    },
    session: () => {
      if (disconnected) {
        disconnected = false;
        return Promise.reject(new HttpFailure(503, "dependency_unavailable"));
      }
      return signedIn
        ? Promise.resolve(session)
        : Promise.reject(new HttpFailure(401, "unauthorized"));
    },
    login: (_username, _password, enrollment) => {
      if (condition === "login" && !enrollment)
        return Promise.reject(new HttpFailure(403, "enrollment_required"));
      signedIn = true;
      return Promise.resolve(session);
    },
    logout: () => {
      signedIn = false;
      return Promise.resolve();
    },
    issueDesktopCode: () =>
      Promise.resolve({
        code: "0".repeat(64),
        expires_at: "2024-01-01T00:01:00.000000Z",
      }),
    refreshOnvif: (_site, camera) => Promise.resolve(camera),
    audio: (_site, _point, request) =>
      request.mode === "archive"
        ? Promise.reject(new HttpFailure(404, "camera_audio_missing"))
        : Promise.resolve({}),
    capturePolicies: () => Promise.resolve(structuredClone(capture)),
    captureGroups: () =>
      Promise.resolve([
        { id: ids.dockGroup, name: "Погрузочные ворота", points: [point] },
      ]),
    saveCapture: (_site, id, body) => {
      const revision = String(Number(body.revision) + 1),
        since = Date.now() * 1000;
      capture.policies = capture.policies.filter((p) => p.id !== id);
      capture.policies.push({ ...body, id, revision, since });
      resolveCapture();
      return Promise.resolve({ id, revision });
    },
    deleteCapture: (_site, id) => {
      capture.policies = capture.policies.filter((p) => p.id !== id);
      resolveCapture();
      return Promise.resolve();
    },
    captureStorage: () => Promise.resolve(structuredClone(storage)),
    setCaptureStorage: (_site, days, reserve_percent) =>
      Promise.resolve(
        (storage = {
          ...storage,
          days,
          reserve_percent,
          revision: String(Number(storage.revision) + 1),
        }),
      ),
    manualRecording: () =>
      Promise.resolve({
        enabled: true,
        active: recording,
        until_us: recording ? (Date.now() + 900000) * 1000 : null,
      }),
    setManualRecording: (_site, _point, active) =>
      Promise.resolve({
        enabled: true,
        active: (recording = active),
        until_us: active ? (Date.now() + 900000) * 1000 : null,
      }),
    layouts: () => Promise.resolve(structuredClone(layouts)),
    saveLayout: (_site, id, draft) => {
      const current = layouts.find((l) => l.id === id);
      if (draft.revision !== (current?.revision ?? "0"))
        return Promise.reject(new HttpFailure(409, "revision_conflict"));
      const revision = String(Number(draft.revision) + 1);
      layouts = [
        ...layouts.filter((l) => l.id !== id),
        { ...draft, id, revision, editable: true },
      ];
      return Promise.resolve({ id, revision });
    },
    deleteLayout: (_site, id) => {
      layouts = layouts.filter((l) => l.id !== id);
      return Promise.resolve();
    },
    cameraControls: () =>
      Promise.resolve({
        device_id: ids.camera,
        actions: [
          { command: "preset_goto", target: "gate", label: "Ворота" },
          { command: "preset_goto", target: "dock", label: "Погрузочная зона" },
          { command: "tour_start", target: "perimeter", label: "Периметр" },
          { command: "tour_stop", target: "perimeter", label: "Периметр" },
          ...(
            [
              "focus_near",
              "focus_far",
              "focus_stop",
              "light_on",
              "light_off",
              "wiper_on",
              "wiper_off",
              "relay_on",
              "relay_off",
            ] as const
          ).map((command) => ({
            command,
            target: command.startsWith("relay") ? "1" : "",
            label: command.startsWith("relay") ? "1" : "",
          })),
        ] satisfies CameraControls["actions"],
        inputs: [{ token: "1", active: false }],
        history: [...cameraHistory],
      }),
    ptz: (_site, _point, _device, command, target) => {
      cameraHistory.unshift({
        actor: "Оператор",
        command,
        target: target ?? "",
        outcome: "acknowledged",
        at: new Date().toISOString(),
      });
      return Promise.resolve();
    },
    points: () =>
      Promise.resolve([
        {
          id: point,
          name: "Погрузочные ворота 02",
          ...(condition === "controls" || condition === "intercom"
            ? {
                ptz: {
                  device_id: ids.camera,
                  commands: [
                    "left",
                    "right",
                    "up",
                    "down",
                    "zoom_in",
                    "zoom_out",
                    "stop",
                  ] satisfies PtzCommand[],
                },
                audio: {
                  device_id: ids.camera,
                  listen: true,
                  talk: condition === "intercom",
                  record: true,
                  archive: true,
                },
              }
            : {}),
          connection: "online",
          recording: "current",
        },
        ...(condition === "multiview"
          ? [
              {
                id: ids.rampPoint,
                name: "Рампа",
                connection: "online" as const,
                recording: "current" as const,
              },
            ]
          : []),
        ...(condition === "catalog" || condition === "multiview"
          ? [
              {
                id: ids.storagePoint[0]!,
                name: "Разгрузка",
                connection: "online" as const,
                recording: "current" as const,
              },
              {
                id: ids.storagePoint[1]!,
                name: "Склад",
                connection: "offline" as const,
                recording: "stopped" as const,
                recording_reason: "source_offline" as const,
              },
            ]
          : []),
      ]),
    discover: () =>
      Promise.resolve([
        {
          endpoint_id: "test-camera",
          device_service: "https://camera.test/onvif/device",
          selection: "discovered-device",
        },
      ]),
    cameras: () => Promise.resolve(structuredClone(cameras)),
    inspect: (_, __, username, password) =>
      username === "viewer" && password === "demo"
        ? Promise.resolve({
            endpoint_id: "test-camera",
            manufacturer: "Test",
            model: "ONVIF fixture",
            firmware: "1",
            serial: "stand-A",
            ptz: true,
            events: false,
            selection: "approved-device",
            profiles: [
              { token: "p1", name: "High", url: "rtsps://camera.test/1" },
              { token: "p2", name: "Low", url: "rtsps://camera.test/2" },
            ],
          })
        : Promise.reject(new HttpFailure(422, "camera_authentication_failed")),
    recoverGap: () =>
      Promise.resolve({
        blocks: 1,
        duration_us: 1000000,
        outcome: "copied",
        next: null,
      }),
    edgeArchive: () =>
      Promise.resolve({
        binding_revision: "1",
        gaps: [range(1, 2)],
        supported: true,
        recordings: [
          { id: "rec1", tracks: [{ kind: "Video", bounds: range(0, 4) }] },
        ],
      }),
    previousRecording: () =>
      Promise.resolve({ binding_revision: "1", interval: range(0, 4) }),
    record: async (_, __, signal) => {
      await pause(400, signal);
      return range(15, 19);
    },
    checkProfiles: () =>
      Promise.resolve({ main: "available", secondary: "available" }),
    register: async (_, draft, signal) => {
      await pause(160, signal);
      if (condition === "offline" && ++attempt === 1)
        throw new Error("offline");
      const registered: Camera = {
        name: draft.name,
        main_url: draft.main_url,
        secondary_url: draft.secondary_url,
        id: draft.replace_device_id || crypto.randomUUID(),
        binding_revision: draft.replace_device_id ? "2" : "1",
        point_id: point,
        applied_revision: String(++revision),
        desired_revision: String(revision),
        state: "applied",
        has_credentials: !!draft.credentials,
      };
      cameras = [...cameras.filter((c) => c.id !== registered.id), registered];
      return registered;
    },
    archive: () => Promise.resolve(rows),
    resolve: (_, id) => {
      const i = initialAlerts.findIndex((a) => a.id === id),
        item = initialAlerts[i]!;
      const base = { point_id: point, archive_interval: range(0, 360) };
      const link: AlertLink =
        i === 1
          ? { ...base, state: "gap", reason: "source_offline" }
          : i === 3
            ? { ...base, state: "pending_catalog" }
            : {
                ...base,
                state: "ready",
                position: item.observed_at,
                interval: rows[i === 0 ? 0 : 2]!.interval,
              };
      return Promise.resolve(link);
    },
    subscribe: (_, publish) => {
      subscribers.add(publish);
      publish({ status: "online", items: alerts });
      return () => {
        subscribers.delete(publish);
      };
    },
    attach: async (video, _, target, signal, _fail, format) => {
      if (condition === "multiview" && target.mode === "live")
        format?.(
          target.quality === "main"
            ? "Основной · H.264 · 1280×720"
            : "Дополнительный · H.264 · 640×360",
        );
      signal.throwIfAborted();
      const response = await fetch(
        target.mode === "live" ? liveClip : archiveClip,
        { signal },
      );
      if (!response.ok) throw Error("Fixture footage unavailable");
      const blob = await response.blob();
      signal.throwIfAborted();
      const url = URL.createObjectURL(blob);
      const loaded = new Promise<void>((resolve, reject) => {
        video.addEventListener("loadeddata", () => resolve(), {
          once: true,
          signal,
        });
        video.addEventListener("error", () => reject(Error("video")), {
          once: true,
          signal,
        });
        signal.addEventListener("abort", () => reject(Error("abort")), {
          once: true,
        });
      });
      signal.addEventListener(
        "abort",
        () => {
          video.pause();
          video.removeAttribute("src");
          video.load();
          URL.revokeObjectURL(url);
        },
        { once: true },
      );
      video.preload = "auto";
      video.src = url;
      await loaded;
      if (target.mode === "live") {
        const settled = new Promise<void>((done, reject) => {
          video.addEventListener("seeked", () => done(), {
            once: true,
            signal,
          });
          signal.addEventListener(
            "abort",
            () => reject(new DOMException("Aborted", "AbortError")),
            { once: true },
          );
        });
        video.currentTime = 1;
        await settled;
      }
      if (condition === "slow") await pause(900, signal);
    },
  };
}
