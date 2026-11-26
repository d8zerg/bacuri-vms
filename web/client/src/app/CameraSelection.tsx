import { useEffect, useRef, useState } from "react";
import type {
  Camera,
  CameraDraft,
  CameraInspection,
  DiscoveredCamera,
  Services,
} from "../contracts/workspace";
import { DiscoveredDeviceSelect } from "./DiscoveredDeviceSelect";
import { cameraError } from "./cameraError";
import { Button, IconButton } from "../ui-kit/FormControls";
import { t as translate, useT } from "../i18n";

export default function CameraSelection({
  services,
  site,
  onChoose,
}: {
  services: Services;
  site: string;
  onChoose: (draft: CameraDraft) => void;
}) {
  const t = useT();
  const [deviceQuery, setDeviceQuery] = useState("");
  const [found, setFound] = useState<DiscoveredCamera[]>([]);
  const [registered, setRegistered] = useState<Camera[]>([]);
  const [selection, select] = useState("");
  const [old, setOld] = useState("");
  const [username, user] = useState(""),
    [password, pass] = useState("");
  const [camera, setCamera] = useState<CameraInspection>();
  const [main, setMain] = useState(""),
    [sub, setSub] = useState("");
  const [notice, tell] = useState("");
  const [busy, setBusy] = useState(false);
  const active = useRef<AbortController | null>(null);
  useEffect(() => {
    const ctl = new AbortController();
    void services
      .cameras(site, ctl.signal)
      .then(setRegistered)
      .catch(() => {
        if (!ctl.signal.aborted) tell(translate("select.listFailed"));
      });
    return () => {
      ctl.abort();
      active.current?.abort();
    };
  }, [site, services]);

  async function run(inspect: boolean) {
    if (active.current) return;
    const ctl = new AbortController();
    active.current = ctl;
    setBusy(true);
    tell("");
    setCamera(undefined);
    try {
      if (inspect) {
        const info = await services.inspect(
          site,
          selection,
          username,
          password,
          ctl.signal,
        );
        ctl.signal.throwIfAborted();
        setCamera(info);
        setMain(info.profiles[0]!.url);
        setSub(
          info.profiles.find(
            (p) =>
              p.channel === info.profiles[0]!.channel &&
              p.url !== info.profiles[0]!.url,
          )?.url ?? "",
        );
      } else {
        setDeviceQuery("");
        setFound([]);
        select("");
        user("");
        pass("");
        const result = await services.discover(site, ctl.signal);
        ctl.signal.throwIfAborted();
        setFound(result);
        select(result[0]?.selection ?? "");
        if (!result.length) tell(t("select.noHttps"));
      }
    } catch (error) {
      if (!ctl.signal.aborted)
        tell(cameraError(error) ?? t("select.checkFailed"));
    } finally {
      if (!ctl.signal.aborted) {
        active.current = null;
        setBusy(false);
      }
    }
  }
  const previous = registered.find((c) => c.id === old);
  const source = camera?.profiles.find((p) => p.url === main)?.channel;
  const compatible = camera?.profiles.filter((p) => p.channel === source);
  const existing =
    camera &&
    registered.find(
      (c) =>
        c.endpoint_id === camera.endpoint_id &&
        (!c.channel_token || c.channel_token === source),
    );
  async function refresh() {
    if (!existing || !camera || active.current) return;
    const ctl = new AbortController();
    active.current = ctl;
    setBusy(true);
    try {
      const updated = await services.refreshOnvif(
        site,
        existing,
        {
          name: existing.name,
          main_url: main,
          secondary_url: sub,
          credentials: { username, password },
          onvif_selection: camera.selection,
        },
        ctl.signal,
      );
      ctl.signal.throwIfAborted();
      setRegistered((values) =>
        values.map((value) => (value.id === updated.id ? updated : value)),
      );
      tell(t("select.updated"));
      setCamera(undefined);
      pass("");
    } catch (error) {
      if (!ctl.signal.aborted)
        tell(cameraError(error) ?? t("select.updateFailed"));
    } finally {
      if (!ctl.signal.aborted) {
        active.current = null;
        setBusy(false);
      }
    }
  }
  function pick(onvif: boolean) {
    const draft: CameraDraft = {
      name: previous?.name ?? (onvif ? camera!.model : ""),
      main_url: onvif ? main : "",
      secondary_url: onvif ? sub : "",
      ...(username ? { credentials: { username, password } } : {}),
      ...(previous
        ? { point_id: previous.point_id, replace_device_id: previous.id }
        : {}),
      ...(onvif
        ? {
            onvif_selection: camera!.selection,
          }
        : {}),
    };
    onChoose(draft);
  }
  return (
    <section className="bc-camera" aria-label={t("select.label")}>
      <fieldset className="bc-fields ws-camera-fields" disabled={busy}>
        <label className="bk-field">
          {t("select.point")}
          <select value={old} onChange={(e) => setOld(e.target.value)}>
            <option value="">{t("batch.newPoint")}</option>
            {registered.map((c) => (
              <option value={c.id} key={c.id}>
                {t("select.replace", { name: c.name })}
              </option>
            ))}
          </select>
        </label>
        <Button onClick={() => void run(false)}>{t("select.find")}</Button>
        {!!found.length && (
          <DiscoveredDeviceSelect
            devices={found}
            query={deviceQuery}
            setQuery={setDeviceQuery}
            value={selection}
            onSelect={(id) => {
              select(id);
              setCamera(undefined);
              user("");
              pass("");
            }}
          />
        )}
        <label className="bk-field">
          {t("select.user")}
          <input
            name="camera_user"
            autoComplete="off"
            value={username}
            onChange={(e) => {
              user(e.target.value);
              setCamera(undefined);
            }}
          />
        </label>
        <label className="bk-field">
          {t("select.password")}
          <input
            name="camera_password"
            type="password"
            autoComplete="off"
            value={password}
            onChange={(e) => {
              pass(e.target.value);
              setCamera(undefined);
            }}
          />
        </label>
        {!!found.length && (
          <Button
            disabled={!username || !selection}
            onClick={() => void run(true)}
          >
            {t("select.profiles")}
          </Button>
        )}
        {camera && (
          <>
            <p>
              {camera.manufacturer} · {camera.model} ·{" "}
              {t("select.firmware", { version: camera.firmware })}
              <br />
              {t("select.serial", { serial: camera.serial })}
              <br />
              {t(camera.ptz ? "select.ptzYes" : "select.ptzNo")} ·{" "}
              {t(camera.events ? "select.eventsYes" : "select.eventsNo")}
            </p>
            {(["main", "sub"] as const).map((role) => (
              <label className="bk-field" key={role}>
                {role === "main"
                  ? t("select.recordProfile")
                  : t("select.liveProfile")}
                <select
                  value={role === "main" ? main : sub}
                  onChange={(e) => {
                    const url = e.target.value;
                    if (role === "sub") setSub(url);
                    else {
                      setMain(url);
                      const group = camera.profiles.filter(
                        (p) =>
                          p.channel ===
                            camera.profiles.find((p) => p.url === url)
                              ?.channel && p.url !== url,
                      );
                      if (!group.some((p) => p.url === sub))
                        setSub(group[0]?.url ?? "");
                    }
                  }}
                >
                  {(role === "main" ? camera.profiles : (compatible ?? [])).map(
                    (p) => (
                      <option key={p.token} value={p.url}>
                        {p.name}
                        {p.codec
                          ? " · " + (p.codec === "h265" ? "HEVC" : "H.264")
                          : ""}{" "}
                        · {p.url}
                      </option>
                    ),
                  )}
                </select>
              </label>
            ))}
            {main === sub && <p role="status">{t("select.sameProfile")}</p>}
            {existing ? (
              <IconButton
                icon="rotate-cw"
                label={t("select.refresh")}
                variant="primary"
                disabled={!sub || main === sub}
                onClick={() => void refresh()}
              />
            ) : (
              <Button
                variant="primary"
                disabled={!sub || main === sub}
                onClick={() => pick(true)}
              >
                {t("select.use")}
              </Button>
            )}
          </>
        )}
        <Button onClick={() => pick(false)}>{t("select.manual")}</Button>
      </fieldset>
      <div role="status" className="ws-camera-footer">
        {busy ? t("select.waiting") : notice}
      </div>
    </section>
  );
}
