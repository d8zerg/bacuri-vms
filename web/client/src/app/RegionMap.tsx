import { createPortal } from "react-dom";
import { CameraMarker } from "./CameraMarker";
import { useEffect, useRef, useState } from "react";
import {
  Map as LibreMap,
  Marker,
  NavigationControl,
  LngLatBounds,
} from "maplibre-gl";
import "maplibre-gl/dist/maplibre-gl.css";
import type { MapScene, Point, Placement } from "../contracts/workspace";
import { useT } from "../i18n";

type View = { lng: number; lat: number; zoom: number };
type Overlay = { image: string; corners: number[][]; opacity: number };
export type MapLabel = {
  id: string;
  text: string;
  lng: number;
  lat: number;
  activate?: () => void;
};
function MapBadge({ map, label }: { map: LibreMap; label: MapLabel }) {
  const [element] = useState(() => document.createElement("div"));
  useEffect(() => {
    const marker = new Marker({ element })
      .setLngLat([label.lng, label.lat])
      .addTo(map);
    return () => {
      marker.remove();
    };
  }, [map, element, label.lng, label.lat]);
  return createPortal(
    label.activate ? (
      <button
        type="button"
        className="ws-map-badge"
        onClick={(e) => {
          e.stopPropagation();
          label.activate?.();
        }}
      >
        {label.text}
      </button>
    ) : (
      <span className="ws-map-badge">{label.text}</span>
    ),
    element,
  );
}
function ImageOverlay({
  map,
  overlay,
  fit,
}: {
  map: LibreMap;
  overlay: Overlay;
  fit: boolean;
}) {
  const corners = JSON.stringify(overlay.corners);
  const { image, opacity } = overlay;
  const latestOpacity = useRef(opacity);
  useEffect(() => {
    latestOpacity.current = opacity;
    if (map.getLayer("floor-image"))
      map.setPaintProperty("floor-image", "raster-opacity", opacity);
  }, [map, opacity]);
  useEffect(() => {
    function add() {
      type Pair = [number, number];
      const coordinates = JSON.parse(corners) as [Pair, Pair, Pair, Pair];
      map.addSource("floor-image", { type: "image", url: image, coordinates });
      map.addLayer({
        id: "floor-image",
        type: "raster",
        source: "floor-image",
        paint: {
          "raster-opacity": latestOpacity.current,
          "raster-fade-duration": 0,
        },
      });
      if (fit) {
        const bounds = new LngLatBounds();
        for (const corner of coordinates) bounds.extend(corner);
        map.fitBounds(bounds, { padding: 36, maxZoom: 21, duration: 0 });
      }
    }
    add();
    return () => {
      if (!map.style) return;
      if (map.getLayer("floor-image")) map.removeLayer("floor-image");
      if (map.getSource("floor-image")) map.removeSource("floor-image");
    };
  }, [map, corners, image, fit]);
  return null;
}
type Props = {
  overlay?: Overlay | undefined;
  fitOverlay?: boolean;
  labels?: MapLabel[];
  region: NonNullable<MapScene["region"]>;
  markers: Placement[];
  newAlerts?: ReadonlyMap<string, number>;
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
  newAlerts,
  select,
}: {
  map: LibreMap;
  marker: Placement;
  point: Point;
  chosen: boolean;
  newAlerts: number;
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
      newAlerts={newAlerts}
      choose={() => select(point.id)}
    />,
    el,
  );
}
export default function RegionMap(props: Props) {
  const t = useT();
  const [fitInitially] = useState(!!props.fitOverlay);
  const host = useRef<HTMLDivElement>(null),
    latest = useRef(props);
  const [map, setMap] = useState<LibreMap | null>(null),
    [loadedMap, setLoadedMap] = useState<LibreMap | null>(null),
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
          "NavigationControl.ZoomIn": t("map.zoomIn"),
          "NavigationControl.ZoomOut": t("map.zoomOut"),
          "Map.Title": t("map.region"),
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
    instance.on("load", () => {
      setLoadedMap(instance);
      setFailed(false);
    });
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
  }, [style, t]);
  return (
    <>
      <div
        className="ws-region"
        ref={host}
        role="region"
        aria-label={t("map.region")}
      />
      {map && map === loadedMap && props.overlay && (
        <ImageOverlay
          key={props.overlay.image}
          map={map}
          overlay={props.overlay}
          fit={fitInitially}
        />
      )}
      {map &&
        props.labels?.map((label) => (
          <MapBadge key={label.id} map={map} label={label} />
        ))}
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
                newAlerts={props.newAlerts?.get(point.id) ?? 0}
                select={props.select}
              />
            )
          );
        })}
      {failed && <p role="status">{t("map.regionFailed")}</p>}
    </>
  );
}
