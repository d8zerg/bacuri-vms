import { createPortal } from "react-dom";
import { CameraMarker } from "./CameraMarker";
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
function RegionPin({
  map,
  marker,
  point,
  chosen,
  select,
}: {
  map: LibreMap;
  marker: Placement;
  point: Point;
  chosen: boolean;
  select: (id: string) => void;
}) {
  const [el] = useState(() => document.createElement("div"));
  useEffect(() => {
    const pin = new Marker({ element: el })
      .setLngLat([marker.x, marker.y])
      .addTo(map);
    return () => {
      pin.remove();
    };
  }, [map, el, marker.x, marker.y]);
  return createPortal(
    <CameraMarker
      marker={marker}
      name={point.name}
      selected={chosen}
      choose={() => select(point.id)}
    />,
    el,
  );
}
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
    instance.on("pitch", () =>
      el.style.setProperty(
        "--map-tilt",
        String(Math.cos((instance.getPitch() * Math.PI) / 180)),
      ),
    );
    instance.on("rotate", () =>
      el.style.setProperty("--map-bearing", instance.getBearing() + "deg"),
    );
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
  return (
    <>
      <div
        className="ws-region"
        ref={host}
        role="region"
        aria-label="Карта региона"
      />
      {map &&
        props.markers.map((marker) => {
          const point = props.points.find((p) => p.id === marker.point_id);
          return (
            point && (
              <RegionPin
                key={point.id}
                map={map}
                marker={marker}
                point={point}
                chosen={props.chosen === point.id}
                select={props.select}
              />
            )
          );
        })}
      {failed && (
        <p role="status">
          Не удалось отобразить карту региона. Координаты точек доступны в
          списке.
        </p>
      )}
    </>
  );
}
