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
import { Link, useSearchParams } from "react-router";
import type { Services, MapScene, Point, Feed } from "../contracts/workspace";
import { Button } from "../ui-kit/FormControls";
import "./maps.css";
const RegionMap = lazy(() => import("./RegionMap"));
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
  const selected = params.get("point") ?? "",
    surface = params.get("plan") ?? "";
  const focus = useRef(false);
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
  const plan = scene?.plans.find((p) => p.id === surface);
  const marker = scene?.placements.find(
    (p) => p.point_id === selected && (p.plan_id ?? "") === surface,
  );
  const markers = useMemo(
    () =>
      scene?.placements.filter(
        (p) =>
          (p.plan_id ?? "") === surface &&
          points.some((point) => point.id === p.point_id),
      ) ?? [],
    [scene, surface, points],
  );
  const point = points.find((p) => p.id === selected);
  const scale = Number(params.get("scale")) === 2 ? 2 : 1;
  useEffect(() => {
    if (!scene || focus.current) return;
    const el = document.getElementById("map-point-" + selected);
    if (el) {
      el.focus({ preventScroll: true });
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
    tell("");
    update(
      id === selected
        ? { point: id }
        : {
            point: id,
            alert: null,
            focus: null,
            at: null,
            from: null,
            to: null,
            scroll: null,
          },
    );
  }
  function changeSurface(id: string) {
    setDraft(null);
    tell("");
    update({ plan: id || null, sx: null, sy: null, scale: null });
  }
  function pick(x: number, y: number) {
    if (admin && point) {
      setDraft({ x: fixed(x), y: fixed(y) });
      tell("");
    }
  }
  function target(mode: string, alert?: string) {
    const next = new URLSearchParams(params);
    next.set("point", selected);
    next.set("mode", mode);
    if (alert) {
      if (next.get("alert") !== alert) {
        for (const key of ["at", "from", "to", "focus"]) next.delete(key);
      }
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
  return (
    <>
      <div className="ws-title">
        <h1>Карты и планы</h1>
      </div>
      {!scene ? (
        <p role="status">
          {failed
            ? "Не удалось загрузить карты и планы"
            : "Загрузка карт и планов…"}
        </p>
      ) : (
        <div className="ws-maps">
          <section className="ws-panel ws-pad">
            <label>
              Карта или план{" "}
              <select
                value={surface}
                onChange={(e) => changeSurface(e.target.value)}
              >
                <option value="">Карта региона</option>
                {scene.plans.map((p) => (
                  <option key={p.id} value={p.id}>
                    {p.name}
                  </option>
                ))}
              </select>
            </label>
            {surface ? (
              plan ? (
                <>
                  <h2>{plan.name}</h2>
                  <label>
                    Масштаб плана{" "}
                    <select
                      value={scale}
                      onChange={(e) => update({ scale: e.target.value })}
                    >
                      <option value={1}>100%</option>
                      <option value={2}>200%</option>
                    </select>
                  </label>
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
                        <button
                          type="button"
                          key={m.point_id}
                          className="ws-pin"
                          style={{
                            left: m.x * 100 + "%",
                            top: m.y * 100 + "%",
                          }}
                          aria-pressed={selected === m.point_id}
                          onClick={() => choose(m.point_id)}
                        >
                          {points.find((p) => p.id === m.point_id)?.name}
                        </button>
                      ))}
                    </div>
                  </div>
                </>
              ) : (
                <p role="status">План недоступен</p>
              )
            ) : scene.region ? (
              <>
                <h2>{scene.region.name}</h2>
                <Suspense fallback={<p>Загрузка карты…</p>}>
                  <RegionMap
                    key={scene.region.revision}
                    region={scene.region}
                    markers={markers}
                    points={points}
                    chosen={selected}
                    view={view}
                    select={choose}
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
                  <time dateTime={scene.region.source_time}>
                    {new Intl.DateTimeFormat("ru-RU", {
                      dateStyle: "medium",
                      timeStyle: "short",
                      timeZone: "UTC",
                    }).format(new Date(scene.region.source_time))}{" "}
                    UTC
                  </time>{" "}
                  ·{" "}
                  <a href="https://www.openstreetmap.org/copyright">
                    {scene.region.attribution}
                  </a>
                </p>
              </>
            ) : (
              <p role="status">Карта региона не установлена</p>
            )}
          </section>
          <section className="ws-panel ws-pad">
            <h2>Точки наблюдения</h2>
            <ul className="ws-map-list">
              {(admin
                ? points
                : points.filter((p) => markers.some((m) => m.point_id === p.id))
              ).map((p) => (
                <li key={p.id}>
                  <Button
                    id={"map-point-" + p.id}
                    aria-pressed={selected === p.id}
                    onClick={() => choose(p.id)}
                  >
                    {p.name}
                  </Button>
                </li>
              ))}
            </ul>
            {!markers.length && (
              <p>На карте или плане пока нет доступных точек</p>
            )}
            {point && (admin || marker) && (
              <>
                <h3>{point.name}</h3>
                {marker && (
                  <p>
                    {plan ? "X, Y" : "Долгота, широта"} · {fixed(marker.x)},{" "}
                    {fixed(marker.y)}
                  </p>
                )}
                <div className="ws-map-actions">
                  <Link to={target("live")}>Эфир</Link>
                  <Link to={target("archive")}>Архив</Link>
                </div>
                {feed.items
                  .filter((a) => a.point_id === selected)
                  .map((a) => (
                    <Link key={a.id} to={target("archive", a.id)}>
                      {a.type} · {a.observed_at}
                    </Link>
                  ))}
                {admin && (plan || (!surface && scene.region)) && (
                  <form onSubmit={(e) => void save(e)}>
                    <h3>Положение точки</h3>
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
                            setDraft({ ...coordinates, [axis]: e.target.value })
                          }
                        />
                      </label>
                    ))}
                    <Button type="submit" disabled={!draft || busy}>
                      Сохранить положение
                    </Button>
                  </form>
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
            <p role="status">{notice}</p>
          </section>
        </div>
      )}
    </>
  );
}
