// The selected point: its camera, events, position on the map and its editing.

import { RouteButton } from "../RouteButton";
import type { MapScene } from "../../contracts/workspace";
import { Button } from "../../ui-kit/FormControls";
import { CameraEvents } from "../CameraEvents";

import { viewLabel } from "../CameraMarker";
import { ViewCalibration } from "../ViewCalibration";

import { MapPackages } from "../MapPackages";

const fixed = (v: number) => String(Number(v.toFixed(6)));
import type { MapsState } from "./state";

// The parent shows the section once the scene is read.
export function PointPanel({ m, scene }: { m: MapsState; scene: MapScene }) {
  const {
    t,
    params,
    reading,
    notice,
    busy,
    draft,
    setDraft,
    aim,
    setAim,
    landmark,
    setLandmark,
    selected,
    surface,
    positionEditor,
    cameraTitle,
    update,
    region,
    plan,
    floors,
    floor,
    visibleMarkers,
    marker,
    point,
    changeSurface,
    target,
    save,
    upload,
    coordinates,
    otherPoints,
    pointList,
    services,
    site,
    admin,
    feed,
  } = m;
  return (
    <section className="ws-panel ws-pad">
      {(!point || (!admin && !marker)) && <h2>{t("maps.points")}</h2>}
      {otherPoints.length > 0 &&
        (point && (admin || marker) ? (
          <details key={selected}>
            <summary>{t("maps.changePoint")}</summary>
            {pointList}
          </details>
        ) : (
          pointList
        ))}

      {!visibleMarkers.length && <p>{t("maps.noMarkers")}</p>}
      {point && (admin || marker) && (
        <>
          <h2 tabIndex={-1} ref={cameraTitle} id={"map-point-" + point.id}>
            {point.name}
          </h2>
          <div className="ws-map-actions">
            <RouteButton to={target("live")}>{t("monitor.live")}</RouteButton>
            <RouteButton to={target("archive")}>
              {t("monitor.archive")}
            </RouteButton>
          </div>
          <CameraEvents
            key={point.id}
            services={services}
            site={site}
            point={point}
            feed={feed}
            review={params.get("review")}
            onReview={(id) => update({ review: id }, true)}
            monitor={(id) => target("archive", id)}
          />
          {marker?.source_plan_id && (
            <Button onClick={() => changeSurface(marker.source_plan_id!)}>
              {t("maps.openPlan")} ·{" "}
              {floors.find((p) => p.id === marker.source_plan_id)?.name}
            </Button>
          )}
          {admin && marker?.camera && (
            <ViewCalibration
              key={selected + "/" + surface + "/" + marker.camera.device_id}
              marker={marker}
              site={site}
              services={services}
              landmark={landmark}
              aim={aim}
              setAim={setAim}
              live={target("live")}
              onSaved={() => {
                setAim(false);
                setLandmark(null);
                reading.reload();
              }}
            />
          )}
          {admin &&
          (!floor || marker) &&
          !marker?.source_plan_id &&
          (plan || (!surface && region)) ? (
            <details
              key={
                site + "/" + surface + "/" + selected + "/" + (region?.id ?? "")
              }
              ref={positionEditor}
              onToggle={(e) => {
                if (!e.currentTarget.open) setDraft(null);
              }}
            >
              <summary>{t("maps.editPosition")}</summary>
              {marker?.view && <p>{viewLabel(marker.view)}</p>}
              <form
                className="ws-placement-form"
                onSubmit={(e) => void save(e)}
              >
                {(["x", "y"] as const).map((axis) => (
                  <label key={axis}>
                    {plan
                      ? axis.toUpperCase()
                      : axis === "x"
                        ? t("plan.axis.lng")
                        : t("plan.axis.lat")}
                    <input
                      required
                      type="number"
                      step="any"
                      min={plan ? 0 : axis === "x" ? -180 : -85.051129}
                      max={plan ? 1 : axis === "x" ? 180 : 85.051129}
                      value={coordinates[axis]}
                      onChange={(e) =>
                        setDraft({
                          ...coordinates,
                          [axis]: e.target.value,
                        })
                      }
                    />
                  </label>
                ))}
                <Button type="submit" disabled={!draft || busy}>
                  {t("maps.savePosition")}
                </Button>
              </form>
            </details>
          ) : (
            marker && (
              <details>
                <summary>{t("maps.pointSettings")}</summary>
                <p>
                  {plan ? "X, Y" : t("maps.lngLat")} · {fixed(marker.x)},{" "}
                  {fixed(marker.y)}
                </p>
                {marker.view && <p>{viewLabel(marker.view)}</p>}
              </details>
            )
          )}
        </>
      )}
      {admin && (
        <details>
          <summary>{t("maps.addPlan")}</summary>
          <form onSubmit={(e) => void upload(e)}>
            <label>
              {t("maps.planName")}
              <input name="name" required maxLength={128} />
            </label>
            <label>
              {t("maps.planImage")}
              <input name="image" required type="file" accept="image/png" />
            </label>
            <Button type="submit" disabled={busy}>
              {t("maps.uploadPlan")}
            </Button>
          </form>
        </details>
      )}
      {admin && (
        <MapPackages
          site={site}
          services={services}
          regions={scene.regions}
          placements={scene.placements}
        />
      )}
      <p role="status">{notice}</p>
    </section>
  );
}
