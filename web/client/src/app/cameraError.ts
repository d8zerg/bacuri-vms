import { HttpFailure } from "../adapters/api";
import { t, type MessageKey } from "../i18n";

const failures: Record<string, MessageKey> = {
  revision_conflict: "camera.error.revision_conflict",
  camera_image_unsupported: "camera.error.camera_image_unsupported",
  camera_image_unconfirmed: "camera.error.camera_image_unconfirmed",
  camera_batch_limit: "camera.error.camera_batch_limit",
  camera_authentication_failed: "camera.error.camera_authentication_failed",
  camera_audio_missing: "camera.error.camera_audio_missing",
  camera_audio_unavailable: "camera.error.camera_audio_unavailable",
  camera_audio_unsupported: "camera.error.camera_audio_unsupported",
  camera_ptz_busy: "camera.error.camera_ptz_busy",
  camera_command_unsent: "camera.error.camera_command_unsent",
  camera_command_rejected: "camera.error.camera_command_rejected",
  camera_ptz_unknown: "camera.error.camera_ptz_unknown",
  camera_ptz_unsupported: "camera.error.camera_ptz_unsupported",
  camera_tls_error: "camera.error.camera_tls_error",
  camera_incompatible_profiles: "camera.error.camera_incompatible_profiles",
  camera_transport_denied: "camera.error.camera_transport_denied",
  camera_protocol_error: "camera.error.camera_protocol_error",
  camera_already_registered: "camera.error.camera_already_registered",
  camera_binding_conflict: "camera.error.camera_binding_conflict",
  camera_selection_expired: "camera.error.camera_selection_expired",
  camera_discovery_unavailable: "camera.error.camera_discovery_unavailable",
  camera_unavailable: "camera.error.camera_unavailable",
};
// The explanation of a camera failure reported by the API, or nothing for other failures.
export function cameraError(error: unknown) {
  if (!(error instanceof HttpFailure)) return undefined;
  const key = failures[error.code];
  return key && t(key);
}
