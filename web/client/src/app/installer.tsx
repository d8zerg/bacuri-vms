import { lazy, Suspense, useRef, useState } from "react";
import { useSearchParams } from "react-router";
import type { CameraDraft, Point, Services } from "../contracts/workspace";
import { SubsectionMenu } from "./SubsectionMenu";
const VideoRules = lazy(() => import("./VideoRules"));
const CameraCatalog = lazy(() => import("./CameraCatalog"));
const Connection = lazy(() => import("./CameraConnection"));
const CameraSelection = lazy(() => import("./CameraSelection"));
const RecordingPolicies = lazy(() => import("./RecordingPolicies"));
const CameraBatch = lazy(() => import("./CameraBatch"));
const empty = { name: "", main_url: "", secondary_url: "" };
export function cameraSetup(requested: string | null) {
  return requested === "manual" ||
    requested === "onvif" ||
    requested === "rules" ||
    requested === "recording" ||
    requested === "batch"
    ? requested
    : null;
}
export function Installer({
  services,
  site,
  points,
  connectionNotice,
}: {
  services: Services;
  site: string;
  points: Point[] | null;
  connectionNotice: string;
}) {
  const [params, setParams] = useSearchParams();
  const method = cameraSetup(params.get("setup"));
  const [initial, setInitial] = useState<CameraDraft>(empty);
  const [version, changeVersion] = useState(0);
  const focusDraft = useRef(false);
  function select(next: typeof method) {
    if (method === next) return;
    const request = new URLSearchParams(params);
    if (next) request.set("setup", next);
    else request.delete("setup");
    void setParams(request);
  }
  return (
    <>
      <SubsectionMenu
        label="Подразделы камер"
        items={[
          {
            label: "Каталог устройств",
            short: "Каталог",
            name: "Каталог подключённых устройств",
            icon: "cctv",
            active: !method,
            select: () => select(null),
          },
          {
            label: "Запись и хранение",
            short: "Запись",
            icon: "history",
            active: method === "recording",
            select: () => select("recording"),
          },
          {
            label: "Видеоправила",
            short: "Правила",
            icon: "scan-line",
            active: method === "rules",
            select: () => select("rules"),
          },
          {
            label: "Подключение",
            short: "Добавить",
            icon: "link-2",
            active: !!method && method !== "recording" && method !== "rules",
            select: () => {
              if (!method || method === "rules" || method === "recording")
                select("manual");
            },
          },
        ]}
      />
      {method && method !== "recording" && method !== "rules" && (
        <SubsectionMenu
          nested
          label="Способы подключения"
          items={[
            {
              label: "Вручную",
              name: "Единичное подключение",
              icon: "link-2",
              active: method === "manual",
              select: () => select("manual"),
            },
            {
              label: "Поиск ONVIF",
              name: "Поиск ONVIF и замена камеры",
              icon: "scan-line",
              active: method === "onvif",
              disabled: !points,
              select: () => select("onvif"),
            },
            {
              label: "Массовое",
              name: "Массовое подключение",
              icon: "layers",
              active: method === "batch",
              disabled: !points && method !== "batch",
              select: () => select("batch"),
            },
          ]}
        />
      )}
      <Suspense fallback={<p role="status">Загрузка…</p>}>
        {method === "rules" && (
          <VideoRules services={services} site={site} points={points ?? []} />
        )}
        {method === "recording" && (
          <RecordingPolicies
            key={site}
            services={services}
            site={site}
            points={points ?? []}
          />
        )}
        {!method &&
          (points ? (
            <CameraCatalog services={services} site={site} points={points} />
          ) : (
            <p role="status">{connectionNotice}</p>
          ))}
        {method === "batch" && (
          <CameraBatch site={site} services={services} online={!!points} />
        )}
        {method === "onvif" &&
          (points ? (
            <CameraSelection
              site={site}
              services={services}
              onChoose={(draft) => {
                focusDraft.current = true;
                setInitial(draft);
                changeVersion((n) => n + 1);
                select("manual");
              }}
            />
          ) : (
            <p role="status">{connectionNotice}</p>
          ))}
        {method && method !== "recording" && method !== "rules" && (
          <div
            hidden={method !== "manual"}
            ref={(node) => {
              if (node && method === "manual" && focusDraft.current) {
                node.querySelector("input")?.focus();
                focusDraft.current = false;
              }
            }}
          >
            <Connection
              key={version}
              services={services}
              site={site}
              online={!!points}
              connectionNotice={connectionNotice}
              initial={initial}
            />
          </div>
        )}
      </Suspense>
    </>
  );
}
