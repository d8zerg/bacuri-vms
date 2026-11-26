import "../i18n/extended";
import { useEffect, useRef, useState } from "react";
import type {
  CameraControls,
  Point,
  Services,
  VideoRule,
} from "../contracts/workspace";
import { Button, IconButton } from "../ui-kit/FormControls";
import {
  imageCoordinates,
  localDateTime,
  Preview,
  RegionField,
} from "./ArchiveFrame";
import "./investigations.css";
import { locale, shortWeekdays, t as translate, useT } from "../i18n";
const blank: VideoRule = {
  enabled: false,
  source: "local",
  record: true,
  notify: true,
  preset: "",
  threshold: 24,
  min_cells: 3,
  cooldown: 10,
  include: [],
  exclude: [],
  line: null,
  zone: "UTC",
  schedule: [],
};
const full = { left: 0, top: 0, right: 1, bottom: 1 };
const formatMinute = (n: number) =>
  String(Math.floor(n / 60)).padStart(2, "0") +
  ":" +
  String(n % 60).padStart(2, "0");
function minutes(value: string) {
  const [h, m] = value.split(":").map(Number);
  return (h ?? 0) * 60 + (m ?? 0);
}
function CrossingLine({
  line,
  preview,
  change,
}: {
  line: number[];
  preview?: string | undefined;
  change: (value: number[]) => void;
}) {
  const t = useT();
  const drag = useRef<number | null>(null);
  function move(e: React.PointerEvent<HTMLDivElement>, end: number) {
    const { x, y } = imageCoordinates(e);
    const other = end === 0 ? 2 : 0;
    if (Math.hypot(x - line[other]!, y - line[other + 1]!) > 0.015) {
      const next = [...line];
      next.splice(end, 2, x, y);
      change(next);
    }
  }
  return (
    <div className="inv-crossing">
      <div
        className="inv-roi inv-line"
        role="img"
        aria-label={t("rules.line")}
        onPointerDown={(e) => {
          if (!e.isPrimary || e.button !== 0) return;
          const { x, y } = imageCoordinates(e);
          drag.current =
            Math.hypot(x - line[0]!, y - line[1]!) <=
            Math.hypot(x - line[2]!, y - line[3]!)
              ? 0
              : 2;
          e.currentTarget.setPointerCapture(e.pointerId);
          move(e, drag.current);
        }}
        onPointerMove={(e) => {
          if (e.isPrimary && drag.current !== null) move(e, drag.current);
        }}
        onLostPointerCapture={() => {
          drag.current = null;
        }}
      >
        {preview && <Preview pixels={preview} />}
        <svg viewBox="0 0 100 75" aria-hidden="true">
          <line
            x1={line[0]! * 100}
            y1={line[1]! * 75}
            x2={line[2]! * 100}
            y2={line[3]! * 75}
          />
          {[0, 2].map((i) => (
            <circle
              key={i}
              cx={line[i]! * 100}
              cy={line[i + 1]! * 75}
              r="1.7"
            />
          ))}
        </svg>
      </div>
      <details>
        <summary>{t("rules.lineCoordinates")}</summary>
        <div className="inv-bounds">
          {line.map((v, i) => (
            <label key={i}>
              {["X₁, %", "Y₁, %", "X₂, %", "Y₂, %"][i]}
              <input
                type="number"
                min={0}
                max={100}
                value={Math.round(v * 100)}
                onChange={(e) => {
                  const value = e.target.valueAsNumber;
                  if (value >= 0 && value <= 100)
                    change(line.map((p, n) => (n === i ? value / 100 : p)));
                }}
              />
            </label>
          ))}
        </div>
      </details>
    </div>
  );
}
export default function VideoRules({
  services,
  site,
  points,
}: {
  services: Services;
  site: string;
  points: Point[];
}) {
  const t = useT();
  const [selected, setSelected] = useState("");
  const point =
    points.find((p) => p.id === selected)?.id ?? points[0]?.id ?? "";
  return (
    <div className="inv-work">
      <label>
        {t("channel.camera")}
        <select value={point} onChange={(e) => setSelected(e.target.value)}>
          {points.map((p) => (
            <option key={p.id} value={p.id}>
              {p.name}
            </option>
          ))}
        </select>
      </label>
      {point && <Editor key={point} {...{ point, services, site }} />}
    </div>
  );
}
function Editor({
  services,
  site,
  point,
}: {
  services: Services;
  site: string;
  point: string;
}) {
  const t = useT();
  const [rule, update] = useState<VideoRule>(blank),
    [revision, setRevision] = useState<string | null>(null),
    [presets, setPresets] = useState<CameraControls["actions"]>([]);
  const [from, setFrom] = useState(() => localDateTime(Date.now() - 300000)),
    [to, setTo] = useState(() => localDateTime(Date.now()));
  const [trial, setTrial] = useState<{
      hits: string[];
      complete: boolean;
    } | null>(null),
    [notice, setNotice] = useState(""),
    [busy, setBusy] = useState(false),
    [geometry, setGeometry] = useState<"include" | "exclude">("include");
  const [preview, setPreview] = useState<string | undefined>();
  useEffect(() => {
    const ctl = new AbortController();
    void services
      .videoRule(site, point, ctl.signal)
      .then((r) => {
        if (!ctl.signal.aborted) {
          update(r.rule ?? blank);
          setRevision(r.revision);
        }
      })
      .catch(() => {
        if (!ctl.signal.aborted) setNotice(translate("rules.unavailable"));
      });
    void services
      .cameraControls(site, point, ctl.signal)
      .then((r) => {
        if (!ctl.signal.aborted)
          setPresets(r.actions.filter((a) => a.command === "preset_goto"));
      })
      .catch(() => {});
    const end = Date.now();
    void services
      .searchFrames(
        site,
        {
          points: [point],
          motion: false,
          interval: {
            start: new Date(end - 30000).toISOString(),
            end: new Date(end).toISOString(),
          },
        },
        ctl.signal,
      )
      .then((tracks) => {
        const pixels = tracks[0]?.hits.at(-1)?.preview;
        if (pixels && !ctl.signal.aborted)
          setPreview((shown) => shown ?? pixels);
      })
      .catch(() => {});
    return () => ctl.abort();
  }, [services, site, point]);
  function set<K extends keyof VideoRule>(key: K, v: VideoRule[K]) {
    update((old) => ({ ...old, [key]: v }));
    setTrial(null);
  }
  async function simulate() {
    setBusy(true);
    setNotice("");
    try {
      const ctl = AbortSignal.timeout(20000),
        interval = {
          start: new Date(from).toISOString(),
          end: new Date(to).toISOString(),
        };
      const [results, frames] = await Promise.all([
        services.simulateRule(site, point, { rule, interval }, ctl),
        services.searchFrames(
          site,
          { points: [point], interval, motion: false },
          ctl,
        ),
      ]);
      setTrial(results);
      setPreview(frames[0]?.hits[0]?.preview);
    } catch {
      setNotice(t("rules.trialUnavailable"));
    } finally {
      setBusy(false);
    }
  }
  async function save() {
    if (revision === null) return;
    setBusy(true);
    setNotice("");
    try {
      const saved = await services.saveRule(site, point, { revision, rule });
      setRevision(saved.revision);
      setNotice(t("rules.saved", { revision: saved.revision }));
    } catch {
      setNotice(t("rules.saveFailed"));
    } finally {
      setBusy(false);
    }
  }
  return (
    <fieldset className="inv-rule" disabled={revision === null || busy}>
      <div className="inv-transport">
        <label>
          <input
            type="checkbox"
            checked={rule.enabled}
            onChange={(e) => set("enabled", e.target.checked)}
          />
          {t("rules.enabled")}
        </label>
        <label>
          {t("rules.source")}
          <select
            value={rule.source}
            onChange={(e) => {
              const source = e.target.value as VideoRule["source"];
              update((v) => ({
                ...v,
                source,
                include: [],
                exclude: [],
                line: null,
              }));
              setTrial(null);
            }}
          >
            <option value="local">{t("rules.local")}</option>
            <option value="onvif">{t("rules.onvif")}</option>
          </select>
        </label>
      </div>
      {rule.source === "local" && (
        <>
          <div className="inv-transport">
            <label>
              {t("rules.threshold")} <output>{rule.threshold}</output>
              <input
                type="range"
                min={1}
                max={255}
                value={rule.threshold}
                onChange={(e) => set("threshold", +e.target.value)}
              />
            </label>
            <label>
              {t("rules.cells")}
              <input
                type="number"
                min={1}
                max={192}
                value={rule.min_cells}
                onChange={(e) => set("min_cells", +e.target.value)}
              />
            </label>
          </div>
          <div className="inv-transport">
            <label>
              {t("rules.zones")}
              <select
                value={geometry}
                onChange={(e) => setGeometry(e.target.value as typeof geometry)}
              >
                <option value="include">{t("rules.include")}</option>
                <option value="exclude">{t("rules.exclude")}</option>
              </select>
            </label>
            <IconButton
              icon="plus"
              label={t("privacy.add")}
              disabled={rule[geometry].length >= 8}
              onClick={() => set(geometry, [...rule[geometry], full])}
            />
          </div>
          {rule[geometry].map((r, i) => (
            <div key={geometry + i} className="inv-transport">
              <RegionField
                image={preview}
                region={r}
                change={(v) =>
                  set(
                    geometry,
                    rule[geometry].map((a, n) => (n === i ? v : a)),
                  )
                }
              />
              <IconButton
                icon="circle-x"
                label={t("rules.removeRegion")}
                onClick={() =>
                  set(
                    geometry,
                    rule[geometry].filter((_, n) => n !== i),
                  )
                }
              />
            </div>
          ))}
          <label>
            <input
              type="checkbox"
              checked={!!rule.line}
              onChange={(e) =>
                set("line", e.target.checked ? [0.5, 0, 0.5, 1] : null)
              }
            />
            {t("rules.crossing")}
          </label>
          {rule.line && (
            <CrossingLine
              line={rule.line}
              preview={preview}
              change={(value) => set("line", value)}
            />
          )}
        </>
      )}
      <div className="inv-transport">
        <label>
          {t("rules.cooldown")}
          <input
            type="number"
            min={1}
            max={3600}
            value={rule.cooldown}
            onChange={(e) => set("cooldown", +e.target.value)}
          />
        </label>
        <label>
          <input
            type="checkbox"
            checked={rule.record}
            onChange={(e) => set("record", e.target.checked)}
          />
          {t("rules.record")}
        </label>
        <label>
          <input
            type="checkbox"
            checked={rule.notify}
            onChange={(e) => set("notify", e.target.checked)}
          />
          {t("rules.alarm")}
        </label>
        <label>
          {t("rules.reaction")}
          <select
            value={rule.preset}
            onChange={(e) => set("preset", e.target.value)}
          >
            <option value="">{t("rules.noCommand")}</option>
            {presets.map((p) => (
              <option key={p.target} value={p.target}>
                {p.label}
              </option>
            ))}
          </select>
        </label>
      </div>
      <details>
        <summary>
          {t("rules.schedule")} ·{" "}
          {t(rule.schedule.length ? "rules.byPeriods" : "rules.always")}
        </summary>
        <label>
          {t("rules.timezone")}
          <input
            value={rule.zone}
            onChange={(e) => set("zone", e.target.value)}
          />
        </label>
        {rule.schedule.map((s, i) => (
          <div className="inv-transport" key={i}>
            <select
              aria-label={t("rules.weekday")}
              value={s.day}
              onChange={(e) =>
                set(
                  "schedule",
                  rule.schedule.map((p, n) =>
                    n === i ? { ...p, day: +e.target.value } : p,
                  ),
                )
              }
            >
              {shortWeekdays().map((d, n) => (
                <option key={n} value={n + 1}>
                  {d}
                </option>
              ))}
            </select>
            {(["start", "end"] as const).map((k) => (
              <label key={k}>
                {k === "start" ? t("rules.from") : t("rules.until")}
                <input
                  type="time"
                  value={formatMinute(s[k] % 1440)}
                  onChange={(e) =>
                    set(
                      "schedule",
                      rule.schedule.map((p, n) =>
                        n === i
                          ? {
                              ...p,
                              [k]:
                                minutes(e.target.value) ||
                                (k === "end" ? 1440 : 0),
                            }
                          : p,
                      ),
                    )
                  }
                />
              </label>
            ))}
            <IconButton
              icon="circle-x"
              label={t("rules.removePeriod")}
              onClick={() =>
                set(
                  "schedule",
                  rule.schedule.filter((_, n) => n !== i),
                )
              }
            />
          </div>
        ))}
        <IconButton
          icon="plus"
          label={t("rules.addPeriod")}
          onClick={() =>
            set("schedule", [
              ...rule.schedule,
              { day: 1, start: 540, end: 1080 },
            ])
          }
        />
      </details>
      <details>
        <summary>{t("rules.trial")}</summary>
        <div className="inv-transport">
          <label>
            {t("rules.from")}
            <input
              type="datetime-local"
              step="0.001"
              max={to}
              value={from}
              onChange={(e) => setFrom(e.target.value)}
            />
          </label>
          <label>
            {t("rules.to")}
            <input
              type="datetime-local"
              step="0.001"
              min={from}
              value={to}
              onChange={(e) => setTo(e.target.value)}
            />
          </label>
          <IconButton
            icon="play"
            label={t("rules.runTrial")}
            disabled={!(Date.parse(from) < Date.parse(to))}
            onClick={() => void simulate()}
          />
        </div>
        {trial && (
          <p role="status">
            {t("rules.hits", { count: trial.hits.length })}
            {!trial.complete && " · " + t("rules.partial")}
            {trial.hits.length > 0 &&
              " · " +
                trial.hits
                  .slice(0, 8)
                  .map((t) => new Date(t).toLocaleTimeString(locale()))
                  .join(", ")}
          </p>
        )}
      </details>
      <Button variant="primary" onClick={() => void save()}>
        {t("rules.save")}
      </Button>
      {notice && <p role="status">{notice}</p>}
    </fieldset>
  );
}
