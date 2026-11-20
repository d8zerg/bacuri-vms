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
const empty = { name: "", main_url: "", secondary_url: "" };
export function Installer({
  services,
  site,
  online,
  connectionNotice,
}: {
  services: Services;
  site: string;
  online: boolean;
  connectionNotice: string;
}) {
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
  const [attempt, setAttempt] = useState(0);
  const [operation] = useState(() => ({}));
  const recording = useRef<AbortController | null>(null);
  const group = useRef<HTMLDivElement>(null);
  const recordBtn = useRef<HTMLButtonElement>(null),
    openBtn = useRef<HTMLButtonElement>(null),
    confirmBtn = useRef<HTMLButtonElement>(null);
  const form = useCameraConnectionCheck(
    empty,
    async (draft, signal) => {
      const applied = await services.register(site, draft, signal, operation);
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
    setPhase("recording");
    try {
      const interval = await services.record(site, camera.point_id, ctl.signal);
      ctl.signal.throwIfAborted();
      setTarget({
        mode: "archive",
        point: camera.point_id,
        interval,
        position: interval.start,
      });
      setPhase("clip");
    } catch {
      if (!ctl.signal.aborted) setPhase("error");
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
      <h1>Подключение камеры</h1>
      <div className="ws-install">
        <div ref={group}>
          {!online && <p role="status">{connectionNotice}</p>}
          <fieldset className="ws-form-fields" disabled={!online}>
            <CameraConnectionForm
              {...form}
              onChange={change}
              onCheck={() => {
                reset();
                form.onCheck();
              }}
              submitLabel="Подключить камеру"
            />
          </fieldset>
          {online && camera && (
            <section className="ws-panel ws-pad">
              <h2>Проверка записи</h2>
              <p>Конфигурация применена · версия {camera.applied_revision}</p>
              <div className="ws-actions">
                <Button
                  ref={recordBtn}
                  aria-disabled={phase === "recording"}
                  onClick={() => void record()}
                >
                  Создать тестовую запись
                </Button>
                {target && (
                  <Button
                    ref={openBtn}
                    onClick={() => {
                      setAttempt((n) => n + 1);
                      setPhase("opening");
                    }}
                  >
                    Открыть запись
                  </Button>
                )}
                {(phase === "frame" || phase === "confirmed") && (
                  <Button
                    ref={confirmBtn}
                    disabled={phase === "confirmed"}
                    onClick={() => setPhase("confirmed")}
                  >
                    Подтвердить запись
                  </Button>
                )}
              </div>
            </section>
          )}
        </div>
        <SessionPlayer
          key={attempt + ":" + String(playing) + ":" + (camera?.id ?? "")}
          services={services}
          site={site}
          title="Тестовая запись"
          mode="archive"
          target={online && playing ? target : null}
          onFrame={frame}
          onFailure={interrupted}
          {...(phase === "confirmed" ? { marker: "Запись подтверждена" } : {})}
          notice={
            phase === "recording"
              ? "Ожидаем новый блок записи…"
              : phase === "clip"
                ? "Фрагмент сохранён"
                : phase === "error"
                  ? "Не удалось получить новую запись. Повторите проверку."
                  : "Запись ещё не создана"
          }
          {...(target && playing
            ? {
                position: target.mode === "archive" ? target.position : "",
                retry: () => {
                  setAttempt((n) => n + 1);
                  setPhase("opening");
                },
              }
            : {})}
        />
      </div>
    </>
  );
}
