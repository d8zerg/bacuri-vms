import { useId, useRef, useState } from "react";
import type { KeyboardEvent } from "react";
import { IconButton } from "./FormControls";
import "./archive-timeline.css";
import {
  dateTimeFormat,
  formatNumber,
  t,
  useT,
  type MessageKey,
} from "../i18n";

type Interval = Readonly<{ startMs: number; endMs: number }>;

type Evidence =
  | { kind: "recorded" }
  | { kind: "estimated"; uncertaintyMs?: number }
  | { kind: "gap"; reason: string }
  | { kind: "pending" }
  | { kind: "unavailable"; reason: string };

export type TimelineEntry = Interval & Evidence;

export type TimelineData =
  | { status: "loading" }
  | { status: "empty" }
  | { status: "error"; message: string }
  | { status: "ready"; window: Interval; entries: readonly TimelineEntry[] };

export interface ArchiveTimelineProps {
  data: TimelineData;
  positionMs: number;
  onPositionChange: (utcMs: number) => void;
  onRefresh: () => void;
}

const labels = {
  recorded: "timeline.recorded",
  estimated: "timeline.estimated",
  gap: "timeline.gap",
  pending: "timeline.pending",
  unavailable: "timeline.unavailable",
  unknown: "timeline.unknown",
} as const satisfies Record<string, MessageKey>;

const clockOptions: Intl.DateTimeFormatOptions = {
  timeZone: "UTC",
  hour: "2-digit",
  minute: "2-digit",
  second: "2-digit",
  hourCycle: "h23",
};
const dayOptions: Intl.DateTimeFormatOptions = {
  timeZone: "UTC",
  day: "numeric",
  month: "short",
  year: "numeric",
};

const arrowStepMs: Readonly<Partial<Record<string, number>>> = {
  ArrowLeft: -1000,
  ArrowDown: -1000,
  ArrowRight: 1000,
  ArrowUp: 1000,
};

function time(ms: number) {
  const fraction = new Date(ms).getUTCMilliseconds();
  return (
    dateTimeFormat(clockOptions).format(ms) +
    (fraction ? "," + String(fraction).padStart(3, "0") : "")
  );
}

function describe(entry: TimelineEntry | undefined) {
  if (!entry) return t(labels.unknown);
  switch (entry.kind) {
    case "gap":
    case "unavailable":
      return t(labels[entry.kind]) + ". " + entry.reason;
    case "estimated":
      return (
        t(labels.estimated) +
        (entry.uncertaintyMs === undefined
          ? ". " + t("timeline.uncertaintyUnknown")
          : " ±" +
            t("timeline.seconds", {
              value: formatNumber(entry.uncertaintyMs / 1000, {
                maximumFractionDigits: 6,
              }),
            }))
      );
    default:
      return t(labels[entry.kind]);
  }
}

function SelectedInterval({ entry }: { entry: TimelineEntry | undefined }) {
  return (
    <div className="bt-detail">
      <span
        aria-hidden="true"
        className="bt-swatch"
        data-kind={entry?.kind ?? "unknown"}
      />
      <strong>{describe(entry)}</strong>
      {entry && (
        <span className="bt-bounds">
          {dateTimeFormat(dayOptions).format(entry.startMs)} ·{" "}
          {time(entry.startMs)} -{" "}
          {dateTimeFormat(dayOptions).format(entry.startMs) !==
            dateTimeFormat(dayOptions).format(entry.endMs) &&
            dateTimeFormat(dayOptions).format(entry.endMs) + " · "}
          {time(entry.endMs)} UTC
        </span>
      )}
    </div>
  );
}

export function ArchiveTimeline({
  data,
  positionMs,
  onPositionChange,
  onRefresh,
}: ArchiveTimelineProps) {
  const t = useT();
  const id = useId();
  return (
    <section className="bt-timeline" aria-labelledby={id}>
      <header className="bt-header">
        <h2 id={id}>{t("timeline.title")}</h2>
        <IconButton
          icon="rotate-cw"
          label={t("timeline.refresh")}
          aria-disabled={data.status === "loading"}
          onClick={onRefresh}
        />
      </header>
      <div
        role="status"
        className="bt-status"
        data-ready={data.status === "ready" || undefined}
      >
        {data.status === "loading" && t("archive.loading")}
        {data.status === "empty" && t("timeline.empty")}
        {data.status === "error" &&
          t("timeline.failed", { message: data.message })}
        {data.status === "ready" && t("timeline.ready")}
      </div>
      {data.status === "ready" && (
        <TimelineWindow
          key={data.window.startMs + "/" + data.window.endMs}
          data={data}
          positionMs={positionMs}
          onPositionChange={onPositionChange}
        />
      )}
    </section>
  );
}

