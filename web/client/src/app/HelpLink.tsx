import type publicMessages from "../../../../build/contracts/help-messages.json";
export type PublicHelpCode = keyof typeof publicMessages;
import { createContext, useContext, useLayoutEffect } from "react";
import { useLanguage, useT } from "../i18n";

// The portal only receives display preferences and public documentation identifiers.
export const HelpTheme = createContext<"system" | "light" | "dark">("system");
export type HelpTopic =
  | "start-monitoring"
  | "ptz-control"
  | "review-alarm"
  | "camera-on-map"
  | "node-acceleration"
  | "connect-camera"
  | "investigate-export"
  | "access-privacy"
  | "recording-policy"
  | "recording-missing"
  | "sign-in"
  | "connection-lost"
  | "apply-camera-batch"
  | "users-and-groups"
  | "privacy-masks"
  | "compare-recordings"
  | "map-placement"
  | "activity-journal"
  | "archive-playback"
  | "video-rules";
export const ScreenHelp = createContext<
  ((topic: HelpTopic | undefined) => void) | undefined
>(undefined);
export function useScreenHelp(topic: HelpTopic) {
  const setTopic = useContext(ScreenHelp);
  useLayoutEffect(() => {
    setTopic?.(topic);
    return () => setTopic?.(undefined);
  }, [setTopic, topic]);
}
export function screenHelpTopic(
  path: string,
  params: URLSearchParams,
): HelpTopic {
  switch (path) {
    case "/cameras":
      switch (params.get("setup")) {
        case "recording":
          return "recording-policy";
        case "batch":
          return "apply-camera-batch";
        case "rules":
          return "video-rules";
        default:
          return "connect-camera";
      }
    case "/node":
      return "node-acceleration";
    case "/maps":
      return "camera-on-map";
    case "/audit":
      return "activity-journal";
    case "/access":
      return "access-privacy";
    case "/investigations":
      return "investigate-export";
    default:
      if (params.has("alert")) return "review-alarm";
      return params.get("mode") === "archive"
        ? "archive-playback"
        : "start-monitoring";
  }
}

export function useHelpHref(article?: HelpTopic, code?: PublicHelpCode) {
  const language = useLanguage();
  const theme = useContext(HelpTheme);
  const params = new URLSearchParams({ lang: language, theme });
  if (code) params.set("code", code);
  else if (article) params.set("article", article);
  return "/help#" + params.toString();
}

export function HelpIcon() {
  return (
    <svg
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.7"
      aria-hidden="true"
    >
      <circle cx="12" cy="12" r="9" />
      <path d="M9.4 9a2.7 2.7 0 0 1 5.3.6c0 2-2.7 2.1-2.7 4.4M12 16v1" />
    </svg>
  );
}

export function HelpLink({
  article,
  code,
  iconOnly = false,
  sidebar = false,
}: {
  article?: HelpTopic | undefined;
  code?: PublicHelpCode | undefined;
  iconOnly?: boolean;
  sidebar?: boolean;
}) {
  const href = useHelpHref(article, code);
  const t = useT();
  return (
    <a
      className={iconOnly ? "ws-help-link bk-toolbar-icon" : "ws-help-link"}
      href={href}
      target="_blank"
      rel="noopener noreferrer"
      aria-label={t("nav.help")}
      title={t("nav.help")}
    >
      {(iconOnly || sidebar) && <HelpIcon />}
      {!iconOnly && <span>{t("nav.help")}</span>}
    </a>
  );
}
