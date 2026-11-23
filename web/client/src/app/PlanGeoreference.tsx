import { useState } from "react";
import type { FormEvent } from "react";
import type {
  FloorPlan,
  MapScene,
  PlanLinkPreview,
  PlanReference,
  Point,
  Services,
} from "../contracts/workspace";
import { Button } from "../ui-kit/FormControls";
import { coveringRegion } from "./mapRegions";
import { PlanZoom } from "./PlanZoom";
import RegionMap from "./RegionMap";
import type { MapLabel } from "./RegionMap";

type Anchor = PlanReference["anchor"];
type Draft = Record<keyof Anchor, string>;
type Mode = "pair" | "width" | "unlink";
function draft(value?: Anchor): Draft {
  return {
    x: String(value?.x ?? ""),
    y: String(value?.y ?? ""),
    lng: String(value?.lng ?? ""),
    lat: String(value?.lat ?? ""),
  };
}
function anchor(d: Draft): Anchor {
  if (
    Object.values(d).some((v) => v.trim() === "" || !Number.isFinite(Number(v)))
  )
    throw Error("anchor");
  return {
    x: Number(d.x),
    y: Number(d.y),
    lng: Number(d.lng),
    lat: Number(d.lat),
  };
}
const fixed = (n: number) => String(Number(n.toFixed(8)));
const axisLabels = {
  x: "X на плане (0-1)",
  y: "Y на плане (0-1)",
  lat: "Широта",
  lng: "Долгота",
};
type Props = {
  plan: FloorPlan;
  scene: MapScene;
  initialRegion: string | undefined;
  points: Point[];
  site: string;
  services: Services;
  close: (saved: boolean) => void;
};
export default function PlanGeoreference({
  plan,
  scene,
  initialRegion,
  points,
  site,
  services,
  close,
}: Props) {
  const original = plan.definition;
  const [mode, setMode] = useState<Mode>(
    original?.width_m !== undefined ? "width" : "pair",
  );
  const [anchors, setAnchors] = useState<[Draft, Draft]>(() => [
    draft(original?.anchor),
    draft(original?.second),
  ]);
  const [active, setActive] = useState<0 | 1>(0);
  const [width, setWidth] = useState(String(original?.width_m ?? ""));
  const [bearing, setBearing] = useState(String(original?.bearing ?? 0));
  const [building, setBuilding] = useState(plan.building ?? "");
  const [regionId, setRegion] = useState(
    () =>
      coveringRegion(
        scene.regions,
        scene.regions.find((r) => r.id === initialRegion),
        plan.footprint?.center,
      )?.id ?? initialRegion,
  );
  const [scale, setScale] = useState(1);
  const [opacity, setOpacity] = useState(0.55);
  const [preview, setPreview] = useState<PlanLinkPreview | null>(null);
  const [choices, setChoices] = useState<Record<string, "keep" | "plan">>({});
  const [notice, setNotice] = useState(""),
    [busy, setBusy] = useState(false);
  const region = scene.regions.find((r) => r.id === regionId) ?? scene.region;
  const labels: MapLabel[] = [];
  function reset() {
    setPreview(null);
    setChoices({});
    setNotice("");
  }
  function changeAnchor(values: Partial<Draft>, clicked = false) {
    if (busy) return;
    reset();
    const changed = { ...anchors[active], ...values };
    setAnchors(active === 0 ? [changed, anchors[1]] : [anchors[0], changed]);
    if (
      clicked &&
      mode === "pair" &&
      active === 0 &&
      Object.values(changed).every(Boolean) &&
      Object.values(anchors[1]).some((v) => !v)
    )
      setActive(1);
  }
  function definition(): PlanReference | null {
    if (mode === "unlink") return null;
    const a = anchor(anchors[0]);
    return {
      anchor: a,
      ...(mode === "pair"
        ? { second: anchor(anchors[1]) }
        : { width_m: Number(width), bearing: Number(bearing) }),
      ...(building.trim() ? { building: building.trim() } : {}),
    };
  }
  async function inspect(e: FormEvent) {
    e.preventDefault();
    let value;
    try {
      value = definition();
    } catch {
      setNotice("Укажите положение каждого ориентира на плане и на карте.");
      return;
    }
    setBusy(true);
    reset();
    try {
      const result = await services.previewPlan(site, {
        plan_id: plan.id,
        definition: value,
      });
      setPreview(result);
      const covering = coveringRegion(
        scene.regions,
        region,
        result.footprint?.center,
      );
      if (covering) setRegion(covering.id);
      else if (region && result.footprint)
        setNotice(
          "План находится за пределами установленных регионов. Проверьте координаты или установите нужную карту.",
        );
    } catch {
      setNotice(
        "Не удалось построить привязку. Проверьте координаты, расстояние между ориентирами и доступность сервера.",
      );
    } finally {
      setBusy(false);
    }
  }
  async function save() {
    if (!preview || busy) return;
    setBusy(true);
    try {
      await services.linkPlan(site, {
        plan_id: plan.id,
        definition: definition(),
        token: preview.token,
        choices,
      });
      close(true);
    } catch {
      reset();
      setNotice(
        "Не удалось сохранить привязку. Повторите предпросмотр, чтобы проверить актуальные положения камер.",
      );
    } finally {
      setBusy(false);
    }
  }
  const shown = mode === "pair" ? anchors : [anchors[0]];
  if (mode !== "unlink") {
    shown.forEach((a, i) => {
      if (
        a.lng &&
        a.lat &&
        Math.abs(Number(a.lng)) <= 180 &&
        Math.abs(Number(a.lat)) <= 85.051129
      )
        labels.push({
          id: "anchor" + i,
          text: i ? "B" : "A",
          lng: Number(a.lng),
          lat: Number(a.lat),
        });
    });
    for (const camera of preview?.cameras ?? []) {
      const name =
        points.find((p) => p.id === camera.point_id)?.name ?? camera.point_id;
      labels.push({
        id: camera.point_id,
        text: name,
        lng: camera.projected[0]!,
        lat: camera.projected[1]!,
      });
      if (camera.existing)
        labels.push({
          id: "existing" + camera.point_id,
          text:
            name + (camera.existing_plan_id ? " · другой план" : " · вручную"),
          lng: camera.existing[0]!,
          lat: camera.existing[1]!,
        });
    }
  }
  const conflicts = preview?.cameras.filter((c) => c.existing) ?? [];
  return (
    <section className="ws-maps ws-reference">
      <div className="ws-panel ws-pad">
        <h2>Привязка к карте · {plan.name}</h2>
        <form onSubmit={(e) => void inspect(e)}>
          <fieldset disabled={busy}>
            <label>
              Способ привязки
              <select
                value={mode}
                onChange={(e) => {
                  reset();
                  setMode(e.target.value as Mode);
                  setActive(0);
                }}
              >
                <option value="pair">Два ориентира</option>
                <option value="width">Один ориентир и известная ширина</option>
                {original && <option value="unlink">Снять привязку</option>}
              </select>
            </label>
            {mode !== "unlink" ? (
              <>
                <p>
                  {mode === "pair"
                    ? "Отметьте одно и то же место на плане и карте, затем повторите для второго ориентира. Чем дальше ориентиры друг от друга, тем точнее поворот."
                    : "Отметьте одно и то же место на плане и карте. Укажите реальную ширину всего изображения с полями и азимут направления вверх по плану."}
                </p>
                {mode === "pair" && (
                  <div className="ws-map-actions">
                    {(["A", "B"] as const).map((l, i) => (
                      <Button
                        key={l}
                        aria-pressed={active === i}
                        onClick={() => setActive(i as 0 | 1)}
                      >
                        Ориентир {l}
                        {Object.values(anchors[i]!).every(Boolean) ? " ✓" : ""}
                      </Button>
                    ))}
                  </div>
                )}
                <div className="ws-reference-pair">
                  <div>
                    <div className="ws-plan-heading">
                      <h3>План · ориентир {active ? "B" : "A"}</h3>
                      <PlanZoom
                        label="Масштаб изображения"
                        levels={[1, 2, 4]}
                        value={scale}
                        onChange={setScale}
                      />
                    </div>
                    <div className="ws-plan-scroll">
                      <div
                        className="ws-plan"
                        style={{ width: scale * 100 + "%" }}
                      >
                        <img
                          src={plan.image_url}
                          alt={plan.name}
                          onClick={(e) => {
                            const r = e.currentTarget.getBoundingClientRect();
                            changeAnchor(
                              {
                                x: fixed((e.clientX - r.left) / r.width),
                                y: fixed((e.clientY - r.top) / r.height),
                              },
                              true,
                            );
                          }}
                        />
                        {shown.map(
                          (a, i) =>
                            a.x &&
                            a.y && (
                              <span
                                key={i}
                                className="ws-anchor ws-map-badge"
                                style={{
                                  left: Number(a.x) * 100 + "%",
                                  top: Number(a.y) * 100 + "%",
                                }}
                              >
                                {i ? "B" : "A"}
                              </span>
                            ),
                        )}
                      </div>
                    </div>
                  </div>
                  <div>
                    <h3>Карта · ориентир {active ? "B" : "A"}</h3>
                    {scene.regions.length > 1 && (
                      <label>
                        Регион
                        <select
                          value={region?.id}
                          onChange={(e) => setRegion(e.target.value)}
                        >
                          {scene.regions.map((r) => (
                            <option value={r.id} key={r.id}>
                              {r.name}
                            </option>
                          ))}
                        </select>
                      </label>
                    )}
                    {region ? (
                      <RegionMap
                        key={region.revision}
                        region={region}
                        chosen=""
                        markers={[]}
                        points={[]}
                        view={null}
                        select={() => {}}
                        move={() => {}}
                        pick={(lng, lat) =>
                          changeAnchor(
                            { lng: fixed(lng), lat: fixed(lat) },
                            true,
                          )
                        }
                        labels={labels}
                        overlay={
                          preview?.footprint
                            ? {
                                image: plan.image_url,
                                corners: preview.footprint.corners,
                                opacity,
                              }
                            : undefined
                        }
                        fitOverlay
                      />
                    ) : (
                      <p>
                        Для выбора на карте установите регион. Координаты можно
                        ввести вручную.
                      </p>
                    )}
                  </div>
                </div>
                <details open={region ? undefined : true}>
                  <summary>Координаты ориентира {active ? "B" : "A"}</summary>
                  <div className="ws-reference-fields">
                    {(["x", "y", "lat", "lng"] as const).map((k) => (
                      <label key={k}>
                        {axisLabels[k]}
                        <input
                          type="number"
                          step="any"
                          min={
                            k === "lat" ? -85.051129 : k === "lng" ? -180 : 0
                          }
                          max={k === "lat" ? 85.051129 : k === "lng" ? 180 : 1}
                          value={anchors[active][k]}
                          onChange={(e) =>
                            changeAnchor({ [k]: e.target.value })
                          }
                        />
                      </label>
                    ))}
                  </div>
                </details>
                {mode === "width" && (
                  <div className="ws-reference-fields">
                    <label>
                      Ширина изображения, м
                      <input
                        type="number"
                        required
                        min=".25"
                        step="any"
                        value={width}
                        onChange={(e) => {
                          reset();
                          setWidth(e.target.value);
                        }}
                      />
                    </label>
                    <label>
                      Азимут верха плана, °
                      <input
                        type="number"
                        required
                        min="0"
                        max="360"
                        step="any"
                        value={bearing}
                        onChange={(e) => {
                          reset();
                          setBearing(e.target.value);
                        }}
                      />
                    </label>
                    <div className="ws-map-actions">
                      {["Север", "Восток", "Юг", "Запад"].map((label, i) => (
                        <Button
                          key={label}
                          aria-pressed={Number(bearing) === i * 90}
                          onClick={() => {
                            reset();
                            setBearing(String(i * 90));
                          }}
                        >
                          {label}
                        </Button>
                      ))}
                    </div>
                  </div>
                )}
                <label>
                  Здание для группировки этажей (по желанию)
                  <input
                    list="map-buildings"
                    maxLength={128}
                    value={building}
                    onChange={(e) => {
                      reset();
                      setBuilding(e.target.value);
                    }}
                  />
                </label>
                <datalist id="map-buildings">
                  {[
                    ...new Set(
                      scene.plans.map((p) => p.building).filter(Boolean),
                    ),
                  ].map((n) => (
                    <option key={n} value={n} />
                  ))}
                </datalist>
              </>
            ) : (
              <p>
                План и камеры останутся доступны на плане. Для камер с
                сохранённой ручной геопозицией восстановится её отображение.
                Остальные точки этого плана исчезнут с геокарты, если план
                задавал их положение.
              </p>
            )}
            <Button type="submit">Предпросмотр</Button>
          </fieldset>
        </form>
        {preview && (
          <>
            <h3>Проверка привязки</h3>
            {preview.footprint && (
              <>
                <p>
                  Ширина {preview.footprint.width_m.toFixed(1)} м · азимут верха{" "}
                  {preview.footprint.bearing.toFixed(1)}° · точек на плане{" "}
                  {preview.cameras.length}
                </p>
                {region && (
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
              </>
            )}
            {conflicts.map((c) => (
              <label key={c.point_id}>
                {points.find((p) => p.id === c.point_id)?.name ?? c.point_id} ·
                расхождение {c.distance_m?.toFixed(1)} м
                <select
                  value={choices[c.point_id] ?? ""}
                  onChange={(e) =>
                    setChoices({
                      ...choices,
                      [c.point_id]: e.target.value as "keep" | "plan",
                    })
                  }
                >
                  <option value="" disabled>
                    Выберите положение
                  </option>
                  <option value="keep">
                    Использовать{" "}
                    {c.existing_plan_id
                      ? "положение с другого плана"
                      : "ручное положение"}
                  </option>
                  <option value="plan">Использовать этот план</option>
                </select>
              </label>
            ))}
            <Button
              variant="primary"
              disabled={busy || conflicts.some((c) => !choices[c.point_id])}
              onClick={() => void save()}
            >
              {mode === "unlink" ? "Снять привязку" : "Сохранить привязку"}
            </Button>
          </>
        )}
        <Button disabled={busy} onClick={() => close(false)}>
          Вернуться к плану без сохранения
        </Button>
        <p role="status">{notice}</p>
      </div>
    </section>
  );
}
