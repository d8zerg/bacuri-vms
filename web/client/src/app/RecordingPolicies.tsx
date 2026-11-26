import { useEffect, useRef, useState } from "react";
import type {
  CapturePolicy,
  CapturePolicies,
  CaptureRule,
  CaptureStorage,
  Point,
  Services,
} from "../contracts/workspace";
import { Button, ControlGlyph, IconButton } from "../ui-kit/FormControls";
import {
  FilterPanel,
  FilterSegments,
  matchesQuery,
  needsListSearch,
} from "../ui-kit/FilterPanel";
import "./recording-policies.css";
import {
  formatNumber,
  locale,
  shortWeekdays,
  t as translate,
  useT,
  type MessageKey,
} from "../i18n";
type Period = { days: number[]; start: number; end: number };
function groupPeriods(schedule: CaptureRule["schedule"]): Period[] {
  const periods: Period[] = [];
  for (const { day, start, end } of schedule) {
    const period = periods.find((p) => p.start === start && p.end === end);
    if (period) {
      if (!period.days.includes(day)) period.days.push(day);
    } else periods.push({ days: [day], start, end });
  }
  return periods;
}
const defaults: CaptureRule = {
  continuous: true,
  manual: false,
  event: false,
  zone: "UTC",
  schedule: [],
  pre: 5,
  post: 10,
  audio: false,
  days: null,
};
const time = (minute: number) =>
  String(Math.floor(minute / 60) % 24).padStart(2, "0") +
  ":" +
  String(minute % 60).padStart(2, "0");
const minute = (value: string) => {
  const [h = 0, m = 0] = value.split(":").map(Number);
  return h * 60 + m;
};
function archiveDepth(oldest: number, newest: number) {
  const seconds = Math.max(0, (newest - oldest) / 1e6);
  const [value, unit] =
    seconds < 60
      ? ([seconds, "unit.seconds"] as const)
      : seconds < 3600
        ? ([seconds / 60, "unit.minutes"] as const)
        : seconds < 86400
          ? ([seconds / 3600, "unit.hours"] as const)
          : ([seconds / 86400, "unit.days"] as const);
  return translate(unit, {
    value: formatNumber(value, { maximumFractionDigits: 1 }),
  });
}
function modes(rule: CaptureRule) {
  return (
    [
      rule.continuous && translate("rp.continuous"),
      rule.manual && translate("cameras.manual"),
      rule.event && translate("rp.motion"),
      rule.schedule.length > 0 && translate("rp.scheduled"),
    ]
      .filter(Boolean)
      .join(" · ") || translate("rp.state.disabled")
  );
}
const recordingStates: Record<string, MessageKey> = {
  unbound: "rp.state.unbound",
  source_offline: "rp.state.source_offline",
  awaiting_policy: "rp.state.awaiting_policy",
  selected: "rp.state.selected",
  waiting_event: "rp.state.waiting_event",
  outside_schedule: "rp.state.outside_schedule",
  waiting_manual: "rp.state.waiting_manual",
  disabled: "rp.state.disabled",
  recording_stopped: "rp.state.recording_stopped",
  offline: "rp.state.offline",
};
type CaptureGroup = Awaited<ReturnType<Services["captureGroups"]>>[number];

