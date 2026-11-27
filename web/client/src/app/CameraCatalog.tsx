import "../i18n/camera";
import { useEffect, useRef, useState } from "react";
import { IconButton } from "../ui-kit/FormControls";
import type {
  Camera,
  EdgeArchive,
  Interval,
  Point,
  Services,
} from "../contracts/workspace";
import { FilterPanel, matchesQuery } from "../ui-kit/FilterPanel";
import { groupChannels, channelName } from "./cameraChannels";
import "./camera-batch.css";
import { dateTimeFormat, formatNumber, useT } from "../i18n";
function DeviceRecordings({
  services,
  site,
  device,
}: {
  services: Services;
  site: string;
  device: string;
}) {
  const t = useT();
  const [attempt, check] = useState(0);
  const [result, setResult] = useState<EdgeArchive | null>(null);
  const [loading, setLoading] = useState(false);
  const [failure, setFailure] = useState(false);
  useEffect(() => {
    if (!attempt) return;
    const ctl = new AbortController();
    setLoading(true);
    setFailure(false);
    void services
      .edgeArchive(
        site,
        device,
        AbortSignal.any([ctl.signal, AbortSignal.timeout(15000)]),
      )
      .then((value) => {
        if (!ctl.signal.aborted) setResult(value);
      })
      .catch(() => {
        if (!ctl.signal.aborted) setFailure(true);
      })
      .finally(() => {
        if (!ctl.signal.aborted) setLoading(false);
      });
    return () => ctl.abort();
  }, [services, site, device, attempt]);
  const recovering = useRef<AbortController | null>(null);
  const [progress, setProgress] = useState("");
  const [busy, setBusy] = useState(false);
  useEffect(
    () => () => {
      recovering.current?.abort();
      recovering.current = null;
    },
    [],
  );
  async function restore(id: string, range: Interval) {
    if (!result || recovering.current) return;
    const ctl = new AbortController();
    recovering.current = ctl;
    setBusy(true);
    let duration = 0;
    try {
      let start = range.start;
      while (Date.parse(start) < Date.parse(range.end) && !ctl.signal.aborted) {
        const end = new Date(
          Math.min(Date.parse(range.end), Date.parse(start) + 3599000),
        ).toISOString();
        const reply = await services.recoverGap(
          site,
          device,
          id,
          result.binding_revision,
          { start, end },
          ctl.signal,
        );
        duration += reply.duration_us;
        setProgress(
          t("catalog.recovered", {
            value: formatNumber(duration / 1000000, {
              maximumFractionDigits: 1,
            }),
          }),
        );
        if (reply.outcome === "quota" || reply.outcome === "timeout") break;
        const next = reply.next ?? end;
        if (Date.parse(next) <= Date.parse(start)) break;
        start = next;
      }
    } catch {
      if (!ctl.signal.aborted) setProgress(t("catalog.recoverInterrupted"));
    } finally {
      if (recovering.current === ctl) {
        recovering.current = null;
        setBusy(false);
        check((n) => n + 1);
      }
    }
  }
  const labels = {
    Video: t("catalog.video"),
    Audio: t("audio.label"),
    Metadata: t("catalog.metadata"),
  };
  const date = (utc: string) =>
    dateTimeFormat({ dateStyle: "short", timeStyle: "medium" }).format(
      new Date(utc),
    );
  return (
    <div>
      <div className="ws-edge-heading">
        <strong>{t("catalog.deviceArchive")}</strong>
        <IconButton
          icon={attempt ? "rotate-cw" : "search"}
          label={t("catalog.checkArchive")}
          disabled={loading || busy}
          onClick={() => check((n) => n + 1)}
        />
      </div>
      <div role="status">
        {loading
          ? t("catalog.polling")
          : failure
            ? t("catalog.readFailed")
            : !result
              ? null
              : !result.supported
                ? t("catalog.unsupported")
                : !result.recordings.length
                  ? t("catalog.noRecordings")
                  : result.recordings.map((record, i) => (
                      <div key={record.id}>
                        {(record.name || result.recordings.length > 1) && (
                          <strong>
                            {record.name ||
                              t("catalog.recording", { number: i + 1 })}
                          </strong>
                        )}
                        <dl>
                          {record.tracks.map((track, index) => (
                            <div key={index}>
                              <dt>{labels[track.kind]}</dt>
                              <dd>
                                {track.bounds
                                  ? date(track.bounds.start) +
                                    " - " +
                                    date(track.bounds.end)
                                  : t("catalog.noInterval")}
                              </dd>
                            </div>
                          ))}
                        </dl>
                        {!record.tracks.length && (
                          <p>{t("catalog.noTracks")}</p>
                        )}
                        {result.gaps
                          .filter((gap) =>
                            record.tracks.some(
                              (track) =>
                                track.kind === "Video" &&
                                track.bounds &&
                                Date.parse(track.bounds.start) <
                                  Date.parse(gap.end) &&
                                Date.parse(track.bounds.end) >
                                  Date.parse(gap.start),
                            ),
                          )
                          .map((gap) => (
                            <div className="ws-edge-heading" key={gap.start}>
                              <span>
                                {t("catalog.gap", {
                                  start: date(gap.start),
                                  end: date(gap.end),
                                })}
                              </span>
                              <IconButton
                                icon="file-down"
                                label={t("catalog.recoverGap", {
                                  start: date(gap.start),
                                })}
                                disabled={busy}
                                onClick={() => void restore(record.id, gap)}
                              />
                            </div>
                          ))}
                      </div>
                    ))}
        {progress && <p>{progress}</p>}
      </div>
    </div>
  );
}
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
  const t = useT();
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
    online: t("catalog.online"),
    offline: t("catalog.offline"),
    unknown: t("catalog.unknown"),
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
      aria-label={t("catalog.label")}
    >
      <FilterPanel
        label={t("catalog.filters")}
        refresh={{
          label: t("catalog.refresh"),
          run: () => refresh((n) => n + 1),
          busy: loading,
        }}
        queryLabel={t("batch.channelQuery")}
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
          {t("catalog.connection")}
          <select
            value={connection}
            onChange={(e) => setConnection(e.target.value)}
          >
            <option value="">{t("batch.allStates")}</option>
            {Object.entries(connections).map(([k, title]) => (
              <option key={k} value={k}>
                {title}
              </option>
            ))}
          </select>
        </label>
        <label>
          {t("catalog.configuration")}
          <select
            value={statusFilter}
            onChange={(e) => setStatusFilter(e.target.value)}
          >
            <option value="">{t("batch.allStates")}</option>
            <option value="applied">{t("catalog.applied")}</option>
            <option value="pending">{t("batch.recorderWaiting")}</option>
            <option value="rejected">{t("catalog.rejected")}</option>
          </select>
        </label>
      </FilterPanel>
      {!!visible.length && (
        <p className="ws-result-count">
          {visible.length < groups.length
            ? t("catalog.countOf", {
                count: visible.length,
                total: groups.length,
              })
            : t("catalog.count", { count: visible.length })}
        </p>
      )}
      {!visible.length && (
        <p role="status">
          {failed
            ? t("catalog.loadFailed")
            : !cameras
              ? t("catalog.loading")
              : !cameras.length
                ? t("catalog.empty")
                : t("catalog.none")}
        </p>
      )}
      <div className="ws-batch-results">
        {visible.map(({ key, title, items }) => {
          const device = items[0]!;
          return (
            <section className="ws-batch-device" key={key}>
              <header>
                <h2>{title}</h2>
                {device.firmware && (
                  <span>
                    {t("catalog.firmware", { version: device.firmware })}
                  </span>
                )}
              </header>
              {items.map((c) => {
                const name = channelName(c.name, c.model, c.channel_token);
                const point = locations.get(c.point_id)?.name;
                const status =
                  stateOf(c) === "rejected"
                    ? t("catalog.rejectedByRecorder")
                    : stateOf(c) === "applied"
                      ? t("catalog.appliedRevision", {
                          revision: c.applied_revision ?? "",
                        })
                      : t("batch.recorderWaiting");
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
                        <dt>{t("catalog.connection")}</dt>
                        <dd>
                          {
                            connections[
                              locations.get(c.point_id)?.connection ?? "unknown"
                            ]
                          }
                        </dd>
                        <dt>{t("batch.point")}</dt>
                        <dd>
                          {point || c.name} · {c.point_id}
                        </dd>
                        {device.serial && (
                          <>
                            <dt>{t("batch.serial")}</dt>
                            <dd>{device.serial}</dd>
                          </>
                        )}
                        {c.channel_token && (
                          <>
                            <dt>{t("batch.onvifChannel")}</dt>
                            <dd>{c.channel_token}</dd>
                          </>
                        )}
                        <dt>{t("batch.recorderRevision")}</dt>
                        <dd>{c.applied_revision ?? t("catalog.waiting")}</dd>
                      </dl>
                      <DeviceRecordings
                        key={site + c.id}
                        services={services}
                        site={site}
                        device={c.id}
                      />
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
