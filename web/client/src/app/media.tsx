import { milliseconds, formatUtc } from "../ui-kit/utc";
import { useEffect, useRef, useState } from "react";
import type {
  Quality,
  Services,
  Target,
  Entry,
  Interval,
  AlertLink,
} from "../contracts/workspace";
import { MediaPanel } from "../ui-kit/MediaPanel";
import { t, type MessageKey } from "../i18n";
import type { PlayerState } from "../ui-kit/MediaPanel";
import type { ComponentProps } from "react";
import type { TimelineEntry } from "../ui-kit/ArchiveTimeline";

const reasonKeys: Record<string, MessageKey> = {
  source_offline: "gap.sourceOffline",
  storage_failure: "gap.storageFailure",
  overload: "gap.overload",
  keyframe_interval: "gap.keyframeInterval",
  media_offline: "gap.mediaOffline",
  unknown_coverage: "gap.unknown",
  outside_retention: "gap.outsideRetention",
  corrupt: "gap.corrupt",
};
// The name of a gap or unavailability reason reported by the archive.
export function reasonLabel(reason: string) {
  return t(reasonKeys[reason] ?? "gap.unknown");
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
        reason: reasonLabel(entry.reason),
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
      return t("record.available");
    case "uncertain_time":
      return t("record.uncertainTime");
    case "pending_catalog":
      return t("record.pending");
    case "gap":
      return t("record.gapWithReason", { reason: reasonLabel(link.reason) });
    case "unavailable":
      return t("record.unavailableWithReason", {
        reason: reasonLabel(link.reason),
      });
  }
}

export function detail(entry: Entry | undefined) {
  if (!entry) return t("record.unknown");
  if (entry.state === "pending_catalog") return t("record.pending");
  if (entry.state === "recorded") return t("record.available");
  return (
    t(
      entry.state === "gap"
        ? "record.gapWithReason"
        : "record.unavailableWithReason",
      {
        reason: reasonLabel(entry.reason),
      },
    ) +
    " · " +
    formatUtc(entry.interval.start) +
    " - " +
    formatUtc(entry.interval.end)
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
  const [quality, setQuality] = useState<Quality>("auto");
  const [width, setWidth] = useState(0);
  const [renewal, renew] = useState(0);
  const [format, setFormat] = useState("");
  const live = target?.mode === "live";
  useEffect(() => {
    const el = video.current;
    if (!el || !live) return;
    setWidth(Math.max(1, Math.round(el.clientWidth)));
    let timer: ReturnType<typeof setTimeout>;
    const observer = new ResizeObserver(([entry]) => {
      if (!entry || entry.contentRect.width < 1) return;
      clearTimeout(timer);
      timer = setTimeout(
        () =>
          setWidth((prev) =>
            Math.abs(prev - entry.contentRect.width) > 48
              ? Math.round(entry.contentRect.width)
              : prev,
          ),
        350,
      );
    });
    observer.observe(el);
    return () => {
      observer.disconnect();
      clearTimeout(timer);
    };
  }, [live]);
  const negotiated = live && quality === "auto" ? width : 0;
  const [last, setLast] = useState<{
    target: Target;
    attempt: number;
    state: PlayerState;
  } | null>(null);
  const state: PlayerState =
    last && last.target === target && last.attempt === attempt
      ? last.state
      : { kind: "loading", message: t("video.loading") };
  useEffect(() => {
    const element = video.current;
    if (!target || !element || (live && quality === "auto" && !negotiated))
      return;
    setFormat("");
    const ctl = new AbortController();
    const setState = (state: PlayerState) =>
      setLast({ target, attempt, state });
    const fail = (message: string) => {
      if (!ctl.signal.aborted) {
        ctl.abort();
        setFormat("");
        setState({ kind: "error", message });
        onFailure?.();
      }
    };
    setState({ kind: "loading", message: t("video.loading") });
    void services
      .attach(
        element,
        site,
        target.mode === "live"
          ? { ...target, quality, width: negotiated || element.clientWidth }
          : target,
        ctl.signal,
        fail,
        (text) => {
          if (!ctl.signal.aborted) setFormat(text);
        },
        () => {
          if (!ctl.signal.aborted) renew((n) => n + 1);
        },
      )
      .then(() => {
        if (ctl.signal.aborted) return;
        setState({
          kind: "ready",
          message:
            target.mode === "live"
              ? t("video.liveConnected")
              : t("video.frameReceived"),
        });
        onFrame?.();
      })
      .catch(() => fail(t("video.unavailable")));
    const hide = () => ctl.abort();
    const show = (event: PageTransitionEvent) => {
      if (event.persisted) renew((n) => n + 1);
    };
    window.addEventListener("pagehide", hide);
    window.addEventListener("pageshow", show);
    return () => {
      window.removeEventListener("pagehide", hide);
      window.removeEventListener("pageshow", show);
      ctl.abort();
    };
  }, [
    services,
    site,
    target,
    attempt,
    onFrame,
    onFailure,
    quality,
    negotiated,
    live,
    renewal,
  ]);
  return (
    <MediaPanel
      {...view}
      description={format}
      {...(live ? { quality, onQuality: setQuality } : {})}
      videoRef={video}
      state={target ? state : { kind: "idle", message: notice }}
    />
  );
}
