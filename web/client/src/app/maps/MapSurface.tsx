// The map region or floor plan with the camera markers and the tools above it.

import { lazy, Suspense } from "react";

import type { MapScene } from "../../contracts/workspace";
import { Button } from "../../ui-kit/FormControls";

import { PlanZoom } from "../PlanZoom";
import { CameraMarker } from "../CameraMarker";

import { coveringRegion } from "../mapRegions";

import { dateTimeFormat } from "../../i18n";
const RegionMap = lazy(() => import("../RegionMap"));
const fixed = (v: number) => String(Number(v.toFixed(6)));
import type { MapsState } from "./state";

// The parent shows the section once the scene is read.
export function MapSurface({ m, scene }: { m: MapsState; scene: MapScene }) {
  const {
    t,
    setEditingPlan,
    opacity,
    setOpacity,
    selected,
    surface,
    editPlanButton,
    scroller,
    update,
    region,
    plan,
    markers,
    newAlerts,
    floorAlerts,
    floors,
    floor,
    buildings,
    geographyMarkers,
    selectFloor,
    scale,
    chooseFromMarker,
    changeSurface,
    pick,
    view,
    admin,
    points,
  } = m;
  return (
    <section className="ws-panel ws-pad">
      <div className="ws-map-toolbar">
        {surface && (scene.plans.length > 1 || !plan) && (
          <label>
            {t("maps.plan")}{" "}
            <select
              value={surface}
              onChange={(e) => changeSurface(e.target.value)}
            >
              {!plan && (
                <option value={surface} disabled>
                  {t("maps.choosePlan")}
                </option>
              )}
              {scene.plans.map((p) => (
                <option key={p.id} value={p.id}>
                  {p.name}
                </option>
              ))}
            </select>
          </label>
        )}
        {!surface && scene.regions.length > 1 && (
          <label>
            {t("plan.region")}{" "}
            <select
              value={region?.id}
              onChange={(e) =>
                update({
                  region: e.target.value,
                  lng: null,
                  lat: null,
                  z: null,
                })
              }
            >
              {scene.regions.map((r) => (
                <option key={r.id} value={r.id}>
                  {r.name}
                </option>
              ))}
            </select>
          </label>
        )}
      </div>
      {surface ? (
        plan ? (
          <>
            <div className="ws-plan-heading">
              <h2>{plan.name}</h2>
              <PlanZoom
                label={t("maps.planZoom")}
                levels={[1, 2]}
                value={scale}
                onChange={(level) => update({ scale: String(level) })}
              />
            </div>
            {(admin || (plan.footprint && region)) && (
              <div className="ws-map-actions">
                {admin && (
                  <Button
                    ref={editPlanButton}
                    onClick={() => setEditingPlan(plan.id)}
                  >
                    {plan.footprint
                      ? t("maps.configureGeoreference")
                      : t("maps.georeference")}
                  </Button>
                )}
                {plan.footprint && region && (
                  <Button onClick={() => selectFloor(plan.id, true)}>
                    {t("maps.showOnMap")}
                  </Button>
                )}
              </div>
            )}
            <div
              className="ws-plan-scroll"
              ref={scroller}
              onScroll={(e) =>
                update(
                  {
                    sx: String(e.currentTarget.scrollLeft),
                    sy: String(e.currentTarget.scrollTop),
                  },
                  true,
                )
              }
            >
              <div className="ws-plan" style={{ width: scale * 100 + "%" }}>
                <img
                  src={plan.image_url}
                  width={plan.width}
                  height={plan.height}
                  alt={plan.name}
                  onClick={(e) => {
                    const rect = e.currentTarget.getBoundingClientRect();
                    pick(
                      (e.clientX - rect.left) / rect.width,
                      (e.clientY - rect.top) / rect.height,
                    );
                  }}
                />
                {markers.map((m) => (
                  <CameraMarker
                    key={m.point_id}
                    marker={m}
                    newAlerts={newAlerts.get(m.point_id) ?? 0}
                    name={points.find((p) => p.id === m.point_id)?.name ?? ""}
                    selected={selected === m.point_id}
                    choose={() => chooseFromMarker(m.point_id)}
                    style={{
                      left: m.x * 100 + "%",
                      top: m.y * 100 + "%",
                    }}
                  />
                ))}
              </div>
            </div>
          </>
        ) : (
          <p role="status">{t("maps.planUnavailable")}</p>
        )
      ) : region ? (
        <>
          <h2>{region.name}</h2>
          {floors.length > 0 && (
            <label>
              {t("maps.planOnMap")}
              <select
                value={floor?.id ?? ""}
                onChange={(e) => selectFloor(e.target.value)}
              >
                <option value="">{t("maps.overview")}</option>
                {floors
                  .filter((p) => !p.building)
                  .map((p) => (
                    <option key={p.id} value={p.id}>
                      {p.name}
                    </option>
                  ))}
                {[...buildings].map(([name, levels]) => (
                  <optgroup key={name} label={name}>
                    {levels.map((p) => (
                      <option key={p.id} value={p.id}>
                        {p.name}
                      </option>
                    ))}
                  </optgroup>
                ))}
              </select>
            </label>
          )}
          {floor &&
            !coveringRegion(
              region ? [region] : [],
              region,
              floor.footprint?.center,
            ) && <p role="status">{t("maps.outsideRegion")}</p>}
          {floor && (
            <label>
              {t("plan.opacity")}
              <input
                type="range"
                min="0"
                max="1"
                step=".05"
                value={opacity}
                onChange={(e) => setOpacity(Number(e.target.value))}
              />
            </label>
          )}
          <Suspense fallback={<p>{t("maps.loadingMap")}</p>}>
            <RegionMap
              key={region.revision + "/" + (floor?.id ?? "")}
              region={region}
              markers={geographyMarkers}
              newAlerts={newAlerts}
              overlay={
                floor?.footprint
                  ? {
                      image: floor.image_url,
                      corners: floor.footprint.corners,
                      opacity,
                    }
                  : undefined
              }
              fitOverlay={!view}
              labels={
                floor
                  ? []
                  : [...buildings].map(([name, levels]) => {
                      const first =
                        levels.find((p) => floorAlerts.has(p.id)) ?? levels[0]!;
                      const count = levels.reduce(
                        (sum, p) => sum + (floorAlerts.get(p.id) ?? 0),
                        0,
                      );
                      const [lng, lat] = first.footprint!.center;
                      return {
                        id: name,
                        text: count
                          ? name + " · " + t("alerts.newCount", { count })
                          : name,
                        lng: lng!,
                        lat: lat!,
                        activate: () => selectFloor(first.id),
                      };
                    })
              }
              points={points}
              chosen={selected}
              view={view}
              select={chooseFromMarker}
              pick={pick}
              move={(v) =>
                update(
                  {
                    lng: fixed(v.lng),
                    lat: fixed(v.lat),
                    z: fixed(v.zoom),
                  },
                  true,
                )
              }
            />
          </Suspense>
          <p>
            {t("maps.dataAt")}{" "}
            <time dateTime={region.source_time}>
              {dateTimeFormat({
                dateStyle: "medium",
                timeStyle: "short",
                timeZone: "UTC",
              }).format(new Date(region.source_time))}{" "}
              UTC
            </time>{" "}
            ·{" "}
            <a href="https://www.openstreetmap.org/copyright">
              {region.attribution}
            </a>
          </p>
        </>
      ) : (
        <p role="status">{t("maps.noRegion")}</p>
      )}
    </section>
  );
}
