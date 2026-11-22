import { useCallback, useEffect, useId, useMemo, useState } from "react";
import { Link } from "react-router";
import type {
  AlertItem,
  AlertLink,
  Feed,
  Point,
  Services,
  Target,
} from "../contracts/workspace";
import { alertName, alertStates } from "../ui-kit/alertLabels";
import { Button } from "../ui-kit/FormControls";
import { SessionPlayer, alertArchiveNotice } from "./media";
import { formatUtc, milliseconds } from "../ui-kit/utc";
import { pause } from "../workflows/pause";
import icons from "../../../../design/bacuri/icons/sprite.svg";

function EventPreview({
  services,
  site,
  point,
  event,
  monitor,
}: {
  services: Services;
  site: string;
  point: Point;
  event: AlertItem;
  monitor: string;
}) {
  const [mode, setMode] = useState<"archive" | "live">("archive");
  const [link, setLink] = useState<AlertLink | null>(null);
  const [failed, setFailed] = useState(false);
  const [mediaFailed, setMediaFailed] = useState(false);
  const playbackFailure = useCallback(() => setMediaFailed(true), []);
  const [attempt, retry] = useState(0);
  const resolveArchive = mode === "archive" && link?.state !== "ready";
  useEffect(() => {
    if (!resolveArchive) return;
    const ctl = new AbortController();
    async function read() {
      let delay = 1000;
      while (!ctl.signal.aborted) {
        const result = await services.resolve(site, event.id, ctl.signal);
        ctl.signal.throwIfAborted();
        if (result.point_id !== point.id) throw Error("Point mismatch");
        setFailed(false);
        setLink(result);
        if (result.state !== "pending_catalog") return;
        await pause(delay, ctl.signal);
        delay = Math.min(5000, delay * 2);
      }
    }
    void read().catch(() => {
      if (!ctl.signal.aborted) setFailed(true);
    });
    return () => ctl.abort();
  }, [services, site, event.id, point.id, attempt, resolveArchive]);
  const target = useMemo<Target | null>(
    () =>
      mode === "live"
        ? { mode: "live", point: point.id }
        : link?.state === "ready"
          ? {
              mode: "archive",
              point: point.id,
              interval: link.interval,
              position: link.position,
            }
          : null,
    [mode, point.id, link],
  );
  const notice = failed
    ? "Не удалось проверить запись"
    : link?.state === "pending_catalog"
      ? "Запись добавляется в каталог. Просмотр откроется автоматически."
      : link
        ? alertArchiveNotice(link)
        : "Проверка записи…";
  const canRetry =
    mediaFailed ||
    (mode === "archive" &&
      (failed ||
        (link && link.state !== "ready" && link.state !== "pending_catalog")));
  function retryPreview() {
    setMediaFailed(false);
    setFailed(false);
    setLink(null);
    retry((n) => n + 1);
  }
  return (
    <>
      <SessionPlayer
        services={services}
        site={site}
        target={target}
        notice={notice}
        title={mode === "archive" ? "Запись события" : "Эфир камеры"}
        mode={mode}
        onMode={(next) => {
          if (next !== mode) {
            setMediaFailed(false);
            setMode(next);
          }
        }}
        onFailure={playbackFailure}
        attempt={attempt}
        {...(canRetry ? { retry: retryPreview } : {})}
      />
      <Link className="ws-event-open" to={monitor}>
        Открыть тревогу в мониторинге
      </Link>
    </>
  );
}

const pageSize = 3;
export function CameraEvents({
  services,
  site,
  point,
  feed,
  review,
  onReview,
  monitor,
}: {
  services: Services;
  site: string;
  point: Point;
  feed: Feed;
  review: string | null;
  onReview: (id: string | null) => void;
  monitor: (id: string) => string;
}) {
  const [limit, setLimit] = useState(pageSize);
  const heading = useId();
  const events = useMemo(
    () =>
      feed.items
        .filter((item) => item.point_id === point.id)
        .sort(
          (a, b) => milliseconds(b.observed_at) - milliseconds(a.observed_at),
        ),
    [feed.items, point.id],
  );
  const visible = events.filter(
    (item, index) => index < limit || item.id === review,
  );
  const status =
    feed.status === "loading"
      ? "Загрузка событий…"
      : feed.status === "denied"
        ? "Доступ к событиям закрыт"
        : feed.status === "reconnecting"
          ? "Связь с лентой прервана. Ожидаем обновления."
          : !events.length
            ? "В текущей ленте нет событий этой камеры."
            : "";
  return (
    <section className="ws-camera-events" aria-labelledby={heading}>
      <h3 id={heading}>События камеры</h3>
      {status && <p role="status">{status}</p>}
      {feed.status !== "denied" && visible.length > 0 && (
        <>
          <ol>
            {visible.map((item) => {
              const open = review === item.id;
              return (
                <li key={item.id}>
                  <button
                    className="ws-event-row"
                    type="button"
                    aria-expanded={open}
                    id={"map-alert-" + item.id}
                    aria-controls={
                      open ? "map-event-preview-" + item.id : undefined
                    }
                    onClick={() => onReview(open ? null : item.id)}
                  >
                    <strong>{alertName(item.type)}</strong>
                    {item.work_state && (
                      <span
                        className="ws-event-state"
                        data-state={item.work_state}
                      >
                        {alertStates[item.work_state]}
                      </span>
                    )}
                    <time dateTime={item.observed_at}>
                      {formatUtc(item.observed_at)}
                    </time>
                    <span className="ws-event-action">
                      {!open && (
                        <svg aria-hidden="true">
                          <use href={icons + "#i-play"} />
                        </svg>
                      )}
                      {open ? "Свернуть" : "Проверить"}
                    </span>
                  </button>
                  {open && (
                    <div
                      className="ws-event-preview"
                      id={"map-event-preview-" + item.id}
                    >
                      <EventPreview
                        services={services}
                        site={site}
                        point={point}
                        event={item}
                        monitor={monitor(item.id)}
                      />
                    </div>
                  )}
                </li>
              );
            })}
          </ol>
          {events.length > visible.length && (
            <Button onClick={() => setLimit((n) => n + pageSize)}>
              Показать ещё ({events.length - visible.length})
            </Button>
          )}
        </>
      )}
    </section>
  );
}
