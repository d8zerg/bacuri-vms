import "../../i18n/extended";
// The state of the maps screen: the scene read from the server, the selection kept in the address and the
// editing of positions and plans.
import { useResource } from "../../adapters/resource";
import {
  FilterPanel,
  matchesQuery,
  needsListSearch,
} from "../../ui-kit/FilterPanel";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import type { FormEvent } from "react";
import { useSearchParams } from "react-router";

import type {
  Services,
  MapScene,
  Point,
  Feed,
} from "../../contracts/workspace";
import { Button } from "../../ui-kit/FormControls";

import { landmarkBearing, type Landmark } from "../ViewCalibration";
import { coveringRegion } from "../mapRegions";

import { useT } from "../../i18n";
const fixed = (v: number) => String(Number(v.toFixed(6)));
export type Props = {
  services: Services;
  site: string;
  admin: boolean;
  points: Point[];
  feed: Feed;
};
export function useMapsState({ services, site, admin, points, feed }: Props) {
  const t = useT();
  const [params, set] = useSearchParams();
  // The scene follows the placements made by other administrators and the progress of map packages.
  const reading = useResource(
    services,
    "maps:" + site,
    (signal) => services.maps(site, signal),
    { interval: 2000, timeout: 4000, retries: 0 },
  );
  // A failed read hides the scene, so the screen never shows positions that may be outdated.
  const scene: MapScene | null = reading.error ? null : (reading.value ?? null),
    failed = !!reading.error;
  const [notice, tell] = useState(""),
    [busy, lock] = useState(false);
  const [draft, setDraft] = useState<{ x: string; y: string } | null>(null);
  const [editingPlan, setEditingPlan] = useState("");
  const [opacity, setOpacity] = useState(0.65);
  const [aim, setAim] = useState(false);
  const [landmark, setLandmark] = useState<Landmark | null>(null);
  const selected = params.get("point") ?? "",
    surface = params.get("plan") ?? "";
  const previousPlan = useRef("");
  const focus = useRef(false);
  const focusCameraTitle = useRef(false);
  const positionEditor = useRef<HTMLDetailsElement>(null);
  const editPlanButton = useRef<HTMLButtonElement>(null);
  const cameraTitle = useRef<HTMLHeadingElement>(null);
  const scroller = useRef<HTMLDivElement>(null);
  const current = useRef(params);
  useEffect(() => {
    current.current = params;
  }, [params]);
  const update = useCallback(
    (values: Record<string, string | null>, replace = false) => {
      const next = new URLSearchParams(current.current);
      for (const [key, value] of Object.entries(values)) {
        if (value === null) next.delete(key);
        else next.set(key, value);
      }
      if (next.toString() === current.current.toString()) return;
      current.current = next;
      void set(next, { replace });
    },
    [set],
  );
  const region =
    scene?.regions.find((r) => r.id === params.get("region")) ?? scene?.region;
  const plan = scene?.plans.find((p) => p.id === surface);
  const markers = useMemo(
    () =>
      scene?.placements.filter(
        (p) =>
          (p.plan_id ?? "") === surface &&
          points.some((point) => point.id === p.point_id),
      ) ?? [],
    [scene, surface, points],
  );
  const newAlerts = useMemo(() => {
    const counts = new Map<string, number>();
    if (feed.status === "online")
      for (const item of feed.items) {
        if (item.work_state === "new")
          counts.set(item.point_id, (counts.get(item.point_id) ?? 0) + 1);
      }
    return counts;
  }, [feed.items, feed.status]);
  const floorAlerts = new Map<string, number>();
  for (const marker of scene?.placements ?? []) {
    if (marker.source_plan_id && newAlerts.has(marker.point_id))
      floorAlerts.set(
        marker.source_plan_id,
        (floorAlerts.get(marker.source_plan_id) ?? 0) +
          newAlerts.get(marker.point_id)!,
      );
  }
  const floors = scene?.plans.filter((p) => p.footprint) ?? [];
  const floor = floors.find((p) => p.id === params.get("floor"));
  const buildings = new Map<string, typeof floors>();
  for (const linked of floors) {
    if (!linked.building) continue;
    const group = buildings.get(linked.building) ?? [];
    group.push(linked);
    buildings.set(linked.building, group);
  }
  const floorPoints = new Set(
    floor
      ? scene?.placements
          .filter((p) => p.plan_id === floor.id)
          .map((p) => p.point_id)
      : [],
  );
  const groupedPlans = new Set(
    floors.filter((p) => p.building).map((p) => p.id),
  );
  const geographyMarkers = markers.filter((m) =>
    floor
      ? floorPoints.has(m.point_id)
      : m.point_id === selected ||
        !m.source_plan_id ||
        !groupedPlans.has(m.source_plan_id),
  );
  const visibleMarkers = surface ? markers : geographyMarkers;
  const marker = visibleMarkers.find((p) => p.point_id === selected);
  const availablePoints =
    admin && !floor
      ? points
      : points.filter((p) => visibleMarkers.some((m) => m.point_id === p.id));
  function selectFloor(id: string, keepPoint = false) {
    setDraft(null);
    setAim(false);
    setLandmark(null);
    const next = coveringRegion(
      scene?.regions ?? [],
      region,
      floors.find((p) => p.id === id)?.footprint?.center,
    );
    update({
      plan: null,
      review: null,
      floor: id || null,
      region: next?.id ?? region?.id ?? null,
      point: keepPoint ? selected || null : null,
      lng: null,
      lat: null,
      z: null,
    });
  }
  const point = points.find((p) => p.id === selected);
  const scale = Number(params.get("scale")) === 2 ? 2 : 1;
  useEffect(() => {
    if (!scene || focus.current) return;
    const review = current.current.get("review");
    const el = focusCameraTitle.current
      ? cameraTitle.current
      : (review && document.getElementById("map-alert-" + review)) ||
        document.getElementById("map-point-" + selected);
    if (el) {
      el.focus({ preventScroll: !focusCameraTitle.current });
      focusCameraTitle.current = false;
      focus.current = true;
    }
    if (scroller.current)
      scroller.current.scrollTo(
        Number(current.current.get("sx")),
        Number(current.current.get("sy")),
      );
  }, [scene, selected]);
  function choose(id: string) {
    focus.current = false;
    setDraft(null);
    setAim(false);
    setLandmark(null);
    tell("");
    update(
      id === selected
        ? { point: id }
        : {
            point: id,
            alert: null,
            review: null,
            focus: null,
            at: null,
            from: null,
            to: null,
            scroll: null,
          },
    );
  }
  function chooseFromMarker(id: string) {
    choose(id);
    if (!window.matchMedia("(max-width: 56rem)").matches) return;
    if (id === selected) {
      cameraTitle.current?.focus();
      focus.current = true;
    } else focusCameraTitle.current = true;
  }
  function changeSurface(id: string) {
    if (surface === id) return;
    if (plan) previousPlan.current = plan.id;
    setDraft(null);
    setAim(false);
    setLandmark(null);
    tell("");
    setEditingPlan("");
    update({
      floor: null,
      review: null,
      plan: id || null,
      sx: null,
      sy: null,
      scale: null,
    });
  }
  function pick(x: number, y: number) {
    if (admin && aim && marker) {
      const bearing = landmarkBearing(marker, x, y, plan);
      if (bearing === null) {
        tell(t("maps.aimAside"));
        return;
      }
      setLandmark({ bearing, camera: marker.camera });
      setAim(false);
      tell("");
      return;
    }
    if (
      admin &&
      point &&
      positionEditor.current?.open &&
      (!floor || marker) &&
      !marker?.source_plan_id
    ) {
      setDraft({ x: fixed(x), y: fixed(y) });
      tell("");
    }
  }
  function target(mode: string, alert?: string) {
    const next = new URLSearchParams(params);
    next.set("point", selected);
    next.set("mode", mode);
    if (alert) {
      for (const key of ["at", "from", "to", "focus"]) next.delete(key);
      next.set("alert", alert);
    } else if (mode === "live") {
      const currentAlert = feed.items.find((a) => a.id === next.get("alert"));
      if (currentAlert && !next.has("at"))
        next.set("at", currentAlert.observed_at);
      next.delete("alert");
    }
    return "/monitor?" + next.toString();
  }
  async function save(e: FormEvent) {
    e.preventDefault();
    if (!draft || !point || busy) return;
    lock(true);
    try {
      await services.place(site, {
        point_id: selected,
        plan_id: surface || null,
        x: Number(draft.x),
        y: Number(draft.y),
      });
      reading.reload();
      setDraft(null);
      tell(t("maps.positionSaved"));
    } catch {
      tell(t("maps.positionFailed"));
    } finally {
      lock(false);
    }
  }
  async function upload(e: FormEvent<HTMLFormElement>) {
    e.preventDefault();
    if (busy) return;
    const form = e.currentTarget,
      fields = new FormData(form),
      file = fields.get("image"),
      name = fields.get("name");
    if (typeof name !== "string") return;
    if (!(file instanceof File) || !file.size || file.size > 4194304) {
      tell(t("maps.pngLimit"));
      return;
    }
    lock(true);
    try {
      const reader = new FileReader();
      const encoded = await new Promise<string>((resolve, reject) => {
        reader.onload = () =>
          typeof reader.result === "string"
            ? resolve(reader.result.split(",")[1]!)
            : reject(Error());
        reader.onerror = () => reject(Error());
        reader.readAsDataURL(file);
      });
      const result = await services.addPlan(site, name, encoded);
      reading.reload();
      changeSurface(result.id);
      form.reset();
      tell(t("maps.planAdded"));
    } catch {
      tell(t("maps.planFailed"));
    } finally {
      lock(false);
    }
  }
  const coordinates = draft ?? {
    x: marker ? fixed(marker.x) : "",
    y: marker ? fixed(marker.y) : "",
  };
  const viewValues = ["lng", "lat", "z"].map((key) =>
    params.has(key) ? Number(params.get(key)) : NaN,
  );
  const [lng, lat, zoom] = viewValues;
  const view =
    lng != null &&
    lat != null &&
    zoom != null &&
    Number.isFinite(lng) &&
    Math.abs(lat) <= 85.051129 &&
    zoom >= 0 &&
    zoom <= 24
      ? { lng, lat, zoom }
      : null;
  const [pointQuery, setPointQuery] = useState("");
  const otherPoints = availablePoints.filter((p) => p.id !== selected);
  const listedPoints = otherPoints.filter((p) =>
    matchesQuery(pointQuery, p.name, p.id),
  );
  const pointList = (
    <>
      {needsListSearch(otherPoints.length, pointQuery) && (
        <FilterPanel
          label={t("maps.pointFilters")}
          queryLabel={t("maps.pointSearch")}
          query={pointQuery}
          onQuery={setPointQuery}
          onReset={() => setPointQuery("")}
        />
      )}
      {!listedPoints.length && pointQuery && (
        <p role="status">{t("maps.noPoints")}</p>
      )}
      <ul className="ws-map-list">
        {listedPoints.map((p) => (
          <li key={p.id}>
            <Button
              id={"map-point-" + p.id}
              aria-pressed={selected === p.id}
              onClick={() => choose(p.id)}
            >
              {p.name}
              {newAlerts.has(p.id) && (
                <span className="ws-point-alert">
                  {" "}
                  · {t("alerts.newCount", { count: newAlerts.get(p.id) ?? 0 })}
                </span>
              )}
            </Button>
          </li>
        ))}
      </ul>
    </>
  );
  return {
    t,
    params,
    set,
    reading,
    scene,
    failed,
    notice,
    tell,
    busy,
    lock,
    draft,
    setDraft,
    editingPlan,
    setEditingPlan,
    opacity,
    setOpacity,
    aim,
    setAim,
    landmark,
    setLandmark,
    selected,
    surface,
    previousPlan,
    focus,
    focusCameraTitle,
    positionEditor,
    editPlanButton,
    cameraTitle,
    scroller,
    current,
    update,
    region,
    plan,
    markers,
    newAlerts,
    floorAlerts,
    floors,
    floor,
    buildings,
    floorPoints,
    groupedPlans,
    geographyMarkers,
    visibleMarkers,
    marker,
    availablePoints,
    selectFloor,
    point,
    scale,
    choose,
    chooseFromMarker,
    changeSurface,
    pick,
    target,
    save,
    upload,
    coordinates,
    viewValues,
    lng,
    lat,
    zoom,
    view,
    pointQuery,
    setPointQuery,
    otherPoints,
    listedPoints,
    pointList,
    services,
    site,
    admin,
    points,
    feed,
  };
}
export type MapsState = ReturnType<typeof useMapsState>;
