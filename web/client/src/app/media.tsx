import { useEffect, useRef, useState } from "react";
import type {
  Services,
  Target,
  Entry,
  Interval,
  AlertLink,
} from "../contracts/workspace";
import { MediaPanel } from "../ui-kit/MediaPanel";
import type { PlayerState } from "../ui-kit/MediaPanel";
import type { ComponentProps } from "react";
import type { TimelineEntry } from "../ui-kit/ArchiveTimeline";

export const reasons: Record<string, string> = {
  source_offline: "Нет связи с камерой",
  storage_failure: "Ошибка хранения",
  overload: "Перегрузка",
  media_offline: "Том недоступен",
  unknown_coverage: "Нет сведений",
  outside_retention: "Истёк срок хранения",
  corrupt: "Запись повреждена",
};
export function milliseconds(utc: string) {
  const fraction = utc.match(/\.\d{3}(\d+)Z$/)?.[1];
  return Date.parse(utc) + (fraction ? Number("0." + fraction) : 0);
}
export function timelineEntry(entry: Entry): TimelineEntry {
  const bounds = {
    startMs: milliseconds(entry.interval.start),
    endMs: milliseconds(entry.interval.end),
  };
  switch (entry.state) {
    case "gap":
    case "unavailable":
      return {
        ...bounds,
        kind: entry.state,
        reason: reasons[entry.reason] ?? "Нет сведений",
      };
    case "pending_catalog":
      return { ...bounds, kind: "pending" };
    case "recorded":
      return entry.time_basis === "source_clock" && entry.uncertainty_us === "0"
        ? { ...bounds, kind: "recorded" }
        : {
            ...bounds,
            kind: "estimated",
            ...(entry.time_basis === "source_clock"
              ? { uncertaintyMs: Number(entry.uncertainty_us) / 1000 }
              : {}),
          };
  }
}
export function windowAround(at: number): Interval {
  return {
    start: new Date(at - 180000).toISOString(),
    end: new Date(at + 180000).toISOString(),
  };
}
export function alertArchiveNotice(link: AlertLink) {
  switch (link.state) {
    case "ready":
      return "Запись доступна";
    case "uncertain_time":
      return "Нельзя точно сопоставить время тревоги с записью";
    case "pending_catalog":
      return "Запись добавляется в каталог";
    case "gap":
      return "Пропуск записи. " + (reasons[link.reason] ?? "Нет сведений");
    case "unavailable":
      return "Запись недоступна. " + (reasons[link.reason] ?? "Нет сведений");
  }
}

export function detail(entry: Entry | undefined) {
  if (!entry) return "Нет сведений о записи";
  if (entry.state === "pending_catalog") return "Запись добавляется в каталог";
  if (entry.state === "recorded") return "Запись доступна";
  return (
    (entry.state === "gap" ? "Пропуск записи" : "Запись недоступна") +
    ". " +
    (reasons[entry.reason] ?? "Нет сведений") +
    " · " +
    entry.interval.start +
    " - " +
    entry.interval.end
  );
}

type Props = Omit<ComponentProps<typeof MediaPanel>, "videoRef" | "state"> & {
  services: Services;
  site: string;
  target: Target | null;
  notice: string;
  attempt?: number;
  onFailure?: () => void;
  onFrame?: () => void;
};
export function SessionPlayer({
  services,
  site,
  target,
  notice,
  onFrame,
  onFailure,
  attempt = 0,
  ...view
}: Props) {
  const video = useRef<HTMLVideoElement>(null);
  const [last, setLast] = useState<{
    target: Target;
    attempt: number;
    state: PlayerState;
  } | null>(null);
  const state: PlayerState =
    last && last.target === target && last.attempt === attempt
      ? last.state
      : { kind: "loading", message: "Загрузка видео…" };
  useEffect(() => {
    const element = video.current;
    if (!target || !element) return;
    const ctl = new AbortController();
    const setState = (state: PlayerState) =>
      setLast({ target, attempt, state });
    const fail = (message: string) => {
      if (!ctl.signal.aborted) {
        ctl.abort();
        setState({ kind: "error", message });
        onFailure?.();
      }
    };
    void services
      .attach(element, site, target, ctl.signal, fail)
      .then(() => {
        if (ctl.signal.aborted) return;
        setState({
          kind: "ready",
          message:
            target.mode === "live" ? "Эфир подключён" : "Кадр записи получен",
        });
        onFrame?.();
      })
      .catch(() => fail("Видео недоступно. Повторите подключение."));
    return () => ctl.abort();
  }, [services, site, target, attempt, onFrame, onFailure]);
  return (
    <MediaPanel
      {...view}
      videoRef={video}
      state={target ? state : { kind: "idle", message: notice }}
    />
  );
}
