import {
  FilterPanel,
  matchesQuery,
  needsListSearch,
} from "../ui-kit/FilterPanel";
import {
  lazy,
  Suspense,
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import type { FormEvent } from "react";
import { useSearchParams } from "react-router";
import { SubsectionMenu } from "./SubsectionMenu";
import { RouteButton } from "./RouteButton";
import type { Services, MapScene, Point, Feed } from "../contracts/workspace";
import { Button } from "../ui-kit/FormControls";
import { CameraEvents } from "./CameraEvents";
import { PlanZoom } from "./PlanZoom";
import { CameraMarker, viewLabel } from "./CameraMarker";
import {
  ViewCalibration,
  landmarkBearing,
  type Landmark,
} from "./ViewCalibration";
import { coveringRegion } from "./mapRegions";
import { MapPackages } from "./MapPackages";
import "./maps.css";
const RegionMap = lazy(() => import("./RegionMap"));
const PlanGeoreference = lazy(() => import("./PlanGeoreference"));
const fixed = (v: number) => String(Number(v.toFixed(6)));
type Props = {
  services: Services;
  site: string;
  admin: boolean;
  points: Point[];
  feed: Feed;
};
export default function Maps({ services, site, admin, points, feed }: Props) {
  const [params, set] = useSearchParams();
  const [scene, show] = useState<MapScene | null>(null),
    [failed, fail] = useState(false),
    [notice, tell] = useState(""),
    [version, refresh] = useState(0),
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
  useEffect(() => {
    const ctl = new AbortController();
    let active = false;
    async function read() {
      if (active) return;
      active = true;
      try {
        const result = await services.maps(
          site,
          AbortSignal.any([ctl.signal, AbortSignal.timeout(4000)]),
        );
        if (!ctl.signal.aborted) {
          show((old) =>
            JSON.stringify(old) === JSON.stringify(result) ? old : result,
          );
          fail(false);
        }
      } catch {
        if (!ctl.signal.aborted) {
          show(null);
          fail(true);
        }
      } finally {
        active = false;
      }
    }
    void read();
    const timer = setInterval(() => void read(), 2000);
    return () => {
      ctl.abort();
      clearInterval(timer);
    };
  }, [site, services, version]);
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
        tell("Выберите ориентир в стороне от камеры");
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
      refresh((v) => v + 1);
      setDraft(null);
      tell("Положение сохранено");
    } catch {
      tell("Не удалось сохранить положение точки");
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
      tell("Выберите PNG до 4 МиБ");
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
      refresh((v) => v + 1);
      changeSurface(result.id);
      form.reset();
      tell("План добавлен");
    } catch {
      tell("Не удалось добавить план. Проверьте формат и размер PNG.");
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
          label="Фильтры точек наблюдения"
          queryLabel="Поиск точки"
          query={pointQuery}
          onQuery={setPointQuery}
          onReset={() => setPointQuery("")}
        />
      )}
      {!listedPoints.length && pointQuery && (
        <p role="status">Точки не найдены.</p>
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
                  · Новых тревог: {newAlerts.get(p.id)}
                </span>
              )}
            </Button>
          </li>
        ))}
      </ul>
    </>
  );
  return (
    <>
      {scene && !(admin && plan && editingPlan === plan.id) && (
        <SubsectionMenu
          label="Подразделы карт и планов"
          items={[
            {
              label: "Карта региона",
              icon: "map",
              active: !surface,
              select: () => changeSurface(""),
            },
            {
              label: "Планы",
              icon: "layers",
              active: !!surface,
              disabled: !scene.plans.length,
              select: () => {
                const next =
                  plan ??
                  scene.plans.find((p) => p.id === previousPlan.current) ??
                  scene.plans[0];
                if (next) changeSurface(next.id);
              },
            },
          ]}
        />
      )}
      {!scene ? (
        <p role="status">
          {failed
            ? "Не удалось загрузить карты и планы"
            : "Загрузка карт и планов…"}
        </p>
      ) : admin && plan && editingPlan === plan.id ? (
        <Suspense fallback={<p>Загрузка привязки…</p>}>
          <PlanGeoreference
            key={site + plan.id}
            plan={plan}
            scene={scene}
            initialRegion={region?.id}
            points={points}
            site={site}
            services={services}
            close={(saved) => {
              setEditingPlan("");
              requestAnimationFrame(() => editPlanButton.current?.focus());
              if (saved) {
                refresh((v) => v + 1);
                tell("Привязка обновлена");
              }
            }}
          />
        </Suspense>
      ) : (
        <div className="ws-maps">
          <section className="ws-panel ws-pad">
            <div className="ws-map-toolbar">
              {surface && (scene.plans.length > 1 || !plan) && (
                <label>
                  План{" "}
                  <select
                    value={surface}
                    onChange={(e) => changeSurface(e.target.value)}
                  >
                    {!plan && (
                      <option value={surface} disabled>
                        Выберите план
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
                  Регион{" "}
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
                      label="Масштаб плана"
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
                            ? "Настроить привязку к карте"
                            : "Привязать план к карте"}
                        </Button>
                      )}
                      {plan.footprint && region && (
                        <Button onClick={() => selectFloor(plan.id, true)}>
                          Показать на геокарте
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
                    <div
                      className="ws-plan"
                      style={{ width: scale * 100 + "%" }}
                    >
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
                          name={
                            points.find((p) => p.id === m.point_id)?.name ?? ""
                          }
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
                <p role="status">План недоступен</p>
              )
            ) : region ? (
              <>
                <h2>{region.name}</h2>
                {floors.length > 0 && (
                  <label>
                    План на геокарте
                    <select
                      value={floor?.id ?? ""}
                      onChange={(e) => selectFloor(e.target.value)}
                    >
                      <option value="">Обзор территории</option>
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
                  ) && (
                    <p role="status">
                      План находится за пределами выбранного региона.
                    </p>
                  )}
                {floor && (
                  <label>
                    Непрозрачность плана
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
                <Suspense fallback={<p>Загрузка карты…</p>}>
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
                              levels.find((p) => floorAlerts.has(p.id)) ??
                              levels[0]!;
                            const count = levels.reduce(
                              (sum, p) => sum + (floorAlerts.get(p.id) ?? 0),
                              0,
                            );
                            const [lng, lat] = first.footprint!.center;
                            return {
                              id: name,
                              text: count
                                ? name + " · Новых тревог: " + count
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
                  Данные на{" "}
                  <time dateTime={region.source_time}>
                    {new Intl.DateTimeFormat("ru-RU", {
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
              <p role="status">Карта региона не установлена</p>
            )}
          </section>
          <section className="ws-panel ws-pad">
            {(!point || (!admin && !marker)) && <h2>Точки наблюдения</h2>}
            {otherPoints.length > 0 &&
              (point && (admin || marker) ? (
                <details key={selected}>
                  <summary>Сменить точку</summary>
                  {pointList}
                </details>
              ) : (
                pointList
              ))}

            {!visibleMarkers.length && (
              <p>На карте или плане пока нет доступных точек</p>
            )}
            {point && (admin || marker) && (
              <>
                <h2
                  tabIndex={-1}
                  ref={cameraTitle}
                  id={"map-point-" + point.id}
                >
                  {point.name}
                </h2>
                <div className="ws-map-actions">
                  <RouteButton to={target("live")}>Эфир</RouteButton>
                  <RouteButton to={target("archive")}>Архив</RouteButton>
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
                    Открыть план ·{" "}
                    {floors.find((p) => p.id === marker.source_plan_id)?.name}
                  </Button>
                )}
                {admin && marker?.camera && (
                  <ViewCalibration
                    key={
                      selected + "/" + surface + "/" + marker.camera.device_id
                    }
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
                      refresh((v) => v + 1);
                    }}
                  />
                )}
                {admin &&
                (!floor || marker) &&
                !marker?.source_plan_id &&
                (plan || (!surface && region)) ? (
                  <details
                    key={
                      site +
                      "/" +
                      surface +
                      "/" +
                      selected +
                      "/" +
                      (region?.id ?? "")
                    }
                    ref={positionEditor}
                    onToggle={(e) => {
                      if (!e.currentTarget.open) setDraft(null);
                    }}
                  >
                    <summary>Изменить положение точки</summary>
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
                              ? "Долгота"
                              : "Широта"}
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
                        Сохранить положение
                      </Button>
                    </form>
                  </details>
                ) : (
                  marker && (
                    <details>
                      <summary>Параметры точки</summary>
                      <p>
                        {plan ? "X, Y" : "Долгота, широта"} · {fixed(marker.x)},{" "}
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
                <summary>Добавить план</summary>
                <form onSubmit={(e) => void upload(e)}>
                  <label>
                    Название плана
                    <input name="name" required maxLength={128} />
                  </label>
                  <label>
                    Изображение PNG
                    <input
                      name="image"
                      required
                      type="file"
                      accept="image/png"
                    />
                  </label>
                  <Button type="submit" disabled={busy}>
                    Загрузить план
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
        </div>
      )}
    </>
  );
}
