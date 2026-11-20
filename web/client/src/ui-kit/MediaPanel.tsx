import "./media-panels.css";
import type { Ref } from "react";
import { Button } from "./FormControls";
export type PlayerState = {
  kind: "loading" | "ready" | "idle" | "error";
  message: string;
};
interface Props {
  title: string;
  mode: "live" | "archive";
  state: PlayerState;
  videoRef: Ref<HTMLVideoElement>;
  onMode?: (mode: "live" | "archive") => void;
  retry?: () => void;
  back?: () => void;
  position?: string;
  marker?: string;
  headingRef?: Ref<HTMLHeadingElement>;
}
export function MediaPanel({
  title,
  mode,
  state,
  videoRef,
  onMode,
  retry,
  back,
  position,
  marker,
  headingRef,
}: Props) {
  return (
    <section className="ws-panel ws-player" aria-label="Видеоплеер">
      <header className="ws-panel-head">
        <div>
          <h2 tabIndex={-1} ref={headingRef}>
            {title}
          </h2>
          <p>
            {mode === "live"
              ? "Дополнительный поток"
              : "Основной поток · Архив"}
          </p>
        </div>
        {onMode && (
          <div className="ws-modes" role="group" aria-label="Режим просмотра">
            {(["live", "archive"] as const).map((m) => (
              <Button
                key={m}
                aria-pressed={mode === m}
                onClick={() => onMode(m)}
              >
                {m === "live" ? "Эфир" : "Архив"}
              </Button>
            ))}
          </div>
        )}
      </header>
      <div className="ws-media">
        <video
          ref={videoRef}
          muted
          playsInline
          controls={state.kind === "ready"}
          style={{ visibility: state.kind === "ready" ? "visible" : "hidden" }}
          aria-label={mode === "live" ? "Прямой эфир" : "Архивная запись"}
        />
        {state.kind !== "ready" && (
          <div className="ws-overlay">{state.message}</div>
        )}
        {state.kind === "ready" && (
          <span className="ws-tag">
            {mode === "live" ? "Прямой эфир" : "Архив"}
          </span>
        )}
      </div>
      <div className="ws-transport">
        <span role="status">{state.message}</span>
        {position && (
          <time dateTime={position}>
            {position.replace("T", " ").replace("Z", " UTC")}
          </time>
        )}
        {marker && <span>{marker}</span>}
        {retry && <Button onClick={retry}>Повторить</Button>}
        {back && <Button onClick={back}>К выбранной тревоге</Button>}
      </div>
    </section>
  );
}
