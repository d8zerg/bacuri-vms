import { useEffect, useState } from "react";
import type { ManualRecording, Services } from "../contracts/workspace";
import { IconButton } from "../ui-kit/FormControls";
export default function ManualCapture({
  site,
  point,
  services,
}: {
  site: string;
  point: string;
  services: Services;
}) {
  const [state, set] = useState<ManualRecording | null>(null),
    [busy, working] = useState(false),
    [error, failed] = useState(false),
    [reload, refresh] = useState(0);
  useEffect(() => {
    const ctl = new AbortController();
    let timer: ReturnType<typeof setTimeout>;
    async function read() {
      try {
        const s = await services.manualRecording(site, point, ctl.signal);
        if (!ctl.signal.aborted) {
          set(s);
          failed(false);
        }
      } catch {
        if (!ctl.signal.aborted) failed(true);
      } finally {
        if (!ctl.signal.aborted) timer = setTimeout(() => void read(), 5000);
      }
    }
    if (!busy) void read();
    return () => {
      ctl.abort();
      clearTimeout(timer);
    };
  }, [site, point, services, reload, busy]);
  async function toggle() {
    working(true);
    try {
      set(await services.setManualRecording(site, point, !state?.active));
      failed(false);
    } catch {
      failed(true);
    } finally {
      working(false);
    }
  }
  if (state && !state.enabled && !error) return null;
  return (
    <div className="ws-record-command">
      <IconButton
        icon={state?.active ? "ptz-stop" : "record-video"}
        label={
          state?.active
            ? "Остановить ручную запись"
            : "Начать ручную запись на 15 минут"
        }
        aria-pressed={state?.active ?? false}
        disabled={!state || busy || error}
        onClick={() => void toggle()}
      />
      {state?.active && state.until_us && (
        <span role="status">
          Запись до{" "}
          {new Date(state.until_us / 1000).toLocaleTimeString("ru", {
            hour: "2-digit",
            minute: "2-digit",
          })}
        </span>
      )}
      {error && (
        <span role="alert">
          Состояние записи недоступно{" "}
          <IconButton
            icon="rotate-cw"
            label="Проверить состояние записи"
            onClick={() => refresh((n) => n + 1)}
          />
        </span>
      )}
    </div>
  );
}
