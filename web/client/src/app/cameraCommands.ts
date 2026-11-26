import type { PtzCommand } from "../contracts/workspace";
import { t, type MessageKey } from "../i18n";

const commandKeys: Record<PtzCommand, MessageKey> = {
  left: "ptz.left",
  right: "ptz.right",
  up: "ptz.up",
  down: "ptz.down",
  zoom_in: "ptz.zoom_in",
  zoom_out: "ptz.zoom_out",
  stop: "ptz.stop",
  focus_near: "ptz.focus_near",
  focus_far: "ptz.focus_far",
  focus_stop: "ptz.focus_stop",
  preset_goto: "ptz.preset_goto",
  tour_start: "ptz.tour_start",
  tour_stop: "ptz.tour_stop",
  light_on: "ptz.light_on",
  light_off: "ptz.light_off",
  wiper_on: "ptz.wiper_on",
  wiper_off: "ptz.wiper_off",
  relay_on: "ptz.relay_on",
  relay_off: "ptz.relay_off",
};
export function ptzLabel(command: PtzCommand): string;
export function ptzLabel(command: string): string | undefined;
// The name of a camera command, or nothing for a command this client does not know.
export function ptzLabel(command: string) {
  const key = commandKeys[command as PtzCommand];
  return key && t(key);
}
