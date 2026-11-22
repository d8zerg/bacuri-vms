export const alertStates = {
  new: "Новая",
  in_progress: "В работе",
  closed: "Закрыта",
} as const;
export function alertName(type: string) {
  return type === "motion" ? "Движение" : type;
}
