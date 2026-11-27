export function cameraSetup(requested: string | null) {
  return requested === "manual" ||
    requested === "onvif" ||
    requested === "rules" ||
    requested === "recording" ||
    requested === "batch"
    ? requested
    : null;
}