function TimelineWindow({
  data,
  positionMs,
  onPositionChange,
}: {
  data: Extract<TimelineData, { status: "ready" }>;
  positionMs: number;
  onPositionChange: (ms: number) => void;
}) {
  const t = useT();
  const id = useId();
  const [preview, setPreview] = useState<number | null>(null);
  const interaction = useRef<number | string | null>(null);
  const shown = preview ?? positionMs;
  const entry = data.entries.find(
    (item) => item.startMs <= shown && shown < item.endMs,
  );

  function cancel() {
    interaction.current = null;
    setPreview(null);
  }
  function finish() {
    if (preview !== null && preview !== positionMs) onPositionChange(preview);
    cancel();
  }
  function step(event: KeyboardEvent<HTMLInputElement>) {
    const delta = arrowStepMs[event.key];
    if (
      delta === undefined &&
      !["Home", "End", "PageUp", "PageDown"].includes(event.key)
    )
      return;
    interaction.current = event.key;
    if (delta !== undefined) {
      event.preventDefault();
      const input = event.currentTarget;
      setPreview(
        Math.max(
          Number(input.min),
          Math.min(Number(input.max), input.valueAsNumber + delta),
        ),
      );
    }
  }

  return (
    <>
      <div className="bt-readout">
        <span>{dateTimeFormat(dayOptions).format(shown)}</span>
        <strong>{time(shown)} UTC</strong>
      </div>
      <div className="bt-track">
        <div className="bt-bands" aria-hidden="true">
          {data.entries.map((item) => {
            const start = Math.max(data.window.startMs, item.startMs);
            const end = Math.min(data.window.endMs, item.endMs);
            if (end <= start) return null;
            const duration = data.window.endMs - data.window.startMs;
            return (
              <span
                key={item.startMs}
                data-kind={item.kind}
                style={{
                  left: ((start - data.window.startMs) / duration) * 100 + "%",
                  width: ((end - start) / duration) * 100 + "%",
                }}
              />
            );
          })}
        </div>
        <input
          type="range"
          min={data.window.startMs}
          max={data.window.endMs - 1}
          step={1}
          value={shown}
          aria-label={t("timeline.position")}
          aria-valuetext={
            dateTimeFormat(dayOptions).format(shown) +
            ", " +
            time(shown) +
            " UTC. " +
            describe(entry)
          }
          aria-describedby={id + "-detail"}
          onKeyDown={step}
          onKeyUp={(e) => {
            if (interaction.current === e.key) finish();
          }}
          onPointerDown={(e) => {
            if (e.isPrimary && e.button === 0) {
              interaction.current = e.pointerId;
              e.currentTarget.setPointerCapture(e.pointerId);
            }
          }}
          onPointerUp={(e) => {
            if (interaction.current === e.pointerId) finish();
          }}
          onLostPointerCapture={(e) => {
            if (interaction.current === e.pointerId) cancel();
          }}
          onPointerCancel={cancel}
          onBlur={() => {
            if (typeof interaction.current === "string") finish();
            else cancel();
          }}
          onChange={(event) => {
            const next = event.currentTarget.valueAsNumber;
            if (interaction.current !== null) setPreview(next);
            else if (next !== positionMs) onPositionChange(next);
          }}
        />
      </div>
      <div className="bt-ticks" aria-hidden="true">
        <span>{time(data.window.startMs)}</span>
        <span>{time(data.window.endMs)}</span>
      </div>
      <div id={id + "-detail"}>
        <SelectedInterval entry={entry} />
      </div>
      <ul className="bt-legend" aria-label={t("timeline.legend")}>
        {Object.entries(labels).map(([kind, label]) => (
          <li key={kind}>
            <span aria-hidden="true" className="bt-swatch" data-kind={kind} />
            {t(label)}
          </li>
        ))}
      </ul>
    </>
  );
}
