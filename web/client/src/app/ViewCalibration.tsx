import { useState } from "react";
import type { FormEvent } from "react";
import type { MapScene, Placement, Services } from "../contracts/workspace";
import { Button } from "../ui-kit/FormControls";
import { useT } from "../i18n";

export function landmarkBearing(
  m: Placement,
  x: number,
  y: number,
  plan?: MapScene["plans"][number],
) {
  if (x === m.x && y === m.y) return null;
  const rad = Math.PI / 180;
  const delta = (x - m.x) * rad;
  const east = plan
    ? (x - m.x) * plan.width
    : Math.sin(delta) * Math.cos(y * rad);
  const north = plan
    ? (m.y - y) * plan.height
    : Math.cos(m.y * rad) * Math.sin(y * rad) -
      Math.sin(m.y * rad) * Math.cos(y * rad) * Math.cos(delta);
  return Math.round(((Math.atan2(east, north) / rad + 360) % 360) * 10) / 10;
}
export type Landmark = { bearing: number; camera?: Placement["camera"] };
type Props = {
  marker: Placement;
  site: string;
  services: Services;
  landmark: Landmark | null;
  aim: boolean;
  setAim: (v: boolean) => void;
  live: string;
  onSaved: () => void;
};
export function ViewCalibration({
  marker,
  site,
  services,
  landmark,
  aim,
  setAim,
  live,
  onSaved,
}: Props) {
  const t = useT();
  const cam = marker.camera!,
    cfg = marker.calibration;
  const [busy, setBusy] = useState(false),
    [notice, say] = useState("");
  const generic =
    cam.ptz &&
    cam.space !==
      "http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionSpaceDegrees";
  const reference = landmark ? landmark.camera : cam;
  const changed =
    cam.ptz &&
    !!landmark &&
    (reference?.device_id !== cam.device_id ||
      reference.profile !== cam.profile ||
      reference.space !== cam.space ||
      reference.pan === undefined ||
      cam.pan === undefined ||
      Math.abs(reference.pan - cam.pan) > 0.00001);
  const ready =
    !cam.ptz || (!changed && cam.fresh && !cam.moving && cam.pan !== undefined);
  async function save(e: FormEvent<HTMLFormElement>) {
    e.preventDefault();
    if (!ready || busy) return;
    const fields = new FormData(e.currentTarget);
    setBusy(true);
    say("");
    try {
      await services.calibrate(site, {
        point_id: marker.point_id,
        plan_id: marker.plan_id,
        device_id: cam.device_id,
        bearing: Number(fields.get("bearing")),
        hfov: Number(fields.get("hfov")),
        ...(cam.ptz && reference?.pan !== undefined && reference.space
          ? { reference_pan: reference.pan, space: reference.space }
          : {}),
        ...(generic ? { degrees_per_unit: Number(fields.get("scale")) } : {}),
        ...(fields.get("tele")
          ? { tele_hfov: Number(fields.get("tele")) }
          : {}),
      });
      say(t("view.saved"));
      onSaved();
    } catch {
      say(t("view.saveFailed"));
    } finally {
      setBusy(false);
    }
  }
  return (
    <details
      onToggle={(e) => {
        if (!e.currentTarget.open) setAim(false);
      }}
    >
      <summary>{t("view.configure")}</summary>
      <form onSubmit={(e) => void save(e)}>
        <Button
          onClick={() => window.open(live, "_blank", "noopener,noreferrer")}
        >
          {t("view.openLive")}
        </Button>
        <Button type="button" aria-pressed={aim} onClick={() => setAim(!aim)}>
          {aim ? t("view.cancelAim") : t("view.aim")}
        </Button>
        {aim && <p role="status">{t("view.aimHint")}</p>}
        <label>
          {marker.plan_id ? t("view.planBearing") : t("view.azimuth")}
          <input
            key={landmark?.bearing ?? "initial"}
            name="bearing"
            type="number"
            min="0"
            max="360"
            step="any"
            required
            defaultValue={landmark?.bearing ?? cfg?.bearing}
          />
        </label>
        <label>
          {cam.ptz ? t("view.wideFov") : t("view.fov")}
          <input
            name="hfov"
            type="number"
            min="0.1"
            max="179.9"
            step="any"
            required
            defaultValue={cfg?.hfov ?? (cam.zoom === 0 ? cam.hfov : undefined)}
          />
        </label>
        {cam.ptz && (
          <>
            <label>
              {t("view.teleFov")}
              <input
                type="number"
                name="tele"
                min="0.1"
                max="179.9"
                step="any"
                defaultValue={cfg?.tele_hfov}
              />
            </label>
            {cam.pan !== undefined && (
              <p>
                {t("view.pan", { value: Number(cam.pan.toFixed(5)) })}
                {cam.hfov !== undefined &&
                  " · " + t("view.currentFov", { fov: cam.hfov })}
              </p>
            )}
            {generic && cam.space && (
              <label>
                {t("view.panScale")}
                <input
                  type="number"
                  name="scale"
                  step="any"
                  required
                  defaultValue={cfg?.degrees_per_unit}
                />
              </label>
            )}
            {!ready && (
              <p role="status">
                {changed ? t("view.moved") : t("view.needPosition")}
              </p>
            )}
          </>
        )}
        <Button type="submit" disabled={!ready || busy || aim}>
          {t("view.save")}
        </Button>
        {notice && <p role="status">{notice}</p>}
      </form>
    </details>
  );
}
