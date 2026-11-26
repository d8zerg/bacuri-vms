import "../../i18n/extended";
// The recording state, archive depth and applied rule of one camera.

import type {
  CapturePolicy,
  CaptureStorage,
  Point,
} from "../../contracts/workspace";
import { Button, ControlGlyph } from "../../ui-kit/FormControls";

import { locale, shortWeekdays, useT } from "../../i18n";
import {
  archiveDepth,
  defaults,
  groupPeriods,
  modes,
  recordingStates,
  time,
  type CaptureGroup,
} from "./rules";

export function CameraSummary({
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
            <dd>{new Date(depth.newest_us / 1000).toLocaleString(locale())}</dd>
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
