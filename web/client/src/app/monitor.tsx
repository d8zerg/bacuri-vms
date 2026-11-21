import { lazy, Suspense, useEffect, useMemo, useRef, useState } from "react";
import { Link, useSearchParams } from "react-router";
import type {
  AlertLink,
  Entry,
  Feed,
  Interval,
  Point,
  PtzCommand,
  Services,
  Target,
} from "../contracts/workspace";
import { AlertList } from "../ui-kit/AlertList";
import { ArchiveTimeline } from "../ui-kit/ArchiveTimeline";
import type { TimelineData } from "../ui-kit/ArchiveTimeline";
import {
  SessionPlayer,
  detail,
  reasons,
  timelineEntry,
  windowAround,
  milliseconds,
} from "./media";

import { Button } from "../ui-kit/FormControls";
import { cameraError } from "./CameraSelection";
const AlertWorkPanel = lazy(() =>
  import("./AlertWork").then((m) => ({ default: m.AlertWorkPanel })),
);
const ptzLabels: Record<PtzCommand, string> = {
  left: "Влево",
  right: "Вправо",
  up: "Вверх",
  down: "Вниз",
  stop: "Остановить",
};
function PtzControls({
  commands,
  send,
}: {
  commands: PtzCommand[];
  send: (command: PtzCommand) => Promise<void>;
}) {
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [confirmed, setConfirmed] = useState("");
  async function move(command: PtzCommand) {
    setBusy(true);
    setError("");
    setConfirmed("");
    try {
      await send(command);
      setConfirmed(
        command === "stop"
          ? "Камера подтвердила команду остановки."
          : "Камера подтвердила движение на одну секунду.",
      );
    } catch (e) {
      setError(
        cameraError(e) ??
          "Камера не подтвердила команду. Проверьте связь и доступ к точке.",
      );
    } finally {
      setBusy(false);
    }
  }
  return (
    <fieldset className="ws-ptz" disabled={busy}>
      <legend>Поворот камеры</legend>
      {commands.map((command) => (
        <Button key={command} onClick={() => void move(command)}>
          {ptzLabels[command]}
        </Button>
      ))}
      {confirmed && <span role="status">{confirmed}</span>}
      {error && <span role="alert">{error}</span>}
    </fieldset>
  );
}
type Selection = { entries: Entry[]; window: Interval; link?: AlertLink };
function View({
  services,
  site,
  point,
  alert,
  at,
  archive,
  restoredWindow,
  onMode,
  onSeek,
  onBack,
}: {
  services: Services;
  site: string;
  point: Point;
  alert: string | null;
  at: string | null;
  archive: boolean;
  onMode: (mode: "live" | "archive") => void;
  onSeek: (utc: number) => void;
  restoredWindow: Interval | null;
  onBack: (position: string, window: Interval) => void;
}) {
  const [loaded, setLoaded] = useState<Selection | null>(null),
    [failed, setFailed] = useState(false),
    [attempt, setAttempt] = useState(0);
  const [anchor] = useState(() =>
    Number.isFinite(Date.parse(at ?? "")) ? Date.parse(at!) : Date.now(),
  );
  const heading = useRef<HTMLHeadingElement>(null);
  useEffect(() => {
    if (alert) heading.current?.focus();
  }, [alert]);
  useEffect(() => {
    const ctl = new AbortController();
    async function load() {
      const link = alert
        ? await services.resolve(site, alert, ctl.signal)
        : undefined;
      if (link && link.point_id !== point.id) throw new Error("Point mismatch");
      const window =
        link?.archive_interval ?? restoredWindow ?? windowAround(anchor);
      const entries = await services.archive(
        site,
        point.id,
        window,
        ctl.signal,
      );
      if (!ctl.signal.aborted)
        setLoaded({ entries, window, ...(link ? { link } : {}) });
    }
    void load().catch(() => {
      if (!ctl.signal.aborted) setFailed(true);
    });
    return () => ctl.abort();
  }, [services, site, point.id, alert, anchor, attempt, restoredWindow]);
  const retry = () => {
    setFailed(false);
    setLoaded(null);
    setAttempt((n) => n + 1);
  };
  const link = loaded?.link;
  const position =
    at ??
    (link?.state === "ready" ? link.position : undefined) ??
    (link
      ? new Date(
          (Date.parse(link.archive_interval.start) +
            Date.parse(link.archive_interval.end)) /
            2,
        ).toISOString()
      : new Date(anchor).toISOString());
  const ms = milliseconds(position);
  const entry = loaded?.entries.find(
    (e) =>
      milliseconds(e.interval.start) <= ms && ms < milliseconds(e.interval.end),
  );
  const block =
    link && !at
      ? link.state === "ready"
        ? link.interval
        : undefined
      : entry?.state === "recorded"
        ? entry.interval
        : undefined;
  const playbackWindow = useMemo(
    () => (link?.state === "ready" && !at ? link.interval : windowAround(ms)),
    [link, at, ms],
  );
  const liveTarget = useMemo<Target>(
    () => ({ mode: "live", point: point.id }),
    [point.id],
  );
  const archiveTarget = useMemo<Target | null>(
    () =>
      block
        ? {
            mode: "archive",
            point: point.id,
            interval: playbackWindow,
            position,
          }
        : null,
    [block, point.id, playbackWindow, position],
  );
  const target = archive ? archiveTarget : liveTarget;
  let notice = failed
    ? "Не удалось прочитать архив"
    : loaded
      ? detail(entry)
      : "Загрузка архива…";
  if (link && !at && link.state !== "ready") {
    if (link.state === "uncertain_time")
      notice = "Нельзя точно сопоставить время тревоги с записью";
    else if (link.state === "pending_catalog")
      notice = "Запись добавляется в каталог";
    else if (entry?.state !== "gap")
      notice =
        (link.state === "gap" ? "Пропуск записи. " : "Запись недоступна. ") +
        (("reason" in link ? reasons[link.reason] : undefined) ??
          "Нет сведений");
  }
  const data: TimelineData = failed
    ? { status: "error", message: "Проверьте соединение с площадкой." }
    : !loaded
      ? { status: "loading" }
      : !loaded.entries.length
        ? { status: "empty" }
        : {
            status: "ready",
            window: {
              startMs: Date.parse(loaded.window.start),
              endMs: Date.parse(loaded.window.end),
            },
            entries: loaded.entries.map(timelineEntry),
          };
  return (
    <div className="ws-watch">
      <SessionPlayer
        attempt={attempt}
        headingRef={heading}
        services={services}
        site={site}
        target={target}
        title={point.name}
        mode={archive ? "archive" : "live"}
        notice={notice}
        onMode={onMode}
        retry={retry}
        {...(archive ? { position } : {})}
        {...(alert
          ? {
              back: () =>
                onBack(position, loaded?.window ?? windowAround(anchor)),
            }
          : {})}
      />
      {!archive && point.ptz && (
        <PtzControls
          key={point.ptz.device_id}
          commands={point.ptz.commands}
          send={(command) =>
            services.ptz(site, point.id, point.ptz!.device_id, command)
          }
        />
      )}
      <ArchiveTimeline
        data={data}
        positionMs={ms}
        onRefresh={retry}
        onPositionChange={onSeek}
      />
    </div>
  );
}
export function Monitor({
  services,
  site,
  points,
  feed,
}: {
  services: Services;
  site: string;
  points: Point[];
  feed: Feed;
}) {
  const [params, set] = useSearchParams();
  useEffect(() => {
    if (
      params.get("mode") !== "archive" ||
      params.has("alert") ||
      params.has("at")
    )
      return;
    const next = new URLSearchParams(params);
    next.set("at", new Date().toISOString());
    void set(next, { replace: true });
  }, [params, set]);
  const chosen = params.get("point"),
    alert = params.get("alert"),
    focus = params.get("focus");
  const point = chosen ? points.find((p) => p.id === chosen) : points[0];
  const list = useRef<HTMLUListElement>(null);
  const offset = Number(params.get("scroll")) || 0;
  const start = params.get("from"),
    end = params.get("to");
  const restoredWindow = useMemo(
    () =>
      start && end && Date.parse(start) < Date.parse(end)
        ? { start, end }
        : null,
    [start, end],
  );
  useEffect(() => {
    if (focus && !alert) {
      document.getElementById("alert-" + focus)?.focus({ preventScroll: true });
      if (list.current) list.current.scrollTop = offset;
    }
  }, [alert, focus, feed.status, offset]);
  function update(values: Record<string, string | null>, replace = false) {
    const next = new URLSearchParams(params);
    for (const [key, value] of Object.entries(values)) {
      if (value === null) next.delete(key);
      else next.set(key, value);
    }
    void set(next, { replace });
  }
  const at = params.get("at");
  const auditParams = new URLSearchParams(params);
  auditParams.delete("alert_id");
  auditParams.set("site", site);
  const validAt = at && Number.isFinite(Date.parse(at)) ? at : null;
  return (
    <>
      <div className="ws-title">
        <h1>Мониторинг</h1>
        <Link className="ws-audit-link" to={"/audit?" + auditParams.toString()}>
          Журнал тревог
        </Link>
        <label>
          Точка наблюдения{" "}
          <select
            value={point?.id ?? ""}
            onChange={(e) =>
              update({
                point: e.target.value,
                alert: null,
                focus: null,
                at: null,
                from: null,
                to: null,
                scroll: null,
              })
            }
          >
            {points.map((p) => (
              <option key={p.id} value={p.id}>
                {p.name}
              </option>
            ))}
          </select>
        </label>
      </div>
      <div className="ws-monitor">
        {point ? (
          <View
            key={site + point.id + (alert ?? "")}
            services={services}
            site={site}
            point={point}
            alert={alert}
            at={validAt}
            archive={!!alert || params.get("mode") === "archive"}
            restoredWindow={restoredWindow}
            onMode={(mode) =>
              update({ mode, alert: null, focus: alert ?? focus })
            }
            onSeek={(ms) =>
              update({ at: new Date(ms).toISOString(), mode: "archive" }, true)
            }
            onBack={(at, window) =>
              update({
                focus: alert,
                alert: null,
                mode: "live",
                at,
                from: window.start,
                to: window.end,
              })
            }
          />
        ) : (
          <p role="status">Нет доступных точек</p>
        )}
        <AlertList
          feed={feed}
          points={points}
          selected={alert ?? focus}
          listRef={list}
          onOpen={(item) => {
            update({
              point: item.point_id,
              scroll: String(list.current?.scrollTop ?? 0),
              from: null,
              to: null,
              alert: item.id,
              at: null,
              focus: null,
              mode: "archive",
            });
          }}
        />
      </div>
      {alert && point && (
        <Suspense fallback={<p role="status">Загрузка карточки…</p>}>
          <AlertWorkPanel
            key={site + point.id + alert}
            services={services}
            site={site}
            alert={alert}
            point={point.id}
          />
        </Suspense>
      )}
    </>
  );
}
