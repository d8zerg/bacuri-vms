import liveClip from "../../../prototype/dist/assets/yard-live.webm";
import archiveClip from "../../../prototype/dist/assets/yard.webm";
import type {
  Services,
  Session,
  AlertItem,
  Entry,
  AlertLink,
} from "../src/contracts/workspace";
import { pause } from "../src/workflows/pause";
export const utc = (n: number) =>
  new Date(Date.UTC(2026, 0, 1, 14, 20) + n * 1000).toISOString();
const range = (a: number, b: number) => ({ start: utc(a), end: utc(b) });
const site = "test-site",
  point = "dock";
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
const alerts: AlertItem[] = [82, 132, 236, 301].map((n, i) => ({
  id: "event-" + i,
  point_id: point,
  observed_at: utc(n),
  type: [
    "Движение у ворот",
    "Потеря видеопотока",
    "Смещение часов камеры",
    "Поток восстановлен",
  ][i]!,
  archive_interval: range(n - 15, n + 15),
  time_basis: "source_clock",
  uncertainty_us: "0",
}));
export function fixture(condition = "normal"): Services {
  const session: Session = {
    user_id: "installer",
    csrf_token: "fixture",
    expires_at: utc(3600),
    site_roles: [{ site_id: site, role: "administrator" }],
  };
  let attempt = 0,
    revision = 0;
  return {
    session: () => Promise.resolve(session),
    login: () => Promise.resolve(session),
    logout: () => Promise.resolve(),
    points: () =>
      Promise.resolve([
        {
          id: point,
          name: "Погрузочные ворота 02",
          connection: "online",
          recording: "current",
        },
      ]),
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
      return {
        ...draft,
        id: "device",
        point_id: point,
        applied_revision: String(++revision),
        desired_revision: String(revision),
        state: "applied",
        has_credentials: false,
      };
    },
    archive: () => Promise.resolve(rows),
    resolve: (_, id) => {
      const i = alerts.findIndex((a) => a.id === id),
        item = alerts[i]!;
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
      publish({ status: "online", items: condition === "empty" ? [] : alerts });
      return () => {};
    },
    attach: async (video, _, target, signal) => {
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
