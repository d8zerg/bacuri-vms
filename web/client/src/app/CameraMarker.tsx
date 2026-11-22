import type { CSSProperties } from "react";
import type { Placement } from "../contracts/workspace";
import icons from "../../../../design/bacuri/icons/sprite.svg";

export function viewLabel(view: Placement["view"]) {
  if (!view || view.state === "unknown")
    return "Текущее направление неизвестно";
  const angles =
    Math.round(view.bearing ?? 0) +
    "° · угол обзора " +
    (view.estimated ? "около " : "") +
    Math.round(view.hfov ?? 0) +
    "°";
  if (view.state === "fixed") return "Заданное направление " + angles;
  const time = view.observed_at
    ? new Date(view.observed_at).toLocaleTimeString("ru-RU")
    : "";
  return (
    (view.state === "stale"
      ? "Последнее известное направление "
      : "Направление PTZ ") +
    angles +
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
  const view = marker.view;
  const half = ((view?.hfov ?? 0) * Math.PI) / 360;
  const x = 52 * Math.sin(half),
    y = 52 * Math.cos(half);
  return (
    <button
      type="button"
      className="ws-pin"
      style={style}
      aria-label={newAlerts ? name + " · Новых тревог: " + newAlerts : name}
      title={[
        name,
        viewLabel(view),
        ...(newAlerts ? ["Новых тревог: " + newAlerts] : []),
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
