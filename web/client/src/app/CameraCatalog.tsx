import { useEffect, useState } from "react";
import type { Camera, Point, Services } from "../contracts/workspace";
import { FilterPanel, matchesQuery } from "../ui-kit/FilterPanel";
import { groupChannels, channelName } from "./cameraChannels";
import "./camera-batch.css";
function stateOf(c: Camera) {
  return c.state === "rejected"
    ? "rejected"
    : c.state === "applied" && c.applied_revision === c.desired_revision
      ? "applied"
      : "pending";
}
export default function CameraCatalog({
  services,
  site,
  points,
}: {
  services: Services;
  site: string;
  points: Point[];
}) {
  const [cameras, setCameras] = useState<Camera[] | null>(null);
  const [loading, setLoading] = useState(true);
  const [failed, setFailed] = useState(false),
    [attempt, refresh] = useState(0);
  const [query, setQuery] = useState(""),
    [statusFilter, setStatusFilter] = useState("");
  useEffect(() => {
    const ctl = new AbortController();
    setFailed(false);
    setLoading(true);
    void services
      .cameras(site, AbortSignal.any([ctl.signal, AbortSignal.timeout(5000)]))
      .then((rows) => {
        if (!ctl.signal.aborted) setCameras(rows);
      })
      .catch(() => {
        if (!ctl.signal.aborted) {
          setCameras(null);
          setFailed(true);
        }
      })
      .finally(() => {
        if (!ctl.signal.aborted) setLoading(false);
      });
    return () => ctl.abort();
  }, [site, services, attempt]);
  useEffect(() => {
    if (cameras && !performance.getEntriesByName("workspace-action").length)
      performance.mark("workspace-action");
  }, [cameras]);
  const [connection, setConnection] = useState("");
  const locations = new Map(points.map((p) => [p.id, p]));
  const connections = {
    online: "На связи",
    offline: "Нет связи",
    unknown: "Нет данных",
  } as const;
  const groups = groupChannels(cameras ?? [], (c) => ({
    device: { ...c, model: c.model || c.name },
    key: c.endpoint_id || c.id,
    token: c.channel_token || "",
  }));
  const visible = groups
    .map((g) => ({
      ...g,
      items: g.items.filter(
        (c) =>
          (!statusFilter || statusFilter === stateOf(c)) &&
          (!connection ||
            connection ===
              (locations.get(c.point_id)?.connection ?? "unknown")) &&
          matchesQuery(
            query,
            g.title,
            c.name,
            channelName(c.name, c.model, c.channel_token),
            locations.get(c.point_id)?.name,
            c.model,
            c.manufacturer,
            c.serial,
            c.firmware,
            c.channel_token,
            c.endpoint_id,
          ),
      ),
    }))
    .filter((g) => g.items.length);
  return (
    <section
      className="ws-panel ws-camera-batch"
      aria-label="Подключённые устройства"
    >
      <FilterPanel
        label="Фильтры каталога"
        refresh={{
          label: "Обновить каталог",
          run: () => refresh((n) => n + 1),
          busy: loading,
        }}
        queryLabel="Устройство, канал или точка"
        query={query}
        onQuery={setQuery}
        filtered={!!statusFilter || !!connection}
        onReset={() => {
          setQuery("");
          setStatusFilter("");
          setConnection("");
        }}
      >
        <label>
          Связь с камерой
          <select
            value={connection}
            onChange={(e) => setConnection(e.target.value)}
          >
            <option value="">Все состояния</option>
            {Object.entries(connections).map(([k, title]) => (
              <option key={k} value={k}>
                {title}
              </option>
            ))}
          </select>
        </label>
        <label>
          Конфигурация канала
          <select
            value={statusFilter}
            onChange={(e) => setStatusFilter(e.target.value)}
          >
            <option value="">Все состояния</option>
            <option value="applied">Применено</option>
            <option value="pending">Ожидает рекордера</option>
            <option value="rejected">Отклонено</option>
          </select>
        </label>
      </FilterPanel>
      {!!visible.length && (
        <p className="ws-result-count">
          Устройств: {visible.length}
          {visible.length < groups.length && ` из ${groups.length}`}
        </p>
      )}
      {!visible.length && (
        <p role="status">
          {failed
            ? "Не удалось загрузить каталог."
            : !cameras
              ? "Загрузка устройств…"
              : !cameras.length
                ? "Подключённых устройств пока нет."
                : "Устройства по заданным условиям не найдены."}
        </p>
      )}
      <div className="ws-batch-results">
        {visible.map(({ key, title, items }) => {
          const device = items[0]!;
          return (
            <section className="ws-batch-device" key={key}>
              <header>
                <h2>{title}</h2>
                {device.firmware && <span>Прошивка {device.firmware}</span>}
              </header>
              {items.map((c) => {
                const name = channelName(c.name, c.model, c.channel_token);
                const point = locations.get(c.point_id)?.name;
                const status =
                  stateOf(c) === "rejected"
                    ? "Отклонено рекордером"
                    : stateOf(c) === "applied"
                      ? "Применена версия " + c.applied_revision
                      : "Ожидает рекордера";
                return (
                  <details className="ws-batch-channel" key={c.id}>
                    <summary>
                      <strong>
                        {name}
                        {point &&
                          point !== c.name &&
                          point !== name &&
                          " · " + point}
                      </strong>
                      <span>{status}</span>
                    </summary>
                    <div className="ws-channel-detail">
                      <dl>
                        <dt>Связь с камерой</dt>
                        <dd>
                          {
                            connections[
                              locations.get(c.point_id)?.connection ?? "unknown"
                            ]
                          }
                        </dd>
                        <dt>Точка</dt>
                        <dd>
                          {point || c.name} · {c.point_id}
                        </dd>
                        {device.serial && (
                          <>
                            <dt>Серийный номер</dt>
                            <dd>{device.serial}</dd>
                          </>
                        )}
                        {c.channel_token && (
                          <>
                            <dt>Канал ONVIF</dt>
                            <dd>{c.channel_token}</dd>
                          </>
                        )}
                        <dt>Версия рекордера</dt>
                        <dd>{c.applied_revision ?? "Ожидание"}</dd>
                      </dl>
                    </div>
                  </details>
                );
              })}
            </section>
          );
        })}
      </div>
    </section>
  );
}
