import { lazy, Suspense, useEffect, useMemo, useRef, useState } from "react";
import { useSearchParams } from "react-router";
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
  alertArchiveNotice,
  timelineEntry,
  windowAround,
} from "./media";

import { milliseconds } from "../ui-kit/utc";
import { alertStates } from "../ui-kit/alertLabels";
import { Button } from "../ui-kit/FormControls";
import { cameraError } from "./CameraSelection";
const FragmentExport = lazy(() => import("./FragmentExport"));
const AlertWorkPanel = lazy(() =>
  import("./AlertWork").then((m) => ({ default: m.AlertWorkPanel })),
);
const connectionLabels = {
  online: "Камера на связи",
  offline: "Нет связи с камерой",
  unknown: "Связь с камерой неизвестна",
};
const recordingLabels = {
  current: "Запись ведётся",
  delayed: "Запись запаздывает",
  stopped: "Запись остановлена",
  unknown: "Состояние записи неизвестно",
};
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
  returnFocus,
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
  returnFocus: string | null;
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
  useEffect(() => {
    if (!returnFocus || (!loaded && !failed)) return;
    const row = document.getElementById("alert-" + returnFocus);
    if (row && row === document.activeElement)
      row.scrollIntoView({ block: "nearest" });
  }, [returnFocus, loaded, failed]);
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
  if (
    link &&
    !at &&
    link.state !== "ready" &&
    (entry?.state !== "gap" ||
      link.state === "uncertain_time" ||
      link.state === "pending_catalog")
  )
    notice = alertArchiveNotice(link);
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
      {alert && (
        <Suspense fallback={<p role="status">Загрузка карточки…</p>}>
          <AlertWorkPanel
            services={services}
            site={site}
            alert={alert}
            point={point.id}
          />
        </Suspense>
      )}
      {archive && loaded && point.can_export && (
        <Suspense fallback={null}>
          <FragmentExport
            key={[
              site,
              point.id,
              alert,
              loaded.window.start,
              loaded.window.end,
            ].join("/")}
            services={services}
            site={site}
            point={point.id}
            alert={alert}
            interval={loaded.window}
          />
        </Suspense>
      )}
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
  const restoredFocus = useRef<string | null>(null);
  useEffect(() => {
    if (alert || !focus) {
      restoredFocus.current = null;
      return;
    }
    if (restoredFocus.current === focus) return;
    const row = document.getElementById("alert-" + focus);
    if (!row && feed.status !== "online") return;
    if (list.current) list.current.scrollTop = offset;
    const destination = row ?? document.getElementById("feed-heading");
    destination?.focus({ preventScroll: true });
    destination?.scrollIntoView({ block: "nearest" });
    restoredFocus.current = focus;
  }, [alert, focus, feed.items, feed.status, offset]);
  function update(values: Record<string, string | null>, replace = false) {
    const next = new URLSearchParams(params);
    for (const [key, value] of Object.entries(values)) {
      if (value === null) next.delete(key);
      else next.set(key, value);
    }
    void set(next, { replace });
  }
  const at = params.get("at");
  const requestedFilter = params.get("state") ?? "";
  const filter = Object.hasOwn(alertStates, requestedFilter)
    ? requestedFilter
    : "";
  const validAt = at && Number.isFinite(Date.parse(at)) ? at : null;
  return (
    <>
      <div className="ws-title">
        <div>
          <h1>Мониторинг</h1>
          {point && (
            <div className="ws-point-health">
              <span data-state={point.connection}>
                {connectionLabels[point.connection]}
              </span>
              <span data-state={point.recording}>
                {recordingLabels[point.recording]}
              </span>
            </div>
          )}
        </div>
        <a className="ws-feed-jump" href="#feed-heading">
          К тревогам
        </a>
        <label>
          Точка наблюдения{" "}
          <select
            disabled={!points.length}
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
            {!point && (
              <option value="" disabled>
                {points.length ? "Выберите точку" : "Нет доступных точек"}
              </option>
            )}
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
            archive={
              params.get("mode") === "archive" ||
              (!!alert && params.get("mode") !== "live")
            }
            restoredWindow={restoredWindow}
            returnFocus={alert ? null : focus}
            onMode={(mode) => update({ mode })}
            onSeek={(ms) =>
              update({ at: new Date(ms).toISOString(), mode: "archive" }, true)
            }
            onBack={(at, window) =>
              update({
                focus: alert,
                state:
                  filter &&
                  feed.items.find((item) => item.id === alert)?.work_state ===
                    filter
                    ? filter
                    : null,
                alert: null,
                mode: "live",
                at,
                from: window.start,
                to: window.end,
              })
            }
          />
        ) : (
          <p className="ws-panel ws-pad" role="status">
            {points.length
              ? "Эта точка недоступна. Выберите другую точку наблюдения."
              : "Нет доступных точек"}
          </p>
        )}
        <AlertList
          feed={feed}
          points={points}
          selected={alert ?? focus}
          listRef={list}
          filter={filter}
          onFilter={(state) =>
            update({ state: state || null, focus: null, scroll: null }, true)
          }
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
    </>
  );
}
