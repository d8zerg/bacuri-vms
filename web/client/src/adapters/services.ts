import type {
  Camera,
  CameraDraft,
  Entry,
  Services,
} from "../contracts/workspace";
import { newRecording } from "./recording";
import { pause } from "../workflows/pause";
import { api, data, HttpFailure } from "./api";
import { subscribe } from "./feed";
import { attach, mediaFailure } from "./video";
type Registration = {
  key: string;
  initial: CameraDraft;
  created?: Camera;
  revision?: string;
};
export function createServices(): Services {
  let csrf = "";
  const operations = new WeakMap<object, Registration>();
  const auth = () => ({ "X-CSRF-Token": csrf });
  return {
    async session(signal) {
      const identity = data(await api.GET("/session", { signal }));
      signal.throwIfAborted();
      csrf = identity.csrf_token;
      return identity;
    },
    async login(username, password, enrollment) {
      const body = {
        username,
        password,
        ...(enrollment ? { enrollment_code: enrollment } : {}),
      };
      for (let attempt = 0; attempt < 2; attempt++) {
        const stage = data(await api.POST("/session", { body }));
        if ("csrf_token" in stage) {
          csrf = stage.csrf_token;
          return stage;
        }
        const { factorAnswer } = await import("/webauthn.js");
        const verified = await api.POST("/session/webauthn", {
          body: await factorAnswer(stage, username),
        });
        if (verified.response.status === 204 && stage.kind === "create")
          continue;
        const identity = data(verified);
        if (!identity) throw new HttpFailure(403, "invalid_factor");
        csrf = identity.csrf_token;
        return identity;
      }
      throw new HttpFailure(403, "invalid_factor");
    },
    async logout() {
      const { response } = await api.DELETE("/session", {
        params: { header: auth() },
      });
      if (!response.ok && response.status !== 401)
        throw Error(String(response.status));
      csrf = "";
    },
    async exportFragment(siteId, pointId, interval, alert, signal) {
      return data(
        await api.POST("/sites/{siteId}/points/{pointId}/exports", {
          params: { path: { siteId, pointId }, header: auth() },
          body: { interval, ...(alert ? { alert_id: alert } : {}) },
          parseAs: "blob",
          signal,
        }),
      );
    },
    async alertWork(siteId, alertId, signal) {
      return data(
        await api.GET("/sites/{siteId}/alerts/{alertId}/work", {
          params: { path: { siteId, alertId } },
          signal,
        }),
      );
    },
    async alertCommand(siteId, alertId, body, key) {
      return data(
        await api.POST("/sites/{siteId}/alerts/{alertId}/work", {
          params: {
            path: { siteId, alertId },
            header: { ...auth(), "Idempotency-Key": key },
          },
          body,
        }),
      );
    },
    async audit(siteId, body, signal) {
      return data(
        await api.POST("/sites/{siteId}/alert-audit/search", {
          params: { path: { siteId }, header: auth() },
          body,
          signal,
        }),
      );
    },
    async delegate(siteId, body) {
      return data(
        await api.POST("/sites/{siteId}/alert-delegations", {
          params: { path: { siteId }, header: auth() },
          body,
        }),
      );
    },
    async revokeDelegation(siteId, delegationId) {
      const result = await api.DELETE(
        "/sites/{siteId}/alert-delegations/{delegationId}",
        { params: { path: { siteId, delegationId }, header: auth() } },
      );
      if (!result.response.ok)
        throw new HttpFailure(result.response.status, result.error?.code ?? "");
    },
    async maps(siteId, signal) {
      return data(
        await api.GET("/sites/{siteId}/maps", {
          params: { path: { siteId } },
          signal,
        }),
      );
    },
    async previewPlan(siteId, body) {
      return data(
        await api.POST("/sites/{siteId}/maps/georeference/preview", {
          params: { path: { siteId }, header: auth() },
          body,
        }),
      );
    },
    async linkPlan(siteId, body) {
      const { response } = await api.PUT("/sites/{siteId}/maps/georeference", {
        params: { path: { siteId }, header: auth() },
        body,
      });
      if (!response.ok) throw Error(String(response.status));
    },
    async calibrate(siteId, body) {
      const { response } = await api.PUT("/sites/{siteId}/maps/calibration", {
        params: { path: { siteId }, header: auth() },
        body,
      });
      if (!response.ok) throw Error(String(response.status));
    },
    async place(siteId, body) {
      const { response } = await api.PUT("/sites/{siteId}/maps/placement", {
        params: { path: { siteId }, header: auth() },
        body,
      });
      if (!response.ok) throw Error(String(response.status));
    },
    async mapManagement(siteId, signal) {
      return data(
        await api.GET("/sites/{siteId}/maps/management", {
          params: { path: { siteId } },
          signal,
        }),
      );
    },
    async mapSource(siteId, body) {
      return data(
        await api.POST("/sites/{siteId}/maps/sources", {
          params: { path: { siteId }, header: auth() },
          body,
        }),
      );
    },
    async inspectMapSource(siteId, sourceId, signal) {
      return data(
        await api.GET("/sites/{siteId}/maps/sources/{sourceId}", {
          params: { path: { siteId, sourceId } },
          signal,
        }),
      );
    },
    async uploadMapChunk(site, id, offset, chunk, signal) {
      const response = await fetch(
        `/api/v1/sites/${site}/maps/sources/${id}/chunks`,
        {
          method: "PUT",
          credentials: "same-origin",
          headers: {
            ...auth(),
            "Content-Type": "application/octet-stream",
            "X-Upload-Offset": String(offset),
          },
          body: chunk,
          signal,
        },
      );
      if (!response.ok)
        throw new HttpFailure(response.status, "map_upload_failed");
    },
    async pauseMapDownload(siteId, jobId, paused) {
      const { response } = await api.PUT(
        "/sites/{siteId}/maps/jobs/{jobId}/download",
        {
          params: { path: { siteId, jobId }, header: auth() },
          body: { paused },
        },
      );
      if (!response.ok)
        throw new HttpFailure(response.status, "download_unavailable");
    },
    async queueMapRegions(siteId, body) {
      const { response } = await api.POST("/sites/{siteId}/maps/jobs", {
        params: { path: { siteId }, header: auth() },
        body,
      });
      if (!response.ok)
        throw new HttpFailure(response.status, "map_selection_failed");
    },
    async addPlan(siteId, name, png) {
      return data(
        await api.POST("/sites/{siteId}/maps/plans", {
          params: { path: { siteId }, header: auth() },
          body: { name, png },
        }),
      );
    },
    async points(siteId, signal) {
      return data(
        await api.GET("/sites/{siteId}/points", {
          params: { path: { siteId } },
          signal,
        }),
      );
    },
    async audio(siteId, pointId, body, signal) {
      return data(
        await api.POST("/sites/{siteId}/points/{pointId}/audio", {
          params: { path: { siteId, pointId }, header: auth() },
          body,
          ...(signal && { signal }),
        }),
      );
    },
    async cameraControls(siteId, pointId, signal) {
      return data(
        await api.GET("/sites/{siteId}/points/{pointId}/ptz", {
          params: { path: { siteId, pointId } },
          signal,
        }),
      );
    },
    async ptz(siteId, pointId, device, command, target) {
      const { response, error } = await api.POST(
        "/sites/{siteId}/points/{pointId}/ptz",
        {
          params: {
            path: { siteId, pointId },
            header: { ...auth(), "Idempotency-Key": crypto.randomUUID() },
          },
          body: { device_id: device, command, ...(target && { target }) },
        },
      );
      if (!response.ok)
        throw new HttpFailure(response.status, error?.code ?? "");
    },
    async discover(siteId, signal) {
      return data(
        await api.POST("/sites/{siteId}/onvif/discovery", {
          params: { path: { siteId }, header: auth() },
          signal,
        }),
      );
    },
    async inspect(siteId, selection, username, password, signal) {
      return data(
        await api.POST("/sites/{siteId}/onvif/profiles", {
          params: { path: { siteId }, header: auth() },
          body: { selection, username, password },
          signal,
        }),
      );
    },
    async refreshOnvif(siteId, camera, draft, signal) {
      const path = { siteId, cameraId: camera.id };
      const current = await api.GET("/sites/{siteId}/cameras/{cameraId}", {
        params: { path },
        signal,
      });
      const etag = current.response.headers.get("ETag");
      if (data(current).desired_revision !== camera.desired_revision || !etag)
        throw new HttpFailure(409, "camera_binding_conflict");
      return data(
        await api.PUT("/sites/{siteId}/cameras/{cameraId}", {
          params: { path, header: { ...auth(), "If-Match": etag } },
          body: draft,
          signal,
        }),
      );
    },
    async cameras(siteId, signal) {
      return data(
        await api.GET("/sites/{siteId}/cameras", {
          params: { path: { siteId } },
          signal,
        }),
      );
    },
    async previousRecording(siteId, cameraId, signal) {
      return data(
        await api.GET("/sites/{siteId}/cameras/{cameraId}/previous-recording", {
          params: { path: { siteId, cameraId } },
          signal,
        }),
      );
    },
    async register(siteId, draft, signal, form) {
      const op: Registration = operations.get(form) ?? {
        key: crypto.randomUUID(),
        initial: draft,
      };
      operations.set(form, op);
      if (!op.created) {
        const response = await api.POST("/sites/{siteId}/cameras", {
          params: {
            path: { siteId },
            header: { ...auth(), "Idempotency-Key": op.key },
          },
          body: op.initial,
          signal,
        });
        // A definitive rejection permits a corrected draft on the next submission.
        if (response.response.status >= 400 && response.response.status < 500)
          operations.delete(form);
        op.created = data(response);
        op.revision = op.created.desired_revision;
      }
      const path = { siteId, cameraId: op.created.id };
      const deadline = AbortSignal.any([signal, AbortSignal.timeout(30000)]);
      const read = () =>
        api.GET("/sites/{siteId}/cameras/{cameraId}", {
          params: { path },
          signal: deadline,
        });
      const current = await read();
      let cam = data(current);
      if (
        draft.name !== cam.name ||
        draft.main_url !== cam.main_url ||
        draft.secondary_url !== cam.secondary_url ||
        cam.state === "rejected"
      ) {
        const etag = current.response.headers.get("ETag");
        if (!etag || cam.desired_revision !== op.revision)
          throw Error("Configuration changed");
        const update = { ...draft };
        delete update.replace_device_id;
        if (
          update.main_url === cam.main_url &&
          update.secondary_url === cam.secondary_url
        )
          delete update.onvif_selection;
        cam = data(
          await api.PUT("/sites/{siteId}/cameras/{cameraId}", {
            params: { path, header: { ...auth(), "If-Match": etag } },
            body: update,
            signal: deadline,
          }),
        );
      }
      op.revision = cam.desired_revision;
      while (cam.state === "applying" && cam.desired_revision === op.revision) {
        await pause(250, deadline);
        cam = data(await read());
      }
      if (cam.desired_revision !== op.revision)
        throw Error("Configuration changed");
      return cam;
    },
    record: (site, point, signal, binding) =>
      newRecording(site, point, auth(), signal, binding),
    async checkProfiles(site, camera, signal) {
      const probe = new AbortController();
      const video = document.createElement("video");
      video.muted = true;
      video.playsInline = true;
      const liveSignal = AbortSignal.any([signal, probe.signal]);
      const [main, secondary] = await Promise.allSettled([
        newRecording(
          site,
          camera.point_id,
          auth(),
          signal,
          camera.binding_revision,
        ),
        attach(
          video,
          site,
          { mode: "live", point: camera.point_id },
          auth(),
          liveSignal,
          () => probe.abort(),
        ).finally(() => probe.abort()),
      ]);
      signal.throwIfAborted();
      const current = data(
        await api.GET("/sites/{siteId}/cameras/{cameraId}", {
          params: { path: { siteId: site, cameraId: camera.id } },
          signal,
        }),
      );
      if (
        current.applied_revision !== camera.applied_revision ||
        current.state !== "applied"
      )
        throw Error("Configuration changed");
      const issues =
        main.status === "rejected" || secondary.status === "rejected"
          ? (
              await api.GET(
                "/sites/{siteId}/cameras/{cameraId}/stream-issues",
                {
                  params: { path: { siteId: site, cameraId: camera.id } },
                  signal,
                },
              )
            ).data
          : undefined;
      return {
        main:
          main.status === "fulfilled"
            ? "available"
            : (issues?.main ?? "unknown"),
        secondary:
          secondary.status === "fulfilled"
            ? "available"
            : (issues?.secondary ?? "unknown"),
      };
    },
    async archive(siteId, pointId, window, signal) {
      const rows: Entry[] = [];
      let cursor: string | undefined;
      do {
        const page = data(
          await api.GET("/sites/{siteId}/points/{pointId}/archive", {
            params: {
              path: { siteId, pointId },
              query: { ...window, ...(cursor ? { cursor } : {}) },
            },
            signal,
          }),
        );
        rows.push(...page.items);
        cursor = page.next_cursor;
      } while (cursor);
      return rows;
    },
    async resolve(siteId, alertId, signal) {
      return data(
        await api.GET("/sites/{siteId}/alerts/{alertId}/archive", {
          params: { path: { siteId, alertId } },
          signal,
        }),
      );
    },
    subscribe,
    async attach(video, site, target, signal, fail) {
      try {
        await attach(video, site, target, auth(), signal, fail);
      } catch (error) {
        if (!signal.aborted) fail(mediaFailure(error));
        throw error;
      }
    },
  };
}
