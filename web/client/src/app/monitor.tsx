import { lazy, Suspense, useEffect, useMemo, useRef, useState } from "react";
import { useSearchParams } from "react-router";
import type {
  AlertLink,
  Entry,
  Feed,
  Interval,
  Point,
  Services,
  Target,
} from "../contracts/workspace";
import { AlertList } from "../ui-kit/AlertList";
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
import { SubsectionMenu } from "./SubsectionMenu";
import { IconButton } from "../ui-kit/FormControls";
const MonitorGrid = lazy(() =>
  import("./MonitorGrid").then((m) => ({ default: m.MonitorGrid })),
);
const CameraConsole = lazy(() =>
  import("./CameraConsole").then((m) => ({ default: m.CameraConsole })),
);
const ArchiveTimeline = lazy(() =>
  import("../ui-kit/ArchiveTimeline").then((m) => ({
    default: m.ArchiveTimeline,
  })),
);
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
  onSeek,
  onBack,
}: {
  services: Services;
  site: string;
  point: Point;
  alert: string | null;
  at: string | null;
  archive: boolean;
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
    if (!archive) return;
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
      if (!ctl.signal.aborted) {
        setFailed(false);
        setLoaded({ entries, window, ...(link ? { link } : {}) });
      }
    }
    void load().catch(() => {
      if (!ctl.signal.aborted) setFailed(true);
    });
    return () => ctl.abort();
  }, [
    archive,
    services,
    site,
    point.id,
    alert,
    anchor,
    attempt,
    restoredWindow,
  ]);
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
  const alertInterval = link?.state === "ready" && !at ? link.interval : null;
  const playable = link && !at ? !!alertInterval : entry?.state === "recorded";
  const start = alertInterval?.start,
    end = alertInterval?.end;
  const playbackWindow = useMemo(
    () => (start && end ? { start, end } : windowAround(ms)),
    [start, end, ms],
  );
  const liveTarget = useMemo<Target>(
    () => ({ mode: "live", point: point.id }),
    [point.id],
  );
  const archiveTarget = useMemo<Target | null>(
    () =>
      playable
        ? {
            mode: "archive",
            point: point.id,
            interval: playbackWindow,
            position,
          }
        : null,
    [playable, point.id, playbackWindow, position],
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
        key={archive ? "archive" : "live"}
        attempt={attempt}
        headingRef={heading}
        services={services}
        site={site}
        target={target}
        title={point.name}
        mode={archive ? "archive" : "live"}
        controls={
          (
            archive
              ? point.audio?.archive
              : point.audio?.listen ||
                point.audio?.record ||
                point.audio?.talk ||
                point.ptz
          ) ? (
            <Suspense fallback={null}>
              <CameraConsole
                key={[site, point.id, point.ptz?.device_id, archive].join("/")}
                services={services}
                site={site}
                point={point.id}
                ptz={archive ? undefined : point.ptz}
                audio={point.audio}
                position={archive ? ms * 1000 : undefined}
              />
            </Suspense>
          ) : undefined
        }
        notice={notice}
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
      {archive && (
        <Suspense fallback={null}>
          <ArchiveTimeline
            data={data}
            positionMs={ms}
            onRefresh={retry}
            onPositionChange={onSeek}
          />
        </Suspense>
      )}
    </div>
  );
}
export function Monitor({
  admin,
  services,
  site,
  points,
  feed,
}: {
  admin: boolean;
  services: Services;
  site: string;
  points: Point[];
  feed: Feed;
}) {
  useEffect(() => {
    if (
      feed.status === "online" &&
      feed.items.length &&
      !performance.getEntriesByName("workspace-action").length
    )
      performance.mark("workspace-action");
  }, [feed]);
  const [feedQuery, setFeedQuery] = useState("");
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
  const archive =
    params.get("mode") === "archive" ||
    (!!alert && params.get("mode") !== "live");
  const grid = params.get("grid") === "1" && !archive;
  function closeGrid() {
    update({ grid: null, wall: null, layout: null, mode: "live" });
  }
  const validAt = at && Number.isFinite(Date.parse(at)) ? at : null;
  return (
    <>
      <SubsectionMenu
        label="Подразделы мониторинга"
        items={[
          {
            label: "Эфир",
            icon: "radio",
            active: !archive,
            disabled: !point,
            select: () => {
              if (archive || grid) closeGrid();
            },
          },
          {
            label: "Архив",
            icon: "archive",
            active: archive,
            disabled: !point,
            select: () => {
              if (!archive) {
                if (!alert && !validAt && !restoredWindow) {
                  const now = Date.now();
                  const window = windowAround(now);
                  update({
                    mode: "archive",
                    at: new Date(now).toISOString(),
                    from: window.start,
                    to: window.end,
                  });
                } else update({ mode: "archive" });
              }
            },
          },
          {
            label: "К тревогам",
            disabled: grid,
            icon: "bell-ring",
            className: "ws-feed-jump",
            select: () => {
              const heading = document.getElementById("feed-heading");
              heading?.focus({ preventScroll: true });
              heading?.scrollIntoView({ block: "start" });
            },
          },
        ]}
      />
      {grid ? (
        <Suspense fallback={<p role="status">Загрузка раскладки…</p>}>
          <MonitorGrid
            key={site}
            site={site}
            services={services}
            points={points}
            admin={admin}
            back={closeGrid}
          />
        </Suspense>
      ) : (
        <>
          <div className="ws-point-toolbar">
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
            {!archive && (
              <IconButton
                icon="layers"
                label="Многоканальный эфир"
                onClick={() =>
                  update({ grid: "1", mode: "live", alert: null, focus: null })
                }
              />
            )}
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
          <div className="ws-monitor">
            {point ? (
              <View
                key={site + point.id + (alert ?? "")}
                services={services}
                site={site}
                point={point}
                alert={alert}
                at={validAt}
                archive={archive}
                restoredWindow={restoredWindow}
                returnFocus={alert ? null : focus}
                onSeek={(ms) =>
                  update(
                    { at: new Date(ms).toISOString(), mode: "archive" },
                    true,
                  )
                }
                onBack={(at, window) => {
                  if (alert && !document.getElementById("alert-" + alert))
                    setFeedQuery("");
                  update({
                    focus: alert,
                    state:
                      filter &&
                      feed.items.find((item) => item.id === alert)
                        ?.work_state === filter
                        ? filter
                        : null,
                    alert: null,
                    mode: "live",
                    at,
                    from: window.start,
                    to: window.end,
                  });
                }}
              />
            ) : (
              <p className="ws-panel ws-pad" role="status">
                {points.length
                  ? "Эта точка недоступна. Выберите другую точку наблюдения."
                  : "Нет доступных точек"}
              </p>
            )}
            <AlertList
              search={{ query: feedQuery, change: setFeedQuery }}
              feed={feed}
              points={points}
              selected={alert ?? focus}
              listRef={list}
              filter={filter}
              onFilter={(state) =>
                update(
                  { state: state || null, focus: null, scroll: null },
                  true,
                )
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
      )}
    </>
  );
}
