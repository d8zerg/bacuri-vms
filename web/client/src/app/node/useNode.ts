import { useCallback, useEffect, useRef, useState } from "react";
import type { components } from "../../../../../build/contracts/api";
import { api, data, HttpFailure } from "../../adapters/api";
import { errorCode, type ErrorCode } from "../../adapters/errors";

type Schema = components["schemas"];
export type Settings = Schema["NodeAccelerationSettings"];
export type Selection = Schema["NodeAccelerationSelection"];
export type Probe = Schema["AccelerationProbeStatus"];
export type Task = Schema["AccelerationTask"];
export const tasks: Task[] = [
  "live_mask",
  "live_profile",
  "live_analysis",
  "export",
  "archive_analysis",
  "keyframe",
  "thumbnail",
];
const settingsRoute = "/sites/{siteId}/nodes/{nodeId}/acceleration";
const probeRoute = "/sites/{siteId}/nodes/{nodeId}/acceleration/probe";
export const running = (probe: Probe | null) =>
  !!probe && ["queued", "running", "waiting"].includes(probe.job.state);
function selection(value: Settings): Selection {
  return value.mode === "manual"
    ? { mode: "manual", gpu_tasks: value.gpu_tasks }
    : { mode: value.mode };
}
export function useNode(
  siteId: string,
  nodeId: string,
  csrf: string,
  expire: () => void,
) {
  const [saved, setSaved] = useState<Settings | null>(null);
  const [draft, setDraft] = useState<Selection | null>(null);
  const [probe, setProbe] = useState<Probe | null>(null);
  const [prepared, setPrepared] = useState<string | null>(null);
  const [preview, setPreview] = useState<{
    key: string;
    settings: Settings;
  } | null>(null);
  const [previewError, setPreviewError] = useState<{
    key: string;
    code: ErrorCode;
  } | null>(null);
  const [probeError, setProbeError] = useState<ErrorCode | null>(null);
  const [error, setError] = useState<ErrorCode | null>(null);
  const [busy, setBusy] = useState(false);
  const [refresh, setRefresh] = useState(0);
  const lifetime = useRef<AbortController | null>(null);
  const writing = useRef(false);
  const epoch = useRef(0);
  const failure = useCallback(
    (err: unknown) => {
      if (
        err instanceof HttpFailure &&
        (err.status === 401 || err.status === 403)
      )
        expire();
      return errorCode(err);
    },
    [expire],
  );

  useEffect(() => {
    const ctl = new AbortController();
    lifetime.current = ctl;
    let timer: ReturnType<typeof setTimeout>;
    async function poll() {
      const version = epoch.current;
      let next: Probe | null = null;
      try {
        next = data(
          await api.GET(probeRoute, {
            params: { path: { siteId, nodeId } },
            signal: AbortSignal.any([ctl.signal, AbortSignal.timeout(6000)]),
          }),
        );
        if (!ctl.signal.aborted && epoch.current === version) {
          setProbe(next);
          setProbeError(null);
        }
      } catch (err) {
        if (!ctl.signal.aborted && epoch.current === version) {
          setProbe(null);
          setProbeError(failure(err));
        }
      } finally {
        if (!ctl.signal.aborted)
          timer = setTimeout(() => void poll(), running(next) ? 800 : 2500);
      }
    }
    void poll();
    return () => {
      ctl.abort();
      clearTimeout(timer);
    };
  }, [siteId, nodeId, failure]);
  useEffect(() => {
    const ctl = new AbortController();
    void api
      .GET(settingsRoute, {
        params: { path: { siteId, nodeId } },
        signal: AbortSignal.any([ctl.signal, AbortSignal.timeout(6000)]),
      })
      .then(data)
      .then((value) => {
        if (!ctl.signal.aborted) setSaved(value);
      })
      .catch((err: unknown) => {
        if (!ctl.signal.aborted) setError(failure(err));
      });
    return () => ctl.abort();
  }, [siteId, nodeId, refresh, failure]);

  const candidate = draft ?? (saved && selection(saved));
  const dirty =
    !!saved &&
    !!candidate &&
    JSON.stringify(selection(saved)) !== JSON.stringify(candidate);
  const leavingCPU =
    saved?.mode === "cpu_only" && candidate?.mode !== "cpu_only";
  const verified =
    !leavingCPU ||
    (!!probe && probe.job.id === prepared && probe.job.state === "completed");
  const fresh = !!probe?.result && !probe.stale;
  const body = JSON.stringify(candidate);
  const key = JSON.stringify([
    body,
    saved?.revision,
    probe?.result?.measured_at,
    refresh,
  ]);
  const canPreview =
    !!saved && candidate?.mode !== "cpu_only" && verified && fresh;
  useEffect(() => {
    if (!canPreview || !body) return;
    const ctl = new AbortController();
    void api
      .POST("/sites/{siteId}/nodes/{nodeId}/acceleration/preview", {
        params: { path: { siteId, nodeId }, header: { "X-CSRF-Token": csrf } },
        body: JSON.parse(body) as Selection,
        signal: AbortSignal.any([ctl.signal, AbortSignal.timeout(6000)]),
      })
      .then(data)
      .then((settings) => {
        if (!ctl.signal.aborted) {
          setPreview({ key, settings });
          setPreviewError(null);
        }
      })
      .catch((err: unknown) => {
        if (!ctl.signal.aborted) {
          setPreview(null);
          setPreviewError({ key, code: failure(err) });
        }
      });
    return () => ctl.abort();
  }, [body, canPreview, key, siteId, nodeId, csrf, failure]);
  const assessment =
    canPreview && preview?.key === key ? preview.settings : null;
  const allowed =
    dirty &&
    !busy &&
    (candidate?.mode === "cpu_only" ||
      (!!assessment?.applicable && !running(probe)));
  async function command(fn: (signal: AbortSignal) => Promise<void>) {
    const ctl = lifetime.current;
    if (!ctl || ctl.signal.aborted || writing.current) return;
    writing.current = true;
    setBusy(true);
    setError(null);
    ++epoch.current;
    try {
      await fn(AbortSignal.any([ctl.signal, AbortSignal.timeout(8000)]));
    } catch (err) {
      if (!ctl.signal.aborted) setError(failure(err));
    } finally {
      ++epoch.current;
      writing.current = false;
      if (!ctl.signal.aborted) setBusy(false);
    }
  }
  function check(prepare: boolean) {
    if (prepare) setPrepared(null);
    return command(async (signal) => {
      const next = data(
        await api.POST(probeRoute, {
          params: {
            path: { siteId, nodeId },
            header: { "X-CSRF-Token": csrf },
          },
          body: { reason: prepare ? "prepare_gpu" : "administrator" },
          signal,
        }),
      );
      signal.throwIfAborted();
      setProbe(next);
      setProbeError(null);
      if (prepare) setPrepared(next.job.id);
    });
  }
  function choose(mode: Settings["mode"]) {
    if (mode === candidate?.mode || writing.current) return;
    setDraft(
      mode === "manual"
        ? { mode, gpu_tasks: saved?.mode === "manual" ? saved.gpu_tasks : [] }
        : { mode },
    );
    setError(null);
    if (mode === "cpu_only") setPrepared(null);
    else if (
      saved?.mode === "cpu_only" &&
      (!prepared || candidate?.mode === "cpu_only")
    )
      void check(true);
  }
  function toggle(task: Task) {
    if (candidate?.mode !== "manual") return;
    const chosen = new Set(candidate.gpu_tasks);
    if (chosen.has(task)) chosen.delete(task);
    else chosen.add(task);
    setDraft({ mode: "manual", gpu_tasks: tasks.filter((t) => chosen.has(t)) });
    setError(null);
  }
  function cancel() {
    if (!probe) return;
    return command(async (signal) => {
      const next = data(
        await api.DELETE(
          "/sites/{siteId}/nodes/{nodeId}/acceleration/probe/{probeId}",
          {
            params: {
              path: { siteId, nodeId, probeId: probe.job.id },
              header: { "X-CSRF-Token": csrf },
            },
            signal,
          },
        ),
      );
      signal.throwIfAborted();
      setProbe(next);
    });
  }
  function apply() {
    if (!saved || !candidate || !allowed) return;
    return command(async (signal) => {
      const args = {
        params: {
          path: { siteId, nodeId },
          header: {
            "X-CSRF-Token": csrf,
            "If-Match": JSON.stringify(String(saved.revision)),
          },
        },
        body: candidate,
        signal,
      };
      const response = await api.PUT(settingsRoute, args);
      if (response.response.status === 412) {
        const latest = data(
          await api.GET(settingsRoute, {
            params: { path: { siteId, nodeId } },
            signal,
          }),
        );
        signal.throwIfAborted();
        setSaved(latest);
      }
      const committed = data(response);
      signal.throwIfAborted();
      setSaved(committed);
      setDraft(null);
      setPrepared(null);
    });
  }
  return {
    saved,
    candidate,
    probe,
    dirty,
    busy,
    verified,
    fresh,
    allowed,
    assessment,
    error:
      error ??
      (canPreview && previewError?.key === key ? previewError.code : null),
    probeError,
    choose,
    toggle,
    cancel,
    apply,
    check: () => check(!!leavingCPU),
    reload: () => {
      setError(null);
      setRefresh((n) => n + 1);
    },
  };
}
