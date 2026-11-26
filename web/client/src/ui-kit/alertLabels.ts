import { t, type MessageKey } from "../i18n";

// Alert work states and the catalog keys of their names.
export const alertStates = {
  new: "alert.state.new",
  in_progress: "alert.state.inProgress",
  closed: "alert.state.closed",
} as const satisfies Record<string, MessageKey>;
export function alertStateLabel(state: keyof typeof alertStates) {
  return t(alertStates[state]);
}
export function alertName(type: string) {
  return type === "motion" ? t("alert.type.motion") : type;
}
