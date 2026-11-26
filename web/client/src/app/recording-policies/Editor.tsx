import "../../i18n/extended";
// The editor of a recording rule of a camera or a group.
import { useState } from "react";
import type {
  CapturePolicy,
  CaptureRule,
  Services,
} from "../../contracts/workspace";
import { Button, IconButton } from "../../ui-kit/FormControls";

import { useSubmission } from "../../workflows/submission";
import { shortWeekdays, useT } from "../../i18n";
import { defaults, groupPeriods, minute, time, type Period } from "./rules";

export function Editor({
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
    [priority, setPriority] = useState(policy?.priority ?? 50);
  const submit = useSubmission(),
    { busy, error } = submit,
    confirm = submit.confirming;
  function update<K extends keyof CaptureRule>(k: K, v: CaptureRule[K]) {
    setRule((r) => ({ ...r, [k]: v }));
  }
  async function save(remove = false) {
    await submit.run(async () => {
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
    }, t("rp.saveFailed"));
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
            onClick={() => (confirm ? void save(true) : submit.confirm())}
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
