import "./media-panels.css";
import type { Ref } from "react";
import icons from "../../../../design/bacuri/icons/sprite.svg";
import { formatUtc } from "./utc";
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
  const showRetry = retry && (state.kind === "error" || state.kind === "idle");
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
                onClick={() => {
                  if (mode !== m) onMode(m);
                }}
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
          <div className="ws-overlay" role="status">
            <svg aria-hidden="true">
              <use
                href={
                  icons +
                  "#i-" +
                  (state.kind === "error"
                    ? "unplug"
                    : state.kind === "loading"
                      ? "hourglass"
                      : "archive")
                }
              />
            </svg>
            <span>{state.message}</span>
          </div>
        )}
        {state.kind === "ready" && (
          <span className="ws-tag">
            {mode === "live" ? "Прямой эфир" : "Архив"}
          </span>
        )}
      </div>
      {(state.kind === "ready" || position || marker || showRetry || back) && (
        <div className="ws-transport">
          {state.kind === "ready" && (
            <span role="status" className="bk-indicator" data-tone="success">
              {state.message}
            </span>
          )}
          {position && <time dateTime={position}>{formatUtc(position)}</time>}
          {marker && <span>{marker}</span>}
          {showRetry && <Button onClick={retry}>Повторить</Button>}
          {back && <Button onClick={back}>К выбранной тревоге</Button>}
        </div>
      )}
    </section>
  );
}
