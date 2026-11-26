import "../i18n/extended";
import { useEffect, useRef, useState } from "react";
import { api, data, HttpFailure } from "../adapters/api";
import type {
  Camera,
  DiscoveredCamera,
  Point,
  Services,
} from "../contracts/workspace";
import type { components } from "../../../../build/contracts/api";
import { Button } from "../ui-kit/FormControls";
import {
  DiscoveredDeviceSelect,
  matchingDevices,
} from "./DiscoveredDeviceSelect";
import { cameraError } from "./cameraError";
import { CameraBatchResults } from "./CameraBatchResults";
import { BatchChannels, type Channel } from "./BatchChannels";
import "./camera-batch.css";
import { t as translate, useT } from "../i18n";

type Receipt = components["schemas"]["CameraBatchReceipt"];
type Template = components["schemas"]["CameraTemplate"];
type Draft = components["schemas"]["CameraBatchDraft"];
function errorText(error: unknown) {
  return (
    cameraError(error) ??
    (error instanceof HttpFailure
      ? translate("batch.requestError", { status: error.status })
      : translate("batch.noResponse"))
  );
}
export default function CameraBatch({
  site,
  services,
  online,
}: {
  site: string;
  services: Services;
  online: boolean;
}) {
  const t = useT();
  const key = "bacuri.camera-batch." + site;
  const [batch, setBatch] = useState(
    () => sessionStorage.getItem(key) ?? crypto.randomUUID(),
  );
  const [rows, setRows] = useState<Receipt[]>([]);
  const [recent, setRecent] = useState<
    { id: string; name: string; count: number }[]
  >([]);
  const [inventory, setInventory] = useState<Camera[]>([]);
  const [deviceQuery, setDeviceQuery] = useState("");
  const [devices, setDevices] = useState<DiscoveredCamera[]>([]);
  const [target, setTarget] = useState("");
  const [channels, setChannels] = useState<Channel[]>([]);
  const [points, setPoints] = useState<Point[]>([]);
  const [username, setUser] = useState(""),
    [password, setPass] = useState("");
  const [templates, setTemplates] = useState<Template[]>([]);
  const [selected, selectTemplate] = useState<Template>();
  const [templateName, setTemplateName] = useState("");
  const [mode, setMode] = useState<"high" | "low">("high"),
    [brightness, setBrightness] = useState("");
  const [busy, setBusy] = useState(false),
    [notice, tell] = useState("");
  const [editing, setEditing] = useState(false);
  const resultsHeading = useRef<HTMLHeadingElement>(null);
  const active = useRef<AbortController | null>(null);
  const batchPath = "/sites/{siteId}/camera-batches/{batchId}" as const;
  useEffect(() => {
    const ctl = new AbortController();
    sessionStorage.setItem(key, batch);
    void services
      .cameras(site, ctl.signal)
      .then((list) => {
        if (!ctl.signal.aborted) setInventory(list);
      })
      .catch(() => {
        if (!ctl.signal.aborted) tell(translate("batch.inventoryFailed"));
      });
    void services
      .points(site, ctl.signal)
      .then((all) => {
        if (!ctl.signal.aborted) setPoints(all);
      })
      .catch(() => {
        if (!ctl.signal.aborted) tell(translate("batch.pointsFailed"));
      });
    void api
      .GET("/sites/{siteId}/camera-batches", {
        params: { path: { siteId: site } },
        signal: ctl.signal,
      })
      .then(data)
      .then((list) => {
        if (!ctl.signal.aborted) setRecent(list);
      })
      .catch(() => {
        if (!ctl.signal.aborted) tell(translate("batch.listFailed"));
      });
    void api
      .GET("/sites/{siteId}/camera-templates", {
        params: { path: { siteId: site } },
        signal: ctl.signal,
      })
      .then(data)
      .then((list) => {
        if (!ctl.signal.aborted) setTemplates(list);
      })
      .catch(() => {
        if (!ctl.signal.aborted) tell(translate("batch.templatesFailed"));
      });
    void api
      .GET(batchPath, {
        params: { path: { siteId: site, batchId: batch } },
        signal: ctl.signal,
      })
      .then(data)
      .then((result) => {
        if (!ctl.signal.aborted) setRows(result);
      })
      .catch(() => {
        if (!ctl.signal.aborted) tell(translate("batch.protocolFailed"));
      });
    return () => {
      ctl.abort();
      active.current?.abort();
    };
  }, [key, site, batch, services]);
  async function run(work: (signal: AbortSignal) => Promise<void>) {
    if (active.current) return;
    const ctl = new AbortController();
    active.current = ctl;
    setBusy(true);
    tell("");
    try {
      await work(ctl.signal);
    } catch (e) {
      if (!ctl.signal.aborted) tell(errorText(e));
    } finally {
      if (!ctl.signal.aborted) {
        active.current = null;
        setBusy(false);
      }
    }
  }
  function update(row: Receipt) {
    setRows((prev) =>
      prev.some((r) => r.item_id === row.item_id)
        ? prev.map((r) => (r.item_id === row.item_id ? row : r))
        : [...prev, row],
    );
  }
  const listedDevices = matchingDevices(devices, deviceQuery);
  async function inspect(signal: AbortSignal) {
    const found = target
      ? listedDevices.filter((d) => d.selection === target)
      : listedDevices;
    const registered = await services.cameras(site, signal);
    const notices: string[] = [];
    for (const device of found) {
      try {
        const info = await services.inspect(
          site,
          device.selection,
          username,
          password,
          signal,
        );
        for (const channel of new Set(
          info.profiles.map((p) => p.channel ?? ""),
        )) {
          const profiles = info.profiles.filter(
            (p) => (p.channel ?? "") === channel,
          );
          if (profiles.length < 2) {
            notices.push(
              info.model + " · " + channel + " · " + t("batch.needsProfiles"),
            );
            continue;
          }
          const current = registered.find(
            (c) =>
              c.endpoint_id === info.endpoint_id &&
              (c.channel_token
                ? c.channel_token === channel
                : profiles.some(
                    (p) => p.url === c.main_url || p.url === c.secondary_url,
                  )),
          );
          const entry = {
            id: crypto.randomUUID(),
            info,
            channel,
            username,
            password,
            selected: !rows.some(
              (r) =>
                r.camera.endpoint_id === info.endpoint_id &&
                r.camera.channel_token === channel &&
                r.state !== "rolled_back" &&
                r.state !== "failed",
            ),
            bound: !!current,
            name:
              current?.name ??
              [info.model, channel].filter(Boolean).join(" · "),
            point: current?.point_id ?? "",
          };
          setChannels((prev) => [
            ...prev.filter(
              (c) =>
                c.info.endpoint_id !== info.endpoint_id ||
                c.channel !== channel,
            ),
            entry,
          ]);
        }
      } catch (e) {
        signal.throwIfAborted();
        notices.push(device.device_service + " · " + errorText(e));
      }
    }
    tell(notices.join(" / "));
    setPass("");
  }
  function chooseTemplate(id: string) {
    const saved = templates.find((t) => t.id === id);
    selectTemplate(saved);
    setTemplateName(saved?.name ?? "");
    setMode(saved?.record_quality ?? "high");
    setBrightness(saved?.brightness?.toString() ?? "");
  }
  const matchingTemplate =
    selected &&
    selected.name === templateName &&
    selected.record_quality === mode &&
    (selected.brightness?.toString() ?? "") === brightness;
  async function saveTemplate(signal: AbortSignal) {
    const { csrf_token } = await services.session(signal);
    const result = data(
      await api.PUT("/sites/{siteId}/camera-templates/{templateId}", {
        params: {
          path: {
            siteId: site,
            templateId: selected?.id ?? crypto.randomUUID(),
          },
          header: { "X-CSRF-Token": csrf_token },
        },
        body: {
          expected_revision: selected?.revision ?? "0",
          name: templateName,
          record_quality: mode,
          ...(brightness === "" ? {} : { brightness: Number(brightness) }),
        },
        signal,
      }),
    );
    signal.throwIfAborted();
    setTemplates((list) => [...list.filter((t) => t.id !== result.id), result]);
    selectTemplate(result);
    tell(t("batch.templateSaved"));
  }
  async function preview(signal: AbortSignal) {
    setEditing(true);
    const { csrf_token } = await services.session(signal);
    const errors: string[] = [];
    for (const ch of channels.filter((c) => c.selected)) {
      const body: Draft = {
        name: ch.name,
        username: ch.username,
        password: ch.password,
        selection: ch.info.selection,
        channel: ch.channel,
        ...(ch.point ? { point_id: ch.point } : {}),
        ...(matchingTemplate && selected
          ? { template_id: selected.id, template_revision: selected.revision }
          : {
              record_quality: mode,
              ...(brightness === "" ? {} : { brightness: Number(brightness) }),
            }),
      };
      try {
        const row = data(
          await api.PUT("/sites/{siteId}/camera-batches/{batchId}/{itemId}", {
            params: {
              path: { siteId: site, batchId: batch, itemId: ch.id },
              header: { "X-CSRF-Token": csrf_token },
            },
            body,
            signal,
          }),
        );
        signal.throwIfAborted();
        update(row);
        setChannels((prev) => prev.filter((c) => c.id !== ch.id));
      } catch (e) {
        signal.throwIfAborted();
        errors.push(ch.name + " · " + errorText(e));
      }
    }
    tell(errors.join(" / "));
    if (!errors.length) {
      setEditing(false);
      requestAnimationFrame(() => resultsHeading.current?.focus());
    }
  }
  async function change(
    action: "apply" | "rollback",
    items: Receipt[],
    signal: AbortSignal,
  ) {
    const { csrf_token } = await services.session(signal);
    for (const row of items) {
      const result = data(
        await api.POST(
          "/sites/{siteId}/camera-batches/{batchId}/{itemId}/{action}",
          {
            params: {
              path: {
                siteId: site,
                batchId: batch,
                itemId: row.item_id,
                action,
              },
              header: { "X-CSRF-Token": csrf_token },
            },
            signal,
          },
        ),
      );
      signal.throwIfAborted();
      update(result);
    }
  }
  async function refresh(signal: AbortSignal) {
    const result = data(
      await api.GET(batchPath, {
        params: { path: { siteId: site, batchId: batch } },
        signal,
      }),
    );
    signal.throwIfAborted();
    setRows(result);
    const [cameras, all] = await Promise.all([
      services.cameras(site, signal),
      services.points(site, signal),
    ]);
    signal.throwIfAborted();
    setInventory(cameras);
    setPoints(all);
  }
  const pending = rows.filter(
    (r) =>
      r.state === "prepared" ||
      r.state === "applying" ||
      (r.state === "failed" &&
        r.error !== "revision_conflict" &&
        r.error !== "camera_binding_conflict") ||
      (r.state === "saved" &&
        r.current?.state === "rejected" &&
        r.revision === r.current.desired_revision),
  );
  function edit(id: string, patch: Partial<Channel>) {
    setChannels((prev) =>
      prev.map((c) => (c.id === id ? { ...c, ...patch } : c)),
    );
  }
  return (
    <section
      className="ws-panel ws-camera-batch"
      aria-label={t("cameras.batch.name")}
    >
      {!!rows.length && (
        <>
          <div className="ws-batch-toolbar">
            <h2 ref={resultsHeading} tabIndex={-1}>
              {t("batch.channels")} <span>({rows.length})</span>
            </h2>
            <div className="ws-actions">
              {!!pending.length && (
                <Button
                  variant="primary"
                  disabled={busy || !online}
                  onClick={() =>
                    void run((signal) => change("apply", pending, signal))
                  }
                >
                  {pending.every((r) => r.state === "prepared")
                    ? t("batch.apply")
                    : t("batch.retry")}
                </Button>
              )}
            </div>
          </div>
          <CameraBatchResults
            refresh={{
              label: t("batch.refresh"),
              run: () => void run(refresh),
              disabled: busy || !online,
            }}
            rows={rows}
            disabled={busy || !online}
            rollback={(row) =>
              void run((signal) => change("rollback", [row], signal))
            }
          />
          <div className="ws-actions">
            <Button
              aria-expanded={editing}
              disabled={busy || !online}
              onClick={() => setEditing(!editing)}
            >
              {editing ? t("batch.collapse") : t("batch.add")}
            </Button>
            <Button
              disabled={busy}
              onClick={() => {
                setRows([]);
                setChannels([]);
                setDevices([]);
                setPass("");
                setEditing(false);
                tell("");
                setBatch(crypto.randomUUID());
              }}
            >
              {t("batch.new")}
            </Button>
          </div>
        </>
      )}

      {recent.some((r) => r.id !== batch) && (
        <label className="bk-field">
          {t("batch.protocols")}
          <select
            disabled={busy}
            value={batch}
            onChange={(e) => {
              setDevices([]);
              setChannels([]);
              setPass("");
              setRows([]);
              setEditing(false);
              setBatch(e.target.value);
            }}
          >
            {!recent.some((r) => r.id === batch) && (
              <option value={batch}>{t("batch.current")}</option>
            )}
            {recent.map((r) => (
              <option value={r.id} key={r.id}>
                {r.name} · {t("batch.count", { count: r.count })} ·{" "}
                {r.id.slice(0, 8)}
              </option>
            ))}
          </select>
        </label>
      )}
      {(!rows.length || editing) && (
        <fieldset
          disabled={busy || !online}
          className="bc-fields ws-camera-fields"
        >
          <details open={!channels.length}>
            <summary>{t("batch.discovery")}</summary>
            <Button
              onClick={() =>
                void run(async (signal) => {
                  const [found, cameras, all] = await Promise.all([
                    services.discover(site, signal),
                    services.cameras(site, signal),
                    services.points(site, signal),
                  ]);
                  signal.throwIfAborted();
                  setInventory(cameras);
                  setDevices(found);
                  setDeviceQuery("");
                  setTarget("");
                  setPoints(all);
                  if (!found.length) tell(t("batch.noCameras"));
                })
              }
            >
              {t("batch.discover")}
            </Button>
            {!!devices.length && (
              <>
                <DiscoveredDeviceSelect
                  multiple
                  devices={devices}
                  query={deviceQuery}
                  setQuery={setDeviceQuery}
                  value={target}
                  onSelect={(id) => {
                    setTarget(id);
                    setPass("");
                  }}
                />
                <label className="bk-field">
                  {t("select.user")}
                  <input
                    name="batch_user"
                    value={username}
                    onChange={(e) => setUser(e.target.value)}
                  />
                </label>
                <label className="bk-field">
                  {t("select.password")}
                  <input
                    name="batch_password"
                    type="password"
                    autoComplete="off"
                    value={password}
                    onChange={(e) => setPass(e.target.value)}
                  />
                </label>
                <Button
                  disabled={!username || !listedDevices.length}
                  onClick={() => void run(inspect)}
                >
                  {t("batch.getChannels")}
                </Button>
              </>
            )}
          </details>
          {!!channels.length && (
            <>
              <label className="bk-field">
                {t("batch.template")}
                <select
                  value={selected?.id ?? ""}
                  onChange={(e) => chooseTemplate(e.target.value)}
                >
                  <option value="">{t("batch.newTemplate")}</option>
                  {templates.map((t) => (
                    <option value={t.id} key={t.id}>
                      {t.name} · {t.revision}
                    </option>
                  ))}
                </select>
              </label>
              <details>
                <summary>{t("batch.streamSettings")}</summary>
                <label className="bk-field">
                  {t("batch.streamTemplate")}
                  <select
                    value={mode}
                    onChange={(e) =>
                      setMode(e.target.value === "low" ? "low" : "high")
                    }
                  >
                    <option value="high">{t("batch.high")}</option>
                    <option value="low">{t("batch.low")}</option>
                  </select>
                </label>
                <label className="bk-field">
                  {t("batch.brightness")}
                  <input
                    name="batch_brightness"
                    type="number"
                    step="any"
                    placeholder={t("batch.keepCurrent")}
                    value={brightness}
                    onChange={(e) => setBrightness(e.target.value)}
                  />
                </label>
                <details>
                  <summary>{t("batch.saveTemplateSettings")}</summary>
                  <label className="bk-field">
                    {t("batch.templateName")}
                    <input
                      name="batch_template_name"
                      value={templateName}
                      maxLength={128}
                      onChange={(e) => setTemplateName(e.target.value)}
                    />
                  </label>
                  <Button
                    disabled={!templateName.trim() || !!matchingTemplate}
                    onClick={() => void run(saveTemplate)}
                  >
                    {t("batch.saveTemplate")}
                  </Button>
                </details>
              </details>
              <div className="ws-batch-toolbar">
                <h2>{t("batch.choose")}</h2>
                <Button
                  variant="primary"
                  disabled={!channels.some((c) => c.selected)}
                  onClick={() => void run(preview)}
                >
                  {t("batch.compare")}
                </Button>
              </div>
              <BatchChannels
                channels={channels}
                points={points}
                inventory={inventory}
                edit={edit}
              />
            </>
          )}
        </fieldset>
      )}
      <p role="status">{busy ? t("batch.busy") : notice}</p>
    </section>
  );
}
