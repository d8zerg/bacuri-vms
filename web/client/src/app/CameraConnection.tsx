import { HttpFailure } from "../adapters/api";
import { ErrorHelpLink } from "./ErrorHelpLink";
import { HelpLink } from "./HelpLink";
import "../i18n/extended";
import { useCallback, useEffect, useRef, useState } from "react";
import type {
  Camera,
  CameraDraft,
  Services,
  Target,
} from "../contracts/workspace";
import { CameraConnectionForm } from "../ui-kit/CameraConnectionForm";
import { useCameraConnectionCheck } from "../workflows/useCameraConnectionCheck";
import { Button } from "../ui-kit/FormControls";
import { SessionPlayer } from "./media";
import { cameraError } from "./cameraError";
import { useT } from "../i18n";
export default function Connection({
  services,
  site,
  online,
  connectionNotice,
  initial,
}: {
  services: Services;
  site: string;
  online: boolean;
  connectionNotice: string;
  initial: CameraDraft;
}) {
  const t = useT();
  const [camera, setCamera] = useState<Camera | null>(null);
  const [phase, setPhase] = useState<
    | "idle"
    | "recording"
    | "clip"
    | "opening"
    | "frame"
    | "confirmed"
    | "error"
    | "interrupted"
  >("idle");
  const [target, setTarget] = useState<Target | null>(null);
  const [history, setHistory] = useState<Target | null>(null);
  const [historyNotice, setHistoryNotice] = useState("");
  const [attempt, setAttempt] = useState(0);
  const [operation] = useState(() => ({}));
  const recording = useRef<AbortController | null>(null);
  const group = useRef<HTMLDivElement>(null);
  const recordBtn = useRef<HTMLButtonElement>(null),
    openBtn = useRef<HTMLButtonElement>(null),
    confirmBtn = useRef<HTMLButtonElement>(null);
  const form = useCameraConnectionCheck(
    initial,
    async (draft, signal) => {
      let applied: Camera;
      try {
        applied = await services.register(site, draft, signal, operation);
      } catch (error) {
        const message = cameraError(error);
        if (message)
          return {
            status: "error",
            reason: "unknown",
            message,
            code: error instanceof HttpFailure ? error.code : undefined,
          };
        throw error;
      }
      signal.throwIfAborted();
      if (
        applied.state !== "applied" ||
        applied.applied_revision !== applied.desired_revision
      )
        throw new Error("Configuration not applied");
      const profiles = await services.checkProfiles(site, applied, signal);
      signal.throwIfAborted();
      if (profiles.main === "available" && profiles.secondary === "available")
        setCamera(applied);
      return { status: "profiles", ...profiles };
    },
    online,
  );
  useEffect(() => () => recording.current?.abort(), []);
  useEffect(() => {
    if (
      camera &&
      document.activeElement ===
        group.current?.querySelector("button[type=submit]")
    )
      recordBtn.current?.focus();
  }, [camera]);
  useEffect(() => {
    if (phase === "clip" && document.activeElement === recordBtn.current)
      openBtn.current?.focus();
    if (phase === "frame" && document.activeElement === openBtn.current)
      confirmBtn.current?.focus();
  }, [phase]);
  const reset = useCallback(() => {
    recording.current?.abort();
    recording.current = null;
    setHistory(null);
    setHistoryNotice("");
    setCamera(null);
    setTarget(null);
    setPhase("idle");
  }, []);
  useEffect(() => {
    if (!online) reset();
  }, [online, reset]);
  function change(draft: CameraDraft) {
    reset();
    form.onChange(draft);
  }
  async function record() {
    if (!camera || recording.current) return;
    const ctl = new AbortController();
    recording.current = ctl;
    setTarget(null);
    setHistory(null);
    setHistoryNotice("");
    setPhase("recording");
    try {
      const interval = await services.record(
        site,
        camera.point_id,
        ctl.signal,
        camera.binding_revision,
      );
      ctl.signal.throwIfAborted();
      setTarget({
        mode: "archive",
        point: camera.point_id,
        interval,
        position: interval.start,
        ...(camera.binding_revision
          ? { binding_revision: camera.binding_revision }
          : {}),
      });
      setPhase("clip");
    } catch {
      if (!ctl.signal.aborted) setPhase("error");
    } finally {
      if (recording.current === ctl) recording.current = null;
    }
  }
  async function openPrevious() {
    if (!camera || recording.current) return;
    const ctl = new AbortController();
    recording.current = ctl;
    try {
      const previous = await services.previousRecording(
        site,
        camera.id,
        ctl.signal,
      );
      ctl.signal.throwIfAborted();
      setHistory({
        mode: "archive",
        point: camera.point_id,
        interval: previous.interval,
        position: previous.interval.start,
        binding_revision: previous.binding_revision,
      });
      setHistoryNotice("");
      setAttempt((n) => n + 1);
    } catch {
      if (!ctl.signal.aborted)
        setHistoryNotice(t("connect.previousUnavailable"));
    } finally {
      if (recording.current === ctl) recording.current = null;
    }
  }
  const frame = useCallback(() => setPhase("frame"), []);
  const interrupted = useCallback(() => setPhase("interrupted"), []);
  const playing =
    phase === "opening" ||
    phase === "frame" ||
    phase === "confirmed" ||
    phase === "interrupted";
  return (
    <>
      <div className="ws-install">
        <div ref={group}>
          {!online && <p role="status">{connectionNotice}</p>}
          {initial.replace_device_id && (
            <p>{t("connect.replace", { name: initial.name })}</p>
          )}
          <fieldset className="ws-form-fields" disabled={!online}>
            <CameraConnectionForm
              {...form}
              onChange={change}
              onCheck={() => {
                reset();
                form.onCheck();
              }}
              submitLabel={t("connect.submit")}
            />
          </fieldset>
          {form.state.status === "error" && (
            <ErrorHelpLink code={form.state.code} article="connect-camera" />
          )}
          {online && camera && (
            <section className="ws-panel ws-pad">
              <h2>{t("connect.recordCheck")}</h2>
              <p>
                {t("connect.applied", {
                  revision: camera.applied_revision ?? "",
                })}
              </p>
              <div className="ws-actions">
                <Button
                  variant={!target ? "primary" : "secondary"}
                  ref={recordBtn}
                  aria-disabled={phase === "recording"}
                  onClick={() => void record()}
                >
                  {t("connect.createTest")}
                </Button>
                {target && (
                  <Button
                    variant={phase === "clip" ? "primary" : "secondary"}
                    ref={openBtn}
                    onClick={() => {
                      setHistory(null);
                      setAttempt((n) => n + 1);
                      setPhase("opening");
                    }}
                  >
                    {t("connect.openRecording")}
                  </Button>
                )}
                {camera.binding_revision && camera.binding_revision !== "1" && (
                  <Button onClick={() => void openPrevious()}>
                    {t("connect.openPrevious")}
                  </Button>
                )}
                {historyNotice && <p role="status">{historyNotice}</p>}
                {!history && (phase === "frame" || phase === "confirmed") && (
                  <Button
                    variant="primary"
                    ref={confirmBtn}
                    disabled={phase === "confirmed"}
                    onClick={() => setPhase("confirmed")}
                  >
                    {t("connect.confirm")}
                  </Button>
                )}
              </div>
            </section>
          )}
        </div>
        {playing || history ? (
          <SessionPlayer
            key={attempt + ":" + String(playing) + ":" + (camera?.id ?? "")}
            services={services}
            site={site}
            title={history ? t("connect.previous") : t("connect.test")}
            mode="archive"
            target={online ? (history ?? (playing ? target : null)) : null}
            {...(history ? {} : { onFrame: frame, onFailure: interrupted })}
            {...(!history && phase === "confirmed"
              ? { marker: t("connect.confirmed") }
              : {})}
            notice={connectionNotice}
            {...(!history && target && playing
              ? {
                  position: target.mode === "archive" ? target.position : "",
                  retry: () => {
                    setAttempt((n) => n + 1);
                    setPhase("opening");
                  },
                }
              : {})}
          />
        ) : (
          <section
            className="ws-panel ws-record-guide"
            aria-labelledby="record-guide-heading"
          >
            <header className="ws-panel-head">
              <h2 id="record-guide-heading">{t("connect.guide")}</h2>
            </header>
            <ol>
              <li aria-current={!camera ? "step" : undefined}>
                <strong>{t("connect.checkStreams")}</strong>
                <span>
                  {camera ? t("connect.streamsOk") : t("connect.streamsHint")}
                </span>
              </li>
              <li aria-current={camera && !target ? "step" : undefined}>
                <strong>{t("connect.createTest")}</strong>
                <span role="status">
                  {phase === "recording"
                    ? t("connect.waitBlock")
                    : phase === "error"
                      ? t("connect.recordFailed")
                      : target
                        ? t("connect.saved")
                        : t("connect.saveHint")}
                  {phase === "error" && (
                    <>
                      {" "}
                      <HelpLink article="recording-missing" />
                    </>
                  )}
                </span>
              </li>
              <li aria-current={target ? "step" : undefined}>
                <strong>{t("connect.confirmFrame")}</strong>
                <span>{t("connect.confirmHint")}</span>
              </li>
            </ol>
          </section>
        )}
      </div>
    </>
  );
}
