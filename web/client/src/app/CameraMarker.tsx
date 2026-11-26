import "../i18n/extended";
import type { CSSProperties } from "react";
import type { Placement } from "../contracts/workspace";
import icons from "../../../../design/bacuri/icons/sprite.svg";
import { dateTimeFormat, t as translate, useT } from "../i18n";

export function viewLabel(view: Placement["view"]) {
  if (!view || view.state === "unknown") return translate("view.unknown");
  const angles = translate(
    view.estimated ? "view.anglesEstimated" : "view.angles",
    {
      bearing: Math.round(view.bearing ?? 0),
      fov: Math.round(view.hfov ?? 0),
    },
  );
  if (view.state === "fixed") return translate("view.fixed", { angles });
  const time = view.observed_at
    ? dateTimeFormat({ timeStyle: "medium" }).format(new Date(view.observed_at))
    : "";
  return (
    translate(view.state === "stale" ? "view.stale" : "view.ptz", {
      angles,
    }) +
    " · " +
    time
  );
}
type Props = {
  marker: Placement;
  name: string;
  selected: boolean;
  newAlerts?: number;
  choose: () => void;
  style?: CSSProperties;
};
export function CameraMarker({
  marker,
  name,
  selected,
  choose,
  style,
  newAlerts = 0,
}: Props) {
  const t = useT();
  const view = marker.view;
  const half = ((view?.hfov ?? 0) * Math.PI) / 360;
  const x = 52 * Math.sin(half),
    y = 52 * Math.cos(half);
  return (
    <button
      type="button"
      className="ws-pin"
      style={style}
      aria-label={
        newAlerts
          ? name + " · " + t("alerts.newCount", { count: newAlerts })
          : name
      }
      title={[
        name,
        viewLabel(view),
        ...(newAlerts ? [t("alerts.newCount", { count: newAlerts })] : []),
      ].join(" · ")}
      aria-pressed={selected}
      onClick={(e) => {
        e.stopPropagation();
        choose();
      }}
    >
      {view?.bearing !== undefined && view.hfov !== undefined && (
        <svg
          className="ws-sector"
          data-state={view.state}
          viewBox="-55 -55 110 110"
          aria-hidden="true"
          style={{
            transform:
              "scaleY(var(--map-tilt, 1)) rotate(calc(" +
              view.bearing +
              "deg - var(--map-bearing, 0deg)))",
          }}
        >
          <path d={`M 0 0 L ${-x} ${-y} A 52 52 0 0 1 ${x} ${-y} Z`} />
        </svg>
      )}
      <svg className="ws-camera-icon" aria-hidden="true" viewBox="0 0 24 24">
        <use href={icons + "#i-cctv"} />
      </svg>
      {newAlerts > 0 && (
        <span className="ws-pin-alert" aria-hidden="true">
          {newAlerts}
        </span>
      )}
      <span className="ws-pin-name">{name}</span>
    </button>
  );
}
