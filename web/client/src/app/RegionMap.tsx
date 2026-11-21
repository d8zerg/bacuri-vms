import { useEffect, useRef, useState } from "react";
import { Map as LibreMap, Marker, NavigationControl } from "maplibre-gl";
import "maplibre-gl/dist/maplibre-gl.css";
import type { MapScene, Point, Placement } from "../contracts/workspace";

type View = { lng: number; lat: number; zoom: number };
type Props = {
  region: NonNullable<MapScene["region"]>;
  markers: Placement[];
  points: Point[];
  chosen: string;
  view: View | null;
  select: (id: string) => void;
  pick: (x: number, y: number) => void;
  move: (view: View) => void;
};
export default function RegionMap(props: Props) {
  const host = useRef<HTMLDivElement>(null),
    latest = useRef(props);
  const [map, setMap] = useState<LibreMap | null>(null),
    [failed, setFailed] = useState(false);
  useEffect(() => {
    latest.current = props;
  }, [props]);
  const style = props.region.style_url;
  useEffect(() => {
    const el = host.current;
    if (!el) return;
    const { view, region } = latest.current;
    const [west, south, east, north] = region.bounds;
    let instance: LibreMap;
    try {
      instance = new LibreMap({
        container: el,
        style,
        attributionControl: false,
        locale: {
          "NavigationControl.ZoomIn": "Увеличить масштаб",
          "NavigationControl.ZoomOut": "Уменьшить масштаб",
          "Map.Title": "Карта региона",
        },
        renderWorldCopies: false,
        ...(view
          ? {
              center: [view.lng, view.lat] as [number, number],
              zoom: view.zoom,
            }
          : {
              bounds: [west!, south!, east!, north!] as [
                number,
                number,
                number,
                number,
              ],
              fitBoundsOptions: { padding: 20, maxZoom: region.max_zoom },
            }),
      });
    } catch {
      setFailed(true);
      return;
    }
    instance.addControl(new NavigationControl({ showCompass: false }));
    instance.on("load", () => setFailed(false));
    instance.on("error", () => setFailed(true));
    instance.on("click", (event) =>
      latest.current.pick(event.lngLat.lng, event.lngLat.lat),
    );
    instance.on("moveend", () => {
      const { lng, lat } = instance.getCenter();
      latest.current.move({ lng, lat, zoom: instance.getZoom() });
    });
    setMap(instance);
    return () => instance.remove();
  }, [style]);
  useEffect(() => {
    if (!map) return;
    const pins: Marker[] = [];
    for (const marker of props.markers) {
      const point = props.points.find((p) => p.id === marker.point_id);
      if (!point) continue;
      const el = document.createElement("button");
      el.type = "button";
      el.className = "ws-pin";
      el.textContent = point.name;
      el.setAttribute("aria-pressed", String(props.chosen === point.id));
      el.onclick = (e) => {
        e.stopPropagation();
        latest.current.select(point.id);
      };
      pins.push(
        new Marker({ element: el }).setLngLat([marker.x, marker.y]).addTo(map),
      );
    }
    return () => {
      for (const pin of pins) pin.remove();
    };
  }, [map, props.markers, props.points, props.chosen]);
  return (
    <>
      <div
        className="ws-region"
        ref={host}
        role="region"
        aria-label="Карта региона"
      />
      {failed && (
        <p role="status">
          Не удалось отобразить карту региона. Координаты точек доступны в
          списке.
        </p>
      )}
    </>
  );
}
