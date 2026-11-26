import {
  catalogMessage,
  describeError,
  errorCode,
  retryOf,
} from "../adapters/errors";
import {
  FilterPanel,
  matchesQuery,
  needsListSearch,
} from "../ui-kit/FilterPanel";
import { useEffect, useRef, useState } from "react";
import type {
  Services,
  MapManagement,
  MapSource,
  MapScene,
  MapMode,
  Placement,
} from "../contracts/workspace";
import { Button, IconButton } from "../ui-kit/FormControls";
import {
  dateTimeFormat,
  formatNumber,
  t as translate,
  useT,
  type MessageKey,
} from "../i18n";

const states = {
  paused: "packages.state.paused",
  queued: "packages.state.queued",
  downloading: "packages.state.downloading",
  retrying: "packages.state.retrying",
  installing: "packages.state.installing",
  installed: "packages.state.installed",
  failed: "packages.state.failed",
} as const satisfies Record<string, MessageKey>;
const stateKeys = Object.keys(states) as (keyof typeof states)[];
const size = (n: number) =>
  translate("packages.size", {
    value: formatNumber(n / 1048576, { maximumFractionDigits: 1 }),
  });
const date = (s: string) =>
  dateTimeFormat({ dateStyle: "short" }).format(new Date(s));
type Props = {
  site: string;
  services: Services;
  regions: MapScene["regions"];
  placements: Placement[];
};
export function MapPackages({ site, services, regions, placements }: Props) {
  const t = useT();
  const [mode, chooseMode] = useState<MapMode | null>(null);
  const [management, setManagement] = useState<MapManagement | null>(null);
  const [url, setUrl] = useState<string | null>(null),
    [source, setSource] = useState("");
  const [options, setOptions] = useState<MapSource["regions"]>([]),
    [selection, select] = useState<string[]>([]);
  const [busy, setBusy] = useState(false),
    [message, say] = useState(""),
    [tick, refresh] = useState(0);
  const operation = useRef<AbortController | null>(null);
  const trigger = useRef<HTMLButtonElement | null>(null);
  useEffect(() => () => operation.current?.abort(), []);
  useEffect(() => {
    const controller = new AbortController();
    let pending = false;
    const read = async () => {
      if (pending) return;
      pending = true;
      try {
        const result = await services.mapManagement(site, controller.signal);
        if (!controller.signal.aborted) setManagement(result);
      } catch {
        if (!controller.signal.aborted) setManagement(null);
      } finally {
        pending = false;
      }
    };
    void read();
    const timer = setInterval(() => void read(), 3000);
    return () => {
      controller.abort();
      clearInterval(timer);
    };
  }, [site, services, tick]);
  function open(next: MapMode) {
    operation.current?.abort();
    chooseMode(next);
    setBusy(false);
    setSource("");
    setQuery("");
    setOptions([]);
    select([]);
    say("");
  }
  function dismiss() {
    operation.current?.abort();
    chooseMode(null);
    setBusy(false);
    say("");
    trigger.current?.focus();
  }
  function toggle(next: MapMode, button: HTMLButtonElement) {
    trigger.current = button;
    if (mode === next) dismiss();
    else open(next);
  }
  async function browse(file?: File) {
    operation.current?.abort();
    const ctl = new AbortController();
    operation.current = ctl;
    setBusy(true);
    setQuery("");
    setOptions([]);
    select([]);
    setSource("");
    say(file ? t("packages.uploading") : t("packages.fetchingCatalog"));
    try {
      const { id } = await services.mapSource(
        site,
        file
          ? { kind: "file", bytes: file.size }
          : {
              kind: "online",
              url: (url ?? management?.catalog_url ?? "").trim(),
            },
      );
      ctl.signal.throwIfAborted();
      if (file) {
        let offset = 0;
        let failures = 0;
        while (offset < file.size) {
          const end = Math.min(file.size, offset + 1048576);
          try {
            await services.uploadMapChunk(
              site,
              id,
              offset,
              file.slice(offset, end),
              ctl.signal,
            );
            offset = end;
            failures = 0;
          } catch (error) {
            ctl.signal.throwIfAborted();
            if (
              ++failures > 3 ||
              (errorCode(error) !== "conflict" && retryOf(error) !== "later")
            )
              throw error;
            const state = await services.inspectMapSource(site, id, ctl.signal);
            offset = state.received;
          }
          say(
            t("packages.uploadProgress", {
              percent: Math.round((100 * offset) / file.size),
            }),
          );
        }
      }
      say(t("packages.readingRegions"));
      for (;;) {
        const state = await services.inspectMapSource(site, id, ctl.signal);
        ctl.signal.throwIfAborted();
        if (state.state === "failed") {
          say(
            catalogMessage(state.error, {
              catalog_unavailable: "packages.catalogFailed",
            }) ?? t(file ? "packages.fileFailed" : "packages.catalogFailed"),
          );
          return;
        }
        if (state.state === "ready") {
          setSource(id);
          setOptions(state.regions);
          say("");
          break;
        }
        await new Promise((r) => setTimeout(r, 700));
        ctl.signal.throwIfAborted();
      }
    } catch (error) {
      if (!ctl.signal.aborted)
        say(
          describeError(error, {
            internal_error: file
              ? "packages.fileFailed"
              : "packages.catalogFailed",
          }),
        );
    } finally {
      if (!ctl.signal.aborted) setBusy(false);
    }
  }
  const [jobQuery, setJobQuery] = useState("");
  const [jobState, setJobState] = useState("");
  const jobs = (management?.jobs ?? []).filter(
    (j) =>
      (!jobState || j.state === jobState) &&
      matchesQuery(jobQuery, j.name, j.region_id, t(states[j.state])),
  );
  const [query, setQuery] = useState("");
  const available = options.filter((e) => {
    const installed = regions.find((r) => r.id === e.id);
    return mode === "install"
      ? !installed
      : installed &&
          Date.parse(e.source_time) > Date.parse(installed.source_time);
  });
  const listed = available.filter((r) => matchesQuery(query, r.name, r.id));
  const selected = available.filter((e) => selection.includes(e.id));
  async function pauseDownload(id: string, paused: boolean) {
    try {
      await services.pauseMapDownload(site, id, paused);
      refresh((v) => v + 1);
    } catch (error) {
      say(describeError(error, { internal_error: "packages.stateFailed" }));
    }
  }
  async function submit() {
    if (!mode || !selected.length) return;
    setBusy(true);
    try {
      await services.queueMapRegions(site, {
        source_id: source,
        mode,
        regions: selected.map((e) => e.id),
      });
      open(mode);
      say(t("packages.queued"));
      refresh((v) => v + 1);
    } catch (error) {
      say(describeError(error, { internal_error: "packages.queueFailed" }));
    } finally {
      setBusy(false);
    }
  }
  return (
    <section className="ws-map-packages" aria-label={t("packages.label")}>
      <h3>{t("packages.title")}</h3>
      <div className="ws-map-actions">
        <Button
          aria-expanded={mode === "install"}
          disabled={busy && !!source}
          onClick={(e) => toggle("install", e.currentTarget)}
        >
          {t("packages.install")}
        </Button>
        <IconButton
          icon="file-down"
          label={t("packages.updateInstalled")}
          aria-expanded={mode === "update"}
          disabled={!regions.length || (busy && !!source)}
          onClick={(e) => toggle("update", e.currentTarget)}
        />
      </div>
      {mode && (
        <fieldset>
          <legend>
            {mode === "install"
              ? t("packages.installing")
              : t("packages.updating")}
          </legend>
          <form
            onSubmit={(e) => {
              e.preventDefault();
              void browse();
            }}
          >
            <label>
              {t("packages.catalog")}
              <input
                type="url"
                required
                pattern="https://.+"
                value={url ?? management?.catalog_url ?? ""}
                onChange={(e) => setUrl(e.target.value)}
              />
            </label>
            <Button type="submit" disabled={busy}>
              {mode === "install"
                ? t("packages.showCatalog")
                : t("packages.checkUpdates")}
            </Button>
          </form>
          <label>
            {mode === "install"
              ? t("packages.fromFile")
              : t("packages.updateFile")}
            <input
              type="file"
              accept=".zip,.mbtiles"
              disabled={busy}
              onChange={(e) => {
                const f = e.target.files?.[0];
                e.target.value = "";
                if (f) void browse(f);
              }}
            />
          </label>
          {source &&
            (available.length ? (
              <>
                {needsListSearch(available.length, query) && (
                  <FilterPanel
                    label={t("packages.filters")}
                    queryLabel={t("packages.regionName")}
                    query={query}
                    onQuery={setQuery}
                    onReset={() => setQuery("")}
                  />
                )}
                {!listed.length && (
                  <p role="status">{t("packages.noRegions")}</p>
                )}
                {!!listed.length && (
                  <label>
                    <input
                      type="checkbox"
                      checked={listed.every((r) => selection.includes(r.id))}
                      onChange={(e) => {
                        const checked = e.target.checked;
                        const shown = new Set(listed.map((r) => r.id));
                        select((prev) =>
                          checked
                            ? [...new Set([...prev, ...shown])]
                            : prev.filter((id) => !shown.has(id)),
                        );
                      }}
                    />
                    {query
                      ? t("packages.selectFound")
                      : t("packages.selectAll")}
                  </label>
                )}
                {query && !!selected.length && (
                  <p>{t("packages.selected", { count: selected.length })}</p>
                )}
                <ul>
                  {listed.map((r) => {
                    const old = regions.find((i) => i.id === r.id);
                    const inside = (bounds: number[], x: number, y: number) =>
                      x >= bounds[0]! &&
                      x <= bounds[2]! &&
                      y >= bounds[1]! &&
                      y <= bounds[3]!;
                    const outside = old
                      ? placements.filter(
                          (p) =>
                            !p.plan_id &&
                            inside(old.bounds, p.x, p.y) &&
                            !inside(r.bounds, p.x, p.y),
                        ).length
                      : 0;
                    return (
                      <li key={r.id}>
                        <label>
                          <input
                            type="checkbox"
                            checked={selection.includes(r.id)}
                            onChange={(e) =>
                              select((v) =>
                                e.target.checked
                                  ? [...v, r.id]
                                  : v.filter((id) => id !== r.id),
                              )
                            }
                          />
                          {r.name} · {date(r.source_time)} · {size(r.bytes)}
                        </label>
                        {outside > 0 && (
                          <small>
                            {t("packages.outside", { count: outside })}
                          </small>
                        )}
                      </li>
                    );
                  })}
                </ul>
                {mode === "install" ? (
                  <Button
                    disabled={busy || !selected.length}
                    onClick={() => void submit()}
                  >
                    {t("packages.installSelected")}
                  </Button>
                ) : (
                  <IconButton
                    icon="file-down"
                    label={t("packages.updateSelected")}
                    disabled={busy || !selected.length}
                    onClick={() => void submit()}
                  />
                )}
              </>
            ) : (
              <p>
                {mode === "install"
                  ? t("packages.noneAvailable")
                  : t("packages.noUpdates")}
              </p>
            ))}
          <Button disabled={busy && !!source} onClick={dismiss}>
            {busy && !source ? t("packages.abort") : t("packages.collapse")}
          </Button>
        </fieldset>
      )}
      {message && <p role="status">{message}</p>}
      {(needsListSearch(management?.jobs.length ?? 0, jobQuery) ||
        jobState) && (
        <FilterPanel
          label={t("packages.jobFilters")}
          queryLabel={t("packages.jobSearch")}
          query={jobQuery}
          onQuery={setJobQuery}
          filtered={!!jobState}
          onReset={() => {
            setJobQuery("");
            setJobState("");
          }}
        >
          <label>
            {t("packages.jobState")}
            <select
              value={jobState}
              onChange={(e) => setJobState(e.target.value)}
            >
              <option value="">{t("batch.allStates")}</option>
              {stateKeys.map((value) => (
                <option key={value} value={value}>
                  {t(states[value])}
                </option>
              ))}
            </select>
          </label>
        </FilterPanel>
      )}
      {(jobQuery || jobState) && !jobs.length && (
        <p role="status">{t("packages.noJobs")}</p>
      )}
      {jobs.length > 0 && (
        <ul aria-label={t("packages.states")}>
          {jobs.map((j) => (
            <li key={j.id}>
              {j.name} · {t(states[j.state])}
              {(j.state === "failed" || j.state === "retrying") &&
                j.error &&
                " · " + (catalogMessage(j.error) ?? t("packages.stateFailed"))}
              {["paused", "retrying", "downloading"].includes(j.state) &&
                " · " + Math.floor((j.received * 100) / j.bytes) + "%"}
              {j.online &&
                ["queued", "downloading", "retrying", "paused"].includes(
                  j.state,
                ) && (
                  <Button
                    type="button"
                    onClick={() =>
                      void pauseDownload(j.id, j.state !== "paused")
                    }
                  >
                    {j.state === "paused"
                      ? t("packages.resume")
                      : t("packages.pause")}
                  </Button>
                )}
              {j.state === "failed" && (
                <Button
                  type="button"
                  onClick={() => {
                    void (async () => {
                      try {
                        await services.queueMapRegions(site, {
                          source_id: j.source_id,
                          mode: j.mode,
                          regions: [j.region_id],
                        });
                        refresh((v) => v + 1);
                      } catch (error) {
                        say(
                          describeError(error, {
                            internal_error: "packages.reopen",
                          }),
                        );
                      }
                    })();
                  }}
                >
                  {t("action.retry")}
                </Button>
              )}
            </li>
          ))}
        </ul>
      )}
    </section>
  );
}
