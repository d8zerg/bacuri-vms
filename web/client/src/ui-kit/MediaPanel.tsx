import "./media-panels.css";
import { useEffect, useRef, useState } from "react";
import type { ReactNode, Ref } from "react";
import icons from "../../../../design/bacuri/icons/sprite.svg";
import type { Quality } from "../contracts/workspace";
import { formatUtc } from "./utc";
import { Button, IconButton } from "./FormControls";
export type PlayerState = {
  kind: "loading" | "ready" | "idle" | "error";
  message: string;
};
interface Props {
  controls?: ReactNode;
  description?: string;
  onExpand?: () => void;
  expanded?: boolean;
  quality?: Quality;
  onQuality?: (quality: Quality) => void;
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
  controls,
  description,
  quality,
  onQuality,
  onExpand,
  expanded,
}: Props) {
  const player = useRef<HTMLElement>(null);
  const [full, setFull] = useState(false),
    [fullscreenError, setFullscreenError] = useState(false);
  useEffect(() => {
    const sync = () => setFull(document.fullscreenElement === player.current);
    document.addEventListener("fullscreenchange", sync);
    return () => document.removeEventListener("fullscreenchange", sync);
  }, []);
  const liveDesk = mode === "live" && (!!controls || !!onExpand);
  const showRetry = retry && (state.kind === "error" || state.kind === "idle");
  return (
    <section
      ref={player}
      className="ws-panel ws-player"
      aria-label="Видеоплеер"
    >
      <header className="ws-panel-head">
        <div>
          <h2 tabIndex={-1} ref={headingRef}>
            {title}
          </h2>
          <p>
            {description ||
              (mode === "live" ? "Эфир" : "Основной поток · Архив")}
          </p>
        </div>
        {onQuality && (
          <label className="ws-stream-choice" title="Качество эфира">
            <svg aria-hidden="true">
              <use href={icons + "#i-settings-2"} />
            </svg>
            <select
              aria-label="Качество эфира"
              value={quality}
              onChange={(event) => onQuality(event.target.value as Quality)}
            >
              <option value="auto">Автоматический выбор потока</option>
              <option value="secondary">Дополнительный поток</option>
              <option value="main">Основной поток</option>
            </select>
          </label>
        )}
        {onExpand && (
          <IconButton
            icon={expanded ? "collapse" : "expand"}
            label={expanded ? "Вернуться к сетке" : "Развернуть камеру"}
            onClick={onExpand}
          />
        )}
        {!onExpand && mode === "live" && document.fullscreenEnabled && (
          <IconButton
            icon={full ? "collapse" : "expand"}
            label={full ? "Выйти из полного экрана" : "На весь экран"}
            aria-pressed={full}
            onClick={() => {
              setFullscreenError(false);
              void (
                full
                  ? document.exitFullscreen()
                  : player.current?.requestFullscreen()
              )?.catch(() => setFullscreenError(true));
            }}
          />
        )}
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
          controls={state.kind === "ready" && mode === "archive"}
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
          <span className={onExpand ? "mw-connected" : "ws-tag"}>
            {liveDesk ? (
              <span role="status">{state.message}</span>
            ) : mode === "live" ? (
              "Прямой эфир"
            ) : (
              "Архив"
            )}
          </span>
        )}
      </div>
      {fullscreenError && (
        <p role="alert">Браузер не разрешил полноэкранный просмотр.</p>
      )}
      {controls}
      {((state.kind === "ready" && !liveDesk) ||
        position ||
        marker ||
        showRetry ||
        back) && (
        <div className="ws-transport">
          {state.kind === "ready" && !liveDesk && (
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
