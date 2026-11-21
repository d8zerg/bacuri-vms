import type { Interval } from "../contracts/workspace";
import { pause } from "../workflows/pause";
import { api, data } from "./api";
// A search span can end inside a block. The playback manifest identifies complete, deliverable blocks.
export async function newRecording(
  siteId: string,
  pointId: string,
  header: { "X-CSRF-Token": string },
  signal: AbortSignal,
  binding?: string,
): Promise<Interval> {
  const start = new Date().toISOString();
  const deadline = AbortSignal.any([signal, AbortSignal.timeout(35000)]);
  for (;;) {
    await pause(500, deadline);
    const res = await api.POST(
      "/sites/{siteId}/points/{pointId}/media-sessions",
      {
        params: {
          path: { siteId, pointId },
          header: { ...header, "Idempotency-Key": crypto.randomUUID() },
        },
        body: {
          mode: "archive",
          ...(binding ? { binding_revision: binding } : {}),
          interval: { start, end: new Date().toISOString() },
        },
        signal: AbortSignal.timeout(3000),
      },
    );
    if (res.response.status === 409) continue;
    const path = { mediaSessionId: data(res).id };
    try {
      deadline.throwIfAborted();
      const manifest = data(
        await api.GET("/media-sessions/{mediaSessionId}/manifest", {
          params: { path },
          signal: deadline,
        }),
      );
      const first = manifest.parts[0];
      if (first) return first.interval;
    } finally {
      await api
        .DELETE("/media-sessions/{mediaSessionId}", {
          params: { path, header },
          signal: AbortSignal.timeout(2000),
        })
        .catch(() => {});
    }
  }
}
