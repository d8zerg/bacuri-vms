import { useEffect, useId, useRef, useState } from "react";
import type { KeyboardEvent } from "react";
import type {
  CameraControls,
  Point,
  PtzCommand,
  Services,
} from "../contracts/workspace";
import { Button, ControlGlyph, IconButton } from "../ui-kit/FormControls";
import type { ControlIcon } from "../ui-kit/FormControls";
import { FilterPanel, matchesQuery } from "../ui-kit/FilterPanel";
import { CameraAudio } from "./CameraAudio";
import { DirectionKey } from "./DirectionKey";
import { cameraError } from "./cameraError";
import { ptzLabel } from "./cameraCommands";
import "./camera-console.css";
import { dateTimeFormat, useT, type MessageKey } from "../i18n";

type Action = CameraControls["actions"][number];
type Section = "drive" | "equipment" | "history";
const icons: Record<PtzCommand, ControlIcon> = {
  up: "chevron-up",
  down: "chevron-down",
  left: "chevron-left",
  right: "chevron-right",
  stop: "ptz-stop",
  zoom_in: "plus",
  zoom_out: "minus",
  focus_near: "focus-near",
  focus_far: "focus-far",
  focus_stop: "lens-focus",
  preset_goto: "bookmark",
  tour_start: "play",
  tour_stop: "ptz-stop",
  relay_on: "power-on",
  relay_off: "power-off",
  wiper_on: "power-on",
  wiper_off: "power-off",
  light_on: "power-on",
  light_off: "power-off",
};
const arrows: PtzCommand[] = ["up", "left", "right", "down"];
const keys: Record<string, PtzCommand> = {
  ArrowUp: "up",
  ArrowDown: "down",
  ArrowLeft: "left",
  ArrowRight: "right",
  "+": "zoom_in",
  "=": "zoom_in",
  "-": "zoom_out",
  Escape: "stop",
};
const outcomes = {
  acknowledged: "console.outcome.acknowledged",
  unknown: "console.outcome.unknown",
  rejected: "console.outcome.rejected",
} as const satisfies Record<string, MessageKey>;
const time = (at: string) =>
  dateTimeFormat({ timeStyle: "medium" }).format(new Date(at));

