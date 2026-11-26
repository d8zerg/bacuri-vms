import { HttpFailure } from "../adapters/api";
import { describeError, errorCode } from "../adapters/errors";

// The explanation of a failure reported by the API, or nothing when the API named no known cause.
export function cameraError(error: unknown) {
  if (!(error instanceof HttpFailure) || errorCode(error) !== error.code)
    return undefined;
  return describeError(error, {
    revision_conflict: "camera.error.revision_conflict",
    precondition_failed: "camera.error.revision_conflict",
  });
}