function CameraSummary({
  point,
  policy,
  group,
  storage,
  edit,
  openGroup,
}: {
  point: Pick<Point, "id" | "name">;
  policy: CapturePolicy | undefined;
  group: CaptureGroup | undefined;
  storage: CaptureStorage;
  edit: () => void;
  openGroup: (id: string) => void;
}) {
  const t = useT();
  const rule = policy?.rule ?? defaults;
  const volume = storage.volume;
  const state = !volume
    ? "offline"
    : volume.state === "recording_stopped"
      ? volume.state
      : volume.point_states?.[point.id];
  const depth = storage.points.find((row) => row.point === point.id);
  const days =
    rule.days === null
      ? storage.days
      : Math.min(rule.days, storage.days ?? rule.days);
  return (
    <section
      className="rp-camera-summary"
      aria-label={t("rp.cameraSettings", { name: point.name })}
    >
      <div className="rp-origin">
        <strong tabIndex={-1} data-policy-focus>
          {point.name}
        </strong>
        {state && recordingStates[state] && (
          <span>{t(recordingStates[state])}</span>
        )}
      </div>
      <dl className="rp-facts">
        <div>
          <dt>{t("rp.settingsSource")}</dt>
          <dd>
            {group ? (
              <Button
                className="rp-group-link"
                onClick={() => openGroup(group.id)}
                aria-label={t("rp.openGroup", { name: group.name })}
              >
                <ControlGlyph icon="users-round" />
                {group.name}
                <ControlGlyph icon="chevron-right" />
              </Button>
            ) : policy?.scope === "point" ? (
              t("rp.individual")
            ) : (
              t("rp.site")
            )}
          </dd>
        </div>
        <div>
          <dt>{t("rp.mode")}</dt>
          <dd>{modes(rule)}</dd>
        </div>
        {rule.event && (
          <div>
            <dt>{t("rp.motion")}</dt>
            <dd>{t("rp.prePost", { pre: rule.pre, post: rule.post })}</dd>
          </div>
        )}
        {rule.schedule.length > 0 && (
          <div>
            <dt>
              {t("rules.schedule")} · {rule.zone}
            </dt>
            <dd>
              {groupPeriods(rule.schedule).map((p) => (
                <span className="rp-summary-period" key={p.start + "/" + p.end}>
                  {shortWeekdays()
                    .filter((_, i) => p.days.includes(i + 1))
                    .join(", ")}{" "}
                  · {time(p.start)} - {p.end === 1440 ? "24:00" : time(p.end)}
                  {p.end < p.start ? " " + t("rp.nextDay") : ""}
                </span>
              ))}
            </dd>
          </div>
        )}
        <div>
          <dt>{t("rp.audio")}</dt>
          <dd>
            {!policy
              ? t("rp.audioOnCommand")
              : rule.audio
                ? t("rp.on")
                : t("rp.off")}
          </dd>
        </div>
        <div>
          <dt>{t("rp.retention")}</dt>
          <dd>
            {days === null ? t("rp.noLimit") : t("unit.days", { value: days })}
          </dd>
        </div>
        <div>
          <dt>{t("rp.depth")}</dt>
          <dd>
            {depth
              ? archiveDepth(depth.oldest_us, depth.newest_us)
              : t("rp.noRecords")}
          </dd>
        </div>
        {depth && (
          <div>
            <dt>{t("rp.lastFrame")}</dt>
            <dd>{new Date(depth.newest_us / 1000).toLocaleString("ru")}</dd>
          </div>
        )}
      </dl>
      <Button className="rp-edit-camera" onClick={edit}>
        <ControlGlyph icon="settings-2" />
        {policy?.scope === "point" ? t("rp.editCamera") : t("rp.override")}
      </Button>
    </section>
  );
}

