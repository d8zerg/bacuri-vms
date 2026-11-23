import type { PtzCommand } from "../contracts/workspace";

export const ptzLabels: Record<PtzCommand, string> = {
  left: "Влево",
  right: "Вправо",
  up: "Вверх",
  down: "Вниз",
  zoom_in: "Приблизить",
  zoom_out: "Отдалить",
  stop: "Остановить",
  focus_near: "Фокус ближе",
  focus_far: "Фокус дальше",
  focus_stop: "Остановить фокус",
  preset_goto: "Перейти к позиции",
  tour_start: "Запустить патруль",
  tour_stop: "Остановить патруль",
  light_on: "Включить подсветку",
  light_off: "Выключить подсветку",
  wiper_on: "Включить очиститель",
  wiper_off: "Выключить очиститель",
  relay_on: "Активировать выход",
  relay_off: "Отключить выход",
};