export function CameraConsole({
  services,
  site,
  point,
  ptz,
  audio,
  position,
}: {
  services: Services;
  site: string;
  point: string;
  ptz: Point["ptz"];
  audio: Point["audio"];
  position: number | undefined;
}) {
  const t = useT();
  const [section, setSection] = useState<Section>("drive");
  const [catalog, setCatalog] = useState<CameraControls>();
  const [revision, reload] = useState(0);
  const [pending, setPending] = useState(0);
  const [readError, setReadError] = useState(false);
  const [loading, setLoading] = useState(false);
  const [notice, setNotice] = useState<{ text: string; error: boolean }>();
  const [filter, setFilter] = useState("");
  const [route, setRoute] = useState("");
  const next = useRef(0),
    inFlight = useRef(new Set<PtzCommand>());
  const panel = useId();
  const activeKey = useRef<AbortController | null>(null);
  const device = ptz?.device_id;
  useEffect(() => {
    if (!device) return;
    const ctl = new AbortController();
    setLoading(true);
    services
      .cameraControls(site, point, ctl.signal)
      .then((result) => {
        if (ctl.signal.aborted) return;
        if (result.device_id !== device) throw Error("Camera changed");
        setCatalog(result);
        setReadError(false);
      })
      .catch(() => {
        if (!ctl.signal.aborted) {
          setReadError(true);
          setCatalog(undefined);
        }
      })
      .finally(() => {
        if (!ctl.signal.aborted) setLoading(false);
      });
    return () => ctl.abort();
  }, [services, site, point, device, revision]);

  async function send(command: PtzCommand, target?: string, refresh = true) {
    if (command === "stop") activeKey.current?.abort();
    if (
      !device ||
      inFlight.current.has(command) ||
      (inFlight.current.size &&
        !["stop", "focus_stop", "tour_stop"].includes(command))
    )
      return false;
    const token = ++next.current;
    inFlight.current.add(command);
    setPending(inFlight.current.size);
    setNotice(undefined);
    try {
      await services.ptz(site, point, device, command, target);
      if (token === next.current)
        setNotice({
          text: command === "stop" ? t("console.stopAck") : t("console.ack"),
          error: false,
        });
      return true;
    } catch (e) {
      if (token === next.current)
        setNotice({
          text: cameraError(e) ?? t("console.noAck"),
          error: true,
        });
      return false;
    } finally {
      inFlight.current.delete(command);
      setPending(inFlight.current.size);
      if (refresh) reload((v) => v + 1);
    }
  }
  const actions = catalog?.actions ?? [];
  const basic = ptz?.commands ?? [];
  function commandButton(
    command: PtzCommand,
    item?: Action,
    className?: string,
  ) {
    if (!item && !basic.includes(command)) return null;
    const name =
      ptzLabel(command) +
      (item?.label &&
      (command.startsWith("tour_") || command.startsWith("relay_"))
        ? " " + item.label
        : "");
    return (
      <IconButton
        key={command + "/" + (item?.target ?? "")}
        className={className ?? ""}
        data-command={command}
        icon={icons[command]}
        label={name}
        aria-disabled={
          pending > 0 &&
          (inFlight.current.has(command) ||
            !["stop", "focus_stop", "tour_stop"].includes(command))
        }
        onClick={() => void send(command, item?.target)}
      />
    );
  }
  function available(command: PtzCommand) {
    return commandButton(
      command,
      actions.find((a) => a.command === command),
    );
  }
  function shortcuts(e: KeyboardEvent<HTMLElement>) {
    if (
      e.target instanceof HTMLInputElement ||
      e.target instanceof HTMLSelectElement ||
      e.ctrlKey ||
      e.altKey ||
      e.metaKey
    )
      return;
    const cmd = keys[e.key];
    if (!cmd || !basic.includes(cmd)) return;
    e.preventDefault();
    if (!e.repeat) void send(cmd);
  }
  const presets = actions.filter((a) => a.command === "preset_goto");
  const visiblePresets = presets.filter((a) => matchesQuery(filter, a.label));
  const tours = actions.filter(
    (a) => a.command === "tour_start" || a.command === "tour_stop",
  );
  const routes = [...new Map(tours.map((a) => [a.target, a])).values()];
  const selectedRoute = routes.find((a) => a.target === route) ?? routes[0];
  const hasPad = arrows.some((d) => basic.includes(d));
  const hasZoom = basic.includes("zoom_in") || basic.includes("zoom_out");
  const hasFocus = actions.some((a) => a.command.startsWith("focus_"));
  const hasEquipment =
    actions.some((a) => /^(light|wiper|relay)_/.test(a.command)) ||
    !!catalog?.inputs.length;
  const active = section === "equipment" && !hasEquipment ? "drive" : section;
  const tabs: ReadonlyArray<readonly [Section, ControlIcon, MessageKey]> = [
    ["drive", "move", "console.tab.drive"],
    ...(hasEquipment
      ? [["equipment", "settings-2", "console.tab.equipment"] as const]
      : []),
    ["history", "history", "console.tab.history"],
  ];
  const owner = catalog?.owner;
  useEffect(() => {
    if (!owner) return;
    const delay = Date.parse(owner.until) - Date.now();
    if (!(delay > 0)) return;
    const timer = setTimeout(
      () =>
        setCatalog((current) => {
          if (!current || current.owner !== owner) return current;
          const updated = { ...current };
          delete updated.owner;
          return updated;
        }),
      delay,
    );
    return () => clearTimeout(timer);
  }, [owner]);
  function hardware(
    label: string,
    icon: ControlIcon,
    items: Action[],
    showTarget = false,
  ) {
    if (!items.length) return null;
    return (
      <div className="cc-device" key={label}>
        <div className="cc-device-name">
          <ControlGlyph icon={icon} />
          <span>{label}</span>
        </div>
        <div className="cc-switch">
          {items.map((a) => (
            <span key={a.command + a.target}>
              {showTarget && <small>{a.label}</small>}
              {commandButton(a.command, a)}
            </span>
          ))}
        </div>
      </div>
    );
  }
  return (
    <div
      className="camera-console"
      role="group"
      onKeyDown={shortcuts}
      aria-label={ptz ? t("console.ptz") : t("console.audio")}
    >
      <div className="cc-bar">
        {ptz && (
          <div
            className="cc-sections"
            role="group"
            aria-label={t("console.sections")}
          >
            {tabs.map(([id, icon, label]) => (
              <IconButton
                key={id}
                icon={icon}
                label={t(label)}
                aria-controls={panel}
                aria-pressed={active === id}
                onClick={() => setSection(id)}
              />
            ))}
          </div>
        )}
        {audio && (
          <CameraAudio
            key={audio.device_id + "/" + (position ?? "live")}
            services={services}
            site={site}
            point={point}
            capabilities={audio}
            position={position}
          />
        )}
      </div>
      {ptz && (
        <div id={panel}>
          {active === "drive" && (
            <div className="cc-drive">
              {hasPad ? (
                <div
                  className="cc-dial"
                  role="group"
                  aria-label={t("console.move")}
                >
                  {arrows
                    .filter((d) => basic.includes(d))
                    .map((d) => (
                      <DirectionKey
                        key={d}
                        command={d}
                        icon={icons[d]}
                        canStop={basic.includes("stop")}
                        disabled={pending > 0}
                        activeKey={activeKey}
                        send={send}
                      />
                    ))}
                  {commandButton("stop")}
                </div>
              ) : (
                commandButton("stop", undefined, "cc-single-stop")
              )}
              {(hasZoom || hasFocus) && (
                <div className="cc-lens">
                  {hasZoom && (
                    <div
                      className="cc-rocker"
                      role="group"
                      aria-label={t("console.zoom")}
                    >
                      {available("zoom_out")}
                      <ControlGlyph icon="search" />
                      {available("zoom_in")}
                    </div>
                  )}
                  {hasFocus && (
                    <div
                      className="cc-rocker"
                      role="group"
                      aria-label={t("console.focus")}
                    >
                      {available("focus_near")}
                      {actions.some((a) => a.command === "focus_stop") ? (
                        commandButton(
                          "focus_stop",
                          actions.find((a) => a.command === "focus_stop"),
                          "cc-focus-stop",
                        )
                      ) : (
                        <ControlGlyph icon="lens-focus" />
                      )}
                      {available("focus_far")}
                    </div>
                  )}
                </div>
              )}
              {(presets.length > 0 || selectedRoute) && (
                <div className="cc-memory">
                  {presets.length > 0 && (
                    <div
                      className="cc-positions"
                      role="group"
                      aria-label={t("console.presets")}
                    >
                      {presets.length > 5 && (
                        <FilterPanel
                          label={t("console.presetFilter")}
                          queryLabel={t("console.findPreset")}
                          query={filter}
                          onQuery={setFilter}
                          onReset={() => setFilter("")}
                        />
                      )}
                      <div className="cc-slots">
                        {visiblePresets.map((a) => (
                          <Button
                            key={a.target}
                            aria-label={ptzLabel("preset_goto") + " " + a.label}
                            title={ptzLabel("preset_goto") + " " + a.label}
                            aria-disabled={pending > 0}
                            onClick={() => void send(a.command, a.target)}
                          >
                            <ControlGlyph icon="bookmark" />
                            <span>{a.label}</span>
                            <ControlGlyph icon="chevron-right" />
                          </Button>
                        ))}
                      </div>
                      {filter && !visiblePresets.length && (
                        <small>{t("console.noPreset")}</small>
                      )}
                    </div>
                  )}
                  {selectedRoute && (
                    <div className="cc-route">
                      <ControlGlyph icon="tour" />
                      {routes.length > 1 ? (
                        <select
                          aria-label={t("console.patrol")}
                          value={selectedRoute.target}
                          onChange={(e) => setRoute(e.target.value)}
                        >
                          {routes.map((a) => (
                            <option key={a.target} value={a.target}>
                              {a.label}
                            </option>
                          ))}
                        </select>
                      ) : (
                        <span>{selectedRoute.label}</span>
                      )}
                      {tours
                        .filter((a) => a.target === selectedRoute.target)
                        .map((a) => commandButton(a.command, a))}
                    </div>
                  )}
                </div>
              )}
            </div>
          )}
          {active === "equipment" && (
            <div className="cc-equipment">
              {(["light", "wiper"] as const).map((prefix) => {
                const items = actions.filter((a) =>
                  a.command.startsWith(prefix + "_"),
                );
                return hardware(
                  t(prefix === "light" ? "console.light" : "console.wiper"),
                  prefix === "light" ? "sun" : "wiper",
                  items,
                  new Set(items.map((a) => a.command)).size !== items.length,
                );
              })}
              {[
                ...new Set(
                  actions
                    .filter((a) => a.command.startsWith("relay_"))
                    .map((a) => a.target),
                ),
              ].map((target) =>
                hardware(
                  t("console.output", { target }),
                  "relay",
                  actions.filter(
                    (a) =>
                      a.command.startsWith("relay_") && a.target === target,
                  ),
                ),
              )}
              {catalog?.inputs.map((input) => (
                <div className="cc-device" key={"input/" + input.token}>
                  <div className="cc-device-name">
                    <ControlGlyph icon="input" />
                    <span>{t("console.input", { token: input.token })}</span>
                  </div>
                  <span
                    className="cc-signal"
                    data-state={input.active}
                    title={
                      input.observed_at ? time(input.observed_at) : undefined
                    }
                  >
                    {input.active === undefined
                      ? t("console.unknown")
                      : input.active
                        ? t("console.active")
                        : t("console.inactive")}
                    {input.observed_at && (
                      <time dateTime={input.observed_at}>
                        {time(input.observed_at)}
                      </time>
                    )}
                  </span>
                </div>
              ))}
            </div>
          )}
          {active === "history" && (
            <div className="cc-history">
              {catalog?.history.length ? (
                <ol>
                  {catalog.history.map((row, i) => (
                    <li key={i} data-outcome={row.outcome}>
                      <ControlGlyph
                        icon={
                          row.outcome === "acknowledged"
                            ? "circle-check"
                            : row.outcome === "unknown"
                              ? "clock-alert"
                              : "triangle-alert"
                        }
                      />
                      <div>
                        <strong>
                          {ptzLabel(row.command)} {row.target}
                        </strong>
                        <small>
                          {row.actor} · {t(outcomes[row.outcome])}
                        </small>
                      </div>
                      <time dateTime={row.at}>{time(row.at)}</time>
                    </li>
                  ))}
                </ol>
              ) : (
                <p>
                  {loading
                    ? t("console.loadingLog")
                    : readError
                      ? t("console.logUnavailable")
                      : t("console.noCommands")}
                </p>
              )}
            </div>
          )}
          {(active !== "drive" || readError || catalog?.capability_error) && (
            <div className="cc-aux-stop">
              {active !== "drive" && commandButton("stop")}{" "}
              {
                <IconButton
                  className="cc-refresh"
                  icon="rotate-cw"
                  label={t("console.refresh")}
                  aria-busy={loading}
                  onClick={() => reload((v) => v + 1)}
                />
              }
            </div>
          )}
        </div>
      )}
      {owner && Date.parse(owner.until) > Date.now() && (
        <div className="cc-owner">
          <ControlGlyph icon="user-round" />
          {t("console.owner", {
            name: owner.name,
            priority: owner.priority,
            until: time(owner.until),
          })}
        </div>
      )}
      {(readError || catalog?.capability_error) && (
        <div className="cc-feedback" role="status">
          {t("console.offline")}
          {catalog?.capability_error ? " " + t("console.savedLog") : ""}
        </div>
      )}
      {notice && (
        <div
          className="cc-feedback"
          data-error={notice.error}
          role={notice.error ? "alert" : "status"}
        >
          <ControlGlyph
            icon={notice.error ? "triangle-alert" : "circle-check"}
          />
          {notice.text}
        </div>
      )}
    </div>
  );
}