function GroupMembers({
  group,
  points,
  policies,
  openCamera,
}: {
  group: CaptureGroup;
  points: Point[];
  policies: CapturePolicies;
  openCamera: (id: string) => void;
}) {
  const t = useT();
  const [query, setQuery] = useState("");
  const policy = policies.policies.find(
    (p) => p.scope === "group" && p.target === group.id,
  );
  const applies = (id: string) =>
    !!policy && policies.effective[id]?.id === policy.id;
  const overrides = new Set(
    policies.policies.filter((p) => p.scope === "point").map((p) => p.target),
  );
  const ids = new Set(group.points);
  const members = points.filter((p) => ids.has(p.id));
  const filtered = members.filter((p) => matchesQuery(query, p.name));
  return (
    <details className="rp-members">
      <summary data-policy-focus>
        <strong>{t("rp.members")}</strong>
        <span>
          {policy
            ? t("rp.appliesTo", {
                count: group.points.filter(applies).length,
                total: group.points.length,
              })
            : t("rp.groupSize", { count: group.points.length })}
        </span>
      </summary>
      {needsListSearch(members.length, query) && (
        <FilterPanel
          label={t("rp.members")}
          queryLabel={t("rp.findInGroup")}
          query={query}
          onQuery={setQuery}
          onReset={() => setQuery("")}
        />
      )}
      <ul>
        {filtered.map((p) => (
          <li key={p.id}>
            <button onClick={() => openCamera(p.id)}>
              <ControlGlyph icon="cctv" />
              <span>
                <strong>{p.name}</strong>
                <small>
                  {applies(p.id)
                    ? t("rp.byGroup")
                    : !policies.effective[p.id]
                      ? t("rp.site")
                      : overrides.has(p.id)
                        ? t("rp.individual")
                        : t("rp.byOtherGroup")}
                </small>
              </span>
              <ControlGlyph icon="chevron-right" />
            </button>
          </li>
        ))}
      </ul>
      {!filtered.length && (
        <p>
          {query
            ? t("inv.noHits")
            : group.points.length
              ? t("rp.camerasUnavailable")
              : t("rp.emptyGroup")}
        </p>
      )}
    </details>
  );
}
function Editor({
  policy,
  inherited,
  scope,
  target,
  site,
  services,
  done,
  name,
  cancel,
}: {
  policy: CapturePolicy | undefined;
  inherited: CaptureRule | undefined;
  scope: "point" | "group";
  target: string;
  site: string;
  services: Services;
  done: () => void;
  name: string;
  cancel?: () => void;
}) {
  const t = useT();
  const [rule, setRule] = useState<CaptureRule>(
      policy?.rule ?? inherited ?? defaults,
    ),
    [periods, setPeriods] = useState(() => groupPeriods(rule.schedule)),
    [priority, setPriority] = useState(policy?.priority ?? 50),
    [busy, setBusy] = useState(false),
    [error, setError] = useState(""),
    [confirm, setConfirm] = useState(false);
  function update<K extends keyof CaptureRule>(k: K, v: CaptureRule[K]) {
    setRule((r) => ({ ...r, [k]: v }));
  }
  async function save(remove = false) {
    setBusy(true);
    setError("");
    try {
      if (remove && policy)
        await services.deleteCapture(site, policy.id, policy.revision);
      else
        await services.saveCapture(site, policy?.id ?? crypto.randomUUID(), {
          scope,
          target,
          priority,
          rule: {
            ...rule,
            schedule: periods.flatMap(({ days, start, end }) =>
              days.map((day) => ({ day, start, end })),
            ),
          },
          revision: policy?.revision ?? "0",
        });
      done();
    } catch {
      setError(t("rp.saveFailed"));
    } finally {
      setBusy(false);
    }
  }
  return (
    <form
      className="rp-editor"
      onSubmit={(e) => {
        e.preventDefault();
        void save();
      }}
    >
      <strong tabIndex={-1} data-policy-focus>
        {t(scope === "group" ? "rp.groupPolicyNamed" : "rp.individualNamed", {
          name,
        })}
      </strong>
      {scope === "group" && (
        <label className="rp-priority">
          {t("rp.priority")}
          <input
            required
            type="number"
            min={0}
            max={100}
            aria-description={t("rp.priorityHint")}
            value={priority}
            onChange={(e) => setPriority(+e.target.value)}
          />
        </label>
      )}
      <fieldset disabled={busy}>
        <legend>{t("rp.record")}</legend>
        <div className="rp-modes">
          {(
            [
              ["continuous", t("rp.continuous")],
              ["manual", t("cameras.manual")],
              ["event", t("rp.motion")],
            ] as const
          ).map(([k, label]) => (
            <label key={k}>
              <input
                type="checkbox"
                checked={rule[k]}
                onChange={(e) => update(k, e.target.checked)}
              />
              {label}
            </label>
          ))}
        </div>
        {rule.event && (
          <div className="rp-fields">
            {(
              [
                ["pre", t("rp.pre"), 0, 30],
                ["post", t("rp.post"), 1, 300],
              ] as const
            ).map(([k, label, min, max]) => (
              <label key={k}>
                {label}
                <input
                  type="number"
                  required
                  min={min}
                  max={max}
                  value={rule[k]}
                  onChange={(e) => update(k, Number(e.target.value))}
                />
              </label>
            ))}
          </div>
        )}
      </fieldset>
      <fieldset disabled={busy}>
        <legend>{t("rp.weekly")}</legend>
        {periods.length > 0 && (
          <label>
            {t("rules.timezone")}
            <input
              required
              value={rule.zone}
              list="rp-zones"
              onChange={(e) => update("zone", e.target.value)}
            />
            <datalist id="rp-zones">
              {[
                "UTC",
                Intl.DateTimeFormat().resolvedOptions().timeZone,
                "Europe/Moscow",
                "Europe/Berlin",
                "Asia/Yekaterinburg",
              ]
                .filter((v, i, a) => a.indexOf(v) === i)
                .map((z) => (
                  <option key={z}>{z}</option>
                ))}
            </datalist>
          </label>
        )}
        {periods.map((period, index) => {
          const patch = (change: Partial<Period>) =>
            setPeriods((rows) =>
              rows.map((p, n) => (n === index ? { ...p, ...change } : p)),
            );
          return (
            <div className="rp-period" key={index}>
              <div className="rp-week" role="group" aria-label={t("rp.days")}>
                {shortWeekdays().map((label, dayIndex) => {
                  const day = dayIndex + 1;
                  const active = period.days.includes(day);
                  return (
                    <button
                      key={day}
                      type="button"
                      aria-pressed={active}
                      disabled={
                        active
                          ? period.days.length === 1
                          : periods.reduce((n, p) => n + p.days.length, 0) >= 56
                      }
                      onClick={() =>
                        patch({
                          days: active
                            ? period.days.filter((d) => d !== day)
                            : [...period.days, day],
                        })
                      }
                    >
                      {label}
                    </button>
                  );
                })}
              </div>
              <div className="rp-times">
                <input
                  aria-label={t("rp.intervalStart")}
                  type="time"
                  required
                  value={time(period.start)}
                  onChange={(e) => {
                    if (e.target.value)
                      patch({ start: minute(e.target.value) });
                  }}
                />
                <span aria-hidden="true">-</span>
                <input
                  aria-label={t("access.intervalEnd")}
                  type="time"
                  required
                  value={time(period.end)}
                  onChange={(e) => {
                    if (e.target.value)
                      patch({ end: minute(e.target.value) || 1440 });
                  }}
                />
                <IconButton
                  type="button"
                  icon="minus"
                  label={t("rp.removeInterval")}
                  onClick={() =>
                    setPeriods((rows) => rows.filter((_, n) => n !== index))
                  }
                />
              </div>
            </div>
          );
        })}
        <IconButton
          type="button"
          icon="plus"
          label={t("rp.addInterval")}
          disabled={periods.reduce((n, p) => n + p.days.length, 0) >= 56}
          onClick={() =>
            setPeriods((rows) => [
              ...rows,
              { days: [1], start: 540, end: 1080 },
            ])
          }
        />
      </fieldset>
      <fieldset disabled={busy}>
        <legend>{t("monitor.archive")}</legend>
        <label className="rp-audio">
          <input
            type="checkbox"
            checked={rule.audio}
            onChange={(e) => update("audio", e.target.checked)}
          />
          {t("rp.recordAudio")}
        </label>
        <div className="rp-fields">
          <label>
            {t("rp.retentionDays")}
            <input
              type="number"
              min={1}
              max={36500}
              placeholder={t("rp.byVolume")}
              value={rule.days ?? ""}
              onChange={(e) =>
                update("days", e.target.value ? +e.target.value : null)
              }
            />
          </label>
        </div>
      </fieldset>
      <footer>
        {cancel && (
          <Button type="button" disabled={busy} onClick={cancel}>
            {t("action.cancel")}
          </Button>
        )}
        {policy && (
          <Button
            type="button"
            disabled={busy}
            onClick={() => (confirm ? void save(true) : setConfirm(true))}
          >
            {confirm
              ? t("rp.confirmReset")
              : scope === "point"
                ? t("rp.inherit")
                : t("rp.removeGroupPolicy")}
          </Button>
        )}
        <Button type="submit" variant="primary" disabled={busy}>
          {scope === "group" ? t("rp.saveGroup") : t("rp.saveCamera")}
        </Button>
      </footer>
      {error && <p role="alert">{error}</p>}
    </form>
  );
}
function Volume({
  value,
  save,
}: {
  value: CaptureStorage;
  save: (days: number, reserve: number) => Promise<void>;
}) {
  const t = useT();
  const [days, term] = useState(value.days?.toString() ?? ""),
    [reserve, floor] = useState(value.reserve_percent),
    [error, fail] = useState(false),
    [busy, working] = useState(false);
  async function submit() {
    working(true);
    try {
      await save(Number(days), reserve);
      fail(false);
    } catch {
      fail(true);
    } finally {
      working(false);
    }
  }
  const v = value.volume;
  return (
    <form
      className="rp-editor"
      onSubmit={(e) => {
        e.preventDefault();
        void submit();
      }}
    >
      <div className="rp-origin">
        <strong>
          {!v
            ? t("rp.state.offline")
            : v.state === "recording_stopped"
              ? t("rp.stopped")
              : v.state === "low_space"
                ? t("rp.lowSpace")
                : t("rp.volumeOk")}
        </strong>
        {v && (
          <span>
            {t("rp.free", {
              free: formatNumber(v.available_bytes / 2 ** 30, {
                minimumFractionDigits: 1,
                maximumFractionDigits: 1,
              }),
              total: formatNumber(v.capacity_bytes / 2 ** 30, {
                minimumFractionDigits: 1,
                maximumFractionDigits: 1,
              }),
            })}
          </span>
        )}
      </div>
      {v && (
        <meter
          aria-label={t("rp.used")}
          min={0}
          max={v.capacity_bytes}
          value={v.capacity_bytes - v.available_bytes}
        />
      )}
      {v?.replica && (
        <p>
          {v.replica.online === false
            ? t("rp.replicaOffline")
            : t("rp.replica")}{" "}
          · {t("rp.verified", { count: v.replica.verified })}
          {!!v.replica.pending &&
            " · " + t("rp.pending", { count: v.replica.pending })}
          {!!v.replica.failed &&
            " · " + t("rp.failed", { count: v.replica.failed })}
        </p>
      )}
      {v?.write_bytes_per_second && (
        <p>
          {t("rp.forecast")} ·{" "}
          {Math.min(
            Number(days) || Infinity,
            Math.max(0, v.capacity_bytes * (1 - reserve / 100)) /
              v.write_bytes_per_second /
              86400,
          ).toLocaleString(locale(), { maximumFractionDigits: 1 })}{" "}
          {t("rp.daysShort")}
        </p>
      )}
      <div className="rp-fields">
        <label>
          {t("rp.siteRetention")}
          <input
            type="number"
            required
            min={1}
            max={36500}
            value={days}
            placeholder={t("rp.unlimited")}
            onChange={(e) => term(e.target.value)}
          />
        </label>
        <label>
          {t("rp.reserve")}
          <input
            type="number"
            required
            min={1}
            max={90}
            value={reserve}
            onChange={(e) => floor(+e.target.value)}
          />
        </label>
      </div>
      <footer>
        <Button type="submit" variant="primary" disabled={busy}>
          {t("action.save")}
        </Button>
      </footer>
      {error && <p role="alert">{t("rp.volumeSaveFailed")}</p>}
    </form>
  );
}
export default function RecordingPolicies({
  site,
  points,
  services,
}: {
  site: string;
  points: Point[];
  services: Services;
}) {
  const t = useT();
  const [view, setView] = useState("point"),
    [query, search] = useState(""),
    [chosen, choose] = useState(""),
    [editing, edit] = useState(""),
    [version, refresh] = useState(0);
  const [result, load] = useState<{
      policies: CapturePolicies;
      storage: CaptureStorage;
      groups: CaptureGroup[];
    } | null>(null),
    [error, fail] = useState(false),
    [busy, working] = useState(true);
  useEffect(() => {
    const ctl = new AbortController();
    working(true);
    Promise.all([
      services.capturePolicies(site, ctl.signal),
      services.captureStorage(site, ctl.signal),
      services.captureGroups(site, ctl.signal),
    ])
      .then(([policies, storage, groups]) => {
        if (!ctl.signal.aborted) {
          load({ policies, storage, groups });
          fail(false);
        }
      })
      .catch(() => {
        if (!ctl.signal.aborted) fail(true);
      })
      .finally(() => {
        if (!ctl.signal.aborted) working(false);
      });
    return () => ctl.abort();
  }, [site, services, version]);
  const items = (view === "point" ? points : (result?.groups ?? [])).filter(
    (p) => matchesQuery(query, p.name),
  );
  const item = items.find((p) => p.id === chosen) ?? items[0];
  const policies = result?.policies.policies ?? [],
    direct = policies.find((p) => p.target === item?.id && p.scope === view);
  const applied =
    item && view === "point" ? result?.policies.effective[item.id] : undefined;
  const group = policies.find(
    (p) => p.id === applied?.id && p.scope === "group",
  );
  const selectedGroup = result?.groups.find(
    (g) => g.id === item?.id && view === "group",
  );
  const detail = useRef<HTMLDivElement>(null);
  const focusDetail = useRef(false);
  useEffect(() => {
    if (focusDetail.current && detail.current) {
      const heading = detail.current.querySelector<HTMLElement>(
        "[data-policy-focus]",
      );
      if (heading) {
        heading.focus({ preventScroll: true });
        const bounds = heading.getBoundingClientRect();
        if (bounds.top < 0 || bounds.bottom > window.innerHeight)
          heading.scrollIntoView({ block: "nearest" });
      }
      focusDetail.current = false;
    }
  }, [view, item?.id, editing]);

  function editCamera(id: string) {
    focusDetail.current = true;
    edit(id);
  }

  function navigate(scope: string, id = "") {
    if (!scope) return;
    focusDetail.current = !!id;
    setView(scope);
    choose(id);
    search("");
    edit("");
  }
  return (
    <section
      className="ws-panel ws-pad rp-workspace"
      aria-label={t("rp.title")}
    >
      <FilterSegments
        label={t("rp.scope")}
        value={view}
        options={[
          { value: "point", label: t("rp.cameras") },
          { value: "group", label: t("access.tab.groups") },
          { value: "volume", label: t("rp.volume") },
        ]}
        onChange={navigate}
      />
      {view !== "volume" && (
        <FilterPanel
          label={t("rp.filter")}
          queryLabel={
            view === "point" ? t("layout.findCamera") : t("rp.findGroup")
          }
          query={query}
          onQuery={(text) => {
            search(text);
            edit("");
          }}
          onReset={() => search("")}
          refresh={{
            label: t("rp.refresh"),
            run: () => refresh((v) => v + 1),
            busy,
          }}
        />
      )}
      {view === "volume" && (
        <IconButton
          icon="rotate-cw"
          label={t("rp.refreshVolume")}
          onClick={() => refresh((v) => v + 1)}
        />
      )}
      {error && <p role="alert">{t("rp.loadFailed")}</p>}
      {!result && busy && <p role="status">{t("action.loading")}</p>}
      {result &&
        (view === "volume" ? (
          <Volume
            key={version}
            value={result.storage}
            save={async (d, r) => {
              const storage = await services.setCaptureStorage(
                site,
                d,
                r,
                result.storage.revision,
              );
              load({ ...result, storage });
            }}
          />
        ) : (
          <div className="rp-split">
            <div
              className="rp-targets"
              aria-label={
                view === "point" ? t("rp.cameras") : t("access.tab.groups")
              }
            >
              {items.map((p) => {
                const rule =
                  view === "point"
                    ? result.policies.effective[p.id]?.rule
                    : policies.find(
                        (x) => x.scope === "group" && x.target === p.id,
                      )?.rule;
                return (
                  <button
                    type="button"
                    key={p.id}
                    aria-pressed={p.id === item?.id}
                    onClick={() => {
                      if (p.id !== item?.id) {
                        choose(p.id);
                        edit("");
                      }
                    }}
                  >
                    <strong>{p.name}</strong>
                    <small>
                      {rule
                        ? modes(rule)
                        : view === "point"
                          ? t("rp.continuous")
                          : t("rp.noPolicy")}
                    </small>
                  </button>
                );
              })}
              {!items.length && (
                <p>
                  {query
                    ? t("inv.noHits")
                    : view === "group"
                      ? t("rp.noGroups")
                      : t("rp.noCameras")}
                </p>
              )}
            </div>
            {item && (
              <div
                className="rp-detail"
                key={view + "/" + item.id}
                ref={detail}
                role="group"
                aria-label={item.name}
              >
                {selectedGroup && (
                  <GroupMembers
                    group={selectedGroup}
                    points={points}
                    policies={result.policies}
                    openCamera={(id) => navigate("point", id)}
                  />
                )}
                {view === "point" && editing !== item.id ? (
                  <CameraSummary
                    point={item}
                    policy={direct ?? group}
                    group={
                      group && result.groups.find((g) => g.id === group.target)
                    }
                    storage={result.storage}
                    edit={() => editCamera(item.id)}
                    openGroup={(id) => navigate("group", id)}
                  />
                ) : (
                  <Editor
                    key={[
                      direct?.id,
                      direct?.revision,
                      applied?.id,
                      applied?.revision,
                    ].join("/")}
                    scope={view === "point" ? "point" : "group"}
                    target={item.id}
                    name={item.name}
                    site={site}
                    services={services}
                    policy={direct}
                    inherited={applied?.rule}
                    done={() => {
                      if (view === "point") editCamera("");
                      refresh((v) => v + 1);
                    }}
                    {...(view === "point"
                      ? { cancel: () => editCamera("") }
                      : {})}
                  />
                )}
              </div>
            )}
          </div>
        ))}
    </section>
  );
}
