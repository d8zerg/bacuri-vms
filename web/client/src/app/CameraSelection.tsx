import { useEffect, useRef, useState } from "react";
import type {
  Camera,
  CameraDraft,
  CameraInspection,
  DiscoveredCamera,
  Services,
} from "../contracts/workspace";
import { HttpFailure } from "../adapters/api";
import { Button } from "../ui-kit/FormControls";

const failures: Record<string, string> = {
  camera_authentication_failed: "Камера отклонила имя или пароль.",
  camera_ptz_busy: "Камерой управляет другой оператор.",
  camera_ptz_unknown:
    "Нет подтверждения камеры. Проверьте её положение в эфире.",
  camera_ptz_unsupported: "Профиль камеры не поддерживает эту команду.",
  camera_tls_error: "Проверьте имя камеры и доверенный сертификат.",
  camera_incompatible_profiles:
    "Для подключения нужны два разных профиля H.264.",
  camera_transport_denied:
    "Адрес или транспорт камеры запрещён настройками площадки.",
  camera_protocol_error: "Камера вернула неподдерживаемый ответ ONVIF.",
  camera_already_registered: "Эта камера уже подключена на площадке.",
  camera_binding_conflict: "Камера точки изменилась. Выберите точку заново.",
  camera_selection_expired: "Повторите обнаружение и выбор профилей.",
  camera_discovery_unavailable:
    "Нет доступного интерфейса в разрешённом сегменте камер.",
  camera_unavailable: "Камера не ответила вовремя.",
};
export function cameraError(error: unknown) {
  return error instanceof HttpFailure ? failures[error.code] : undefined;
}

export function CameraSelection({
  services,
  site,
  onChoose,
  onCancel,
}: {
  services: Services;
  site: string;
  onChoose: (draft: CameraDraft) => void;
  onCancel: () => void;
}) {
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
  const title = useRef<HTMLHeadingElement>(null);
  const active = useRef<AbortController | null>(null);
  useEffect(() => {
    title.current?.focus();
    const ctl = new AbortController();
    void services
      .cameras(site, ctl.signal)
      .then(setRegistered)
      .catch(() => {
        if (!ctl.signal.aborted)
          tell("Не удалось получить список подключённых камер.");
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
          info.profiles.find((p) => p.url !== info.profiles[0]!.url)?.url ?? "",
        );
      } else {
        setFound([]);
        select("");
        user("");
        pass("");
        const result = await services.discover(site, ctl.signal);
        ctl.signal.throwIfAborted();
        setFound(result);
        select(result[0]?.selection ?? "");
        if (!result.length)
          tell("В разрешённом сегменте не обнаружены камеры с HTTPS.");
      }
    } catch (error) {
      if (!ctl.signal.aborted)
        tell(
          cameraError(error) ??
            "Проверка камеры не завершилась. Повторите попытку.",
        );
    } finally {
      if (!ctl.signal.aborted) {
        active.current = null;
        setBusy(false);
      }
    }
  }
  const previous = registered.find((c) => c.id === old);
  const existing =
    camera && registered.find((c) => c.endpoint_id === camera.endpoint_id);
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
      tell("Профили и возможности камеры обновлены.");
      setCamera(undefined);
      pass("");
    } catch (error) {
      if (!ctl.signal.aborted)
        tell(cameraError(error) ?? "Не удалось обновить камеру.");
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
    <section className="bc-camera">
      <header>
        <h2 tabIndex={-1} ref={title}>
          Выбор камеры
        </h2>
      </header>
      <fieldset className="bc-fields ws-camera-fields" disabled={busy}>
        <label className="bk-field">
          Точка наблюдения
          <select value={old} onChange={(e) => setOld(e.target.value)}>
            <option value="">Новая точка</option>
            {registered.map((c) => (
              <option value={c.id} key={c.id}>
                Заменить камеру · {c.name}
              </option>
            ))}
          </select>
        </label>
        <Button onClick={() => void run(false)}>Найти камеры ONVIF</Button>
        {!!found.length && (
          <label className="bk-field">
            Обнаруженная камера
            <select
              value={selection}
              onChange={(e) => {
                select(e.target.value);
                setCamera(undefined);
                user("");
                pass("");
              }}
            >
              {found.map((d) => (
                <option value={d.selection} key={d.endpoint_id}>
                  {d.device_service}
                </option>
              ))}
            </select>
          </label>
        )}
        <label className="bk-field">
          Пользователь камеры
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
          Пароль камеры
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
          <Button disabled={!username} onClick={() => void run(true)}>
            Получить профили
          </Button>
        )}
        {camera && (
          <>
            <p>
              {camera.manufacturer} · {camera.model} · прошивка{" "}
              {camera.firmware}
              <br />
              Серийный номер {camera.serial}
              <br />
              PTZ {camera.ptz ? "доступен" : "отсутствует"} · события{" "}
              {camera.events ? "доступны" : "отсутствуют"}
            </p>
            {(["main", "sub"] as const).map((role) => (
              <label className="bk-field" key={role}>
                {role === "main" ? "Профиль записи" : "Профиль эфира"}
                <select
                  value={role === "main" ? main : sub}
                  onChange={(e) =>
                    (role === "main" ? setMain : setSub)(e.target.value)
                  }
                >
                  {camera.profiles.map((p) => (
                    <option key={p.token} value={p.url}>
                      {p.name} · {p.url}
                    </option>
                  ))}
                </select>
              </label>
            ))}
            {main === sub && (
              <p role="status">Выберите разные профили для записи и эфира.</p>
            )}
            <Button
              variant="primary"
              disabled={!sub || main === sub}
              onClick={() => (existing ? void refresh() : pick(true))}
            >
              {existing
                ? "Обновить подключённую камеру"
                : "Использовать профили"}
            </Button>
          </>
        )}
        <Button onClick={() => pick(false)}>Ввести адреса вручную</Button>
      </fieldset>
      <div className="ws-actions ws-camera-footer">
        <Button onClick={onCancel}>Отмена</Button>
        <span role="status">{busy ? "Ожидаем ответ…" : notice}</span>
      </div>
    </section>
  );
}
