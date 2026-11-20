import { useEffect, useRef, useState } from "react";
import type {
  CameraCheckResult,
  CameraCheckState,
  CameraConnectionFormProps,
  CameraDraft,
} from "../ui-kit/CameraConnectionForm";

export type CheckCameraStreams = (
  draft: CameraDraft,
  signal: AbortSignal,
) => Promise<CameraCheckResult>;

export function useCameraConnectionCheck(
  initial: CameraDraft,
  check: CheckCameraStreams,
  enabled = true,
): CameraConnectionFormProps {
  const [value, setValue] = useState(initial);
  const [state, setState] = useState<CameraCheckState>({ status: "idle" });
  const active = useRef<AbortController | null>(null);
  useEffect(() => () => active.current?.abort(), []);

  useEffect(() => {
    if (!enabled) {
      active.current?.abort();
      active.current = null;
      setState({ status: "idle" });
    }
  }, [enabled]);
  function change(next: CameraDraft) {
    active.current?.abort();
    active.current = null;
    setValue(next);
    setState({ status: "idle" });
  }

  async function run() {
    if (!enabled || active.current) return;
    const request = new AbortController();
    active.current = request;
    setState({ status: "checking" });
    let result: CameraCheckResult;
    try {
      result = await check(value, request.signal);
    } catch {
      result = { status: "error", reason: "unknown" };
    }
    if (request.signal.aborted) return;
    active.current = null;
    setState(result);
  }
  return {
    value,
    state,
    onChange: change,
    onCheck: () => {
      void run();
    },
  };
}
