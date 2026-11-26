// Recording rules: defaults, schedule periods, archive depth and state names.
import type { CaptureRule, Services } from "../../contracts/workspace";
import { formatNumber, t as translate, type MessageKey } from "../../i18n";

export type Period = { days: number[]; start: number; end: number };
export function groupPeriods(schedule: CaptureRule["schedule"]): Period[] {
  const periods: Period[] = [];
  for (const { day, start, end } of schedule) {
    const period = periods.find((p) => p.start === start && p.end === end);
    if (period) {
      if (!period.days.includes(day)) period.days.push(day);
    } else periods.push({ days: [day], start, end });
  }
  return periods;
}
export const defaults: CaptureRule = {
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
export const time = (minute: number) =>
  String(Math.floor(minute / 60) % 24).padStart(2, "0") +
  ":" +
  String(minute % 60).padStart(2, "0");
export const minute = (value: string) => {
  const [h = 0, m = 0] = value.split(":").map(Number);
  return h * 60 + m;
};
export function archiveDepth(oldest: number, newest: number) {
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
export function modes(rule: CaptureRule) {
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
export const recordingStates: Record<string, MessageKey> = {
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
export type CaptureGroup = Awaited<
  ReturnType<Services["captureGroups"]>
>[number];
