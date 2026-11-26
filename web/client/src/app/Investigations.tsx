import "../i18n/extended";
import { useResource } from "../adapters/resource";
import { useEffect, useRef, useState } from "react";
import type {
  FloorPlan,
  SearchTrack,
  VideoSearch,
} from "../contracts/workspace";
import {
  FilterPanel,
  FilterSegments,
  matchesQuery,
} from "../ui-kit/FilterPanel";
import { Button, IconButton } from "../ui-kit/FormControls";
import { SubsectionMenu } from "./SubsectionMenu";

import { localDateTime, Preview, RegionField } from "./ArchiveFrame";
import "./investigations.css";
import { useT } from "../i18n";
import { Cases } from "./investigations/Cases";
import { Track } from "./investigations/Track";
import {
  clock,
  entire,
  iso,
  type Material,
  type Props,
  type Region,
} from "./investigations/shared";

export default function Investigations({
  services,
  site,
  points,
  user,
}: Props) {
  const t = useT();
  const [tab, setTab] = useState("search"),
    [query, setQuery] = useState(""),
    [selected, setSelected] = useState<string[]>([]),
    [from, setFrom] = useState(() => localDateTime(Date.now() - 300000)),
    [to, setTo] = useState(() => localDateTime(Date.now()));
  const [motion, setMotion] = useState(true),
    [region, setRegion] = useState<Region>(entire),
    [threshold, setThreshold] = useState(24);
  const [rows, setRows] = useState<SearchTrack[]>([]),
    [running, setRunning] = useState(false),
    [message, setMessage] = useState(""),
    [at, setAt] = useState(0),
    [playing, setPlaying] = useState(false),
    [materials, setMaterials] = useState<Material[]>([]);
  const plans: FloorPlan[] =
      useResource(services, "maps:" + site, (signal) =>
        services.maps(site, signal),
      ).value?.plans ?? [],
    [plan, setPlan] = useState(""),
    [event, setEvent] = useState("");
  const [last, setLast] = useState<VideoSearch | null>(null);
  const controller = useRef<AbortController | null>(null);
  useEffect(() => () => controller.current?.abort(), []);
  const name = (id: string) =>
    points.find((p) => p.id === id)?.name ?? t("inv.pointUnavailable");
  async function search(
    more = false,
    restored?: { body: VideoSearch; at: string },
  ) {
    const ctl = new AbortController();
    controller.current?.abort();
    controller.current = ctl;
    setRunning(true);
    setMessage("");
    setPlaying(false);
    try {
      if (more && last) {
        const extended = await Promise.all(
          rows.map(async (r) => {
            if (!r.next) return r;
            const batch = await services.searchFrames(
              site,
              {
                ...last,
                points: [r.point_id],
                interval: { start: r.next, end: last.interval.end },
              },
              ctl.signal,
            );
            const next = batch[0];
            if (!next) return null;
            return {
              ...next,
              coverage: [...r.coverage, ...next.coverage],
              hits: [...r.hits, ...next.hits],
              frames: [...r.frames, ...next.frames],
              gaps: [...r.gaps, ...next.gaps],
              events: [...(r.events ?? []), ...(next.events ?? [])],
            };
          }),
        );
        if (!ctl.signal.aborted)
          setRows(extended.filter((r): r is SearchTrack => r !== null));
        return;
      }
      const interval = restored?.body.interval ?? {
        start: iso(Date.parse(from)),
        end: iso(Date.parse(to)),
      };
      const body = restored?.body ?? {
        points: selected,
        interval,
        motion,
        threshold,
        region,
        ...(plan ? { plan } : {}),
        ...(event ? { event_query: event } : {}),
      };
      if (
        !body.points.length ||
        Date.parse(interval.end) <= Date.parse(interval.start)
      )
        throw Error();
      const result = await services.searchFrames(site, body, ctl.signal);
      if (!ctl.signal.aborted) {
        setRows(result);
        setLast(body);
        setAt(
          Date.parse(
            restored?.at ??
              result.flatMap((r) => r.hits.map((h) => h.at)).sort()[0] ??
              interval.start,
          ),
        );
        if (!result.some((r) => r.coverage.length))
          setMessage(t("inv.noRecording"));
      }
    } catch {
      if (!ctl.signal.aborted) {
        setMessage(t("inv.searchFailed"));
        setRows([]);
        setLast(null);
      }
    } finally {
      if (!ctl.signal.aborted) setRunning(false);
    }
  }
  const end = last ? Date.parse(last.interval.end) : 0;
  useEffect(() => {
    if (!playing) return;
    const begin = performance.now(),
      initial = at;
    const timer = setInterval(() => {
      const value = initial + performance.now() - begin;
      setAt(Math.min(value, end));
      if (value >= end) setPlaying(false);
    }, 90);
    return () => clearInterval(timer);
    // Each playback run owns one monotonic origin.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [playing, end]);
  const frames = [
    ...new Set(rows.flatMap((r) => r.frames).map(Date.parse)),
  ].sort((a, b) => a - b);
  function seek(value: number | undefined) {
    if (value !== undefined) {
      setPlaying(false);
      setAt(value);
    }
  }
  function bookmark() {
    if (!last) return;
    setMaterials((prev) => {
      const next = [...prev];
      for (const r of rows) {
        if (
          !r.coverage.some(
            (b) =>
              Date.parse(b.interval.start) <= at &&
              at < Date.parse(b.interval.end),
          )
        )
          continue;
        const point = r.point_id,
          interval = last.interval,
          mark = { at: iso(at), name: clock(iso(at)) },
          i = next.findIndex(
            (m) =>
              m.point_id === point &&
              m.interval.start === interval.start &&
              m.interval.end === interval.end,
          );
        if (i < 0) next.push({ point_id: point, interval, bookmarks: [mark] });
        else if (!next[i]!.bookmarks.some((b) => b.at === mark.at))
          next[i] = { ...next[i]!, bookmarks: [...next[i]!.bookmarks, mark] };
      }
      return next;
    });
  }
  return (
    <>
      <SubsectionMenu
        label={t("inv.title")}
        items={[
          {
            label: t("inv.archiveSearch"),
            icon: "scan-line",
            active: tab === "search",
            select: () => setTab("search"),
          },
          {
            label: t("inv.cases"),
            icon: "clipboard-list",
            active: tab === "cases",
            select: () => {
              setPlaying(false);
              setTab("cases");
            },
          },
        ]}
      />
      {tab === "cases" ? (
        <Cases
          {...{ site, services, points, user, materials }}
          clear={() => setMaterials([])}
          reopen={(m, at, all) => {
            const chosen = [
              ...new Set([
                m.point_id,
                ...all
                  .filter(
                    (a) =>
                      Date.parse(a.interval.start) <= Date.parse(at) &&
                      Date.parse(at) < Date.parse(a.interval.end),
                  )
                  .map((a) => a.point_id),
              ]),
            ].slice(0, 4);
            setTab("search");
            setPlaying(false);
            setSelected(chosen);
            setFrom(localDateTime(Date.parse(m.interval.start)));
            setTo(localDateTime(Date.parse(m.interval.end)));
            setEvent("");
            setPlan("");
            setMotion(false);
            void search(false, {
              at,
              body: { points: chosen, interval: m.interval, motion: false },
            });
          }}
        />
      ) : (
        <div className="inv-work">
          <FilterPanel
            label={t("inv.archiveSearch")}
            queryLabel={t("channel.camera")}
            query={query}
            onQuery={setQuery}
            filtered={!!query || !!selected.length}
            onReset={() => {
              controller.current?.abort();
              setRunning(false);
              setQuery("");
              setSelected([]);
              setRegion(entire);
              setEvent("");
              setPlan("");
              setThreshold(24);
              setMotion(true);
              setRows([]);
              setLast(null);
              setMessage("");
              setPlaying(false);
            }}
            onApply={() => void search()}
          >
            <div className="inv-points">
              {points
                .filter((p) => matchesQuery(query, p.name))
                .map((p) => (
                  <label key={p.id}>
                    <input
                      type="checkbox"
                      checked={selected.includes(p.id)}
                      disabled={
                        !selected.includes(p.id) && selected.length >= 4
                      }
                      onChange={(e) =>
                        setSelected(
                          e.target.checked
                            ? [...selected, p.id]
                            : selected.filter((id) => id !== p.id),
                        )
                      }
                    />
                    {p.name}
                  </label>
                ))}
            </div>
            <label>
              {t("rules.from")}
              <input
                type="datetime-local"
                step=".001"
                value={from}
                onChange={(e) => setFrom(e.target.value)}
              />
            </label>
            <label>
              {t("rules.to")}
              <input
                type="datetime-local"
                step=".001"
                value={to}
                onChange={(e) => setTo(e.target.value)}
              />
            </label>
            <FilterSegments
              label={t("inv.searchType")}
              options={[
                { value: "all", label: t("inv.allFrames") },
                { value: "motion", label: t("inv.motion") },
              ]}
              value={motion ? "motion" : "all"}
              onChange={(v) => {
                if (v) setMotion(v === "motion");
              }}
            />
            <details className="inv-refine">
              <summary>{t("inv.episodeFilters")}</summary>
              <div>
                <label>
                  {t("inv.metadata")}
                  <input
                    value={event}
                    onChange={(e) => setEvent(e.target.value)}
                  />
                </label>
                {plans.length > 0 && (
                  <label>
                    {t("inv.zone")}
                    <select
                      value={plan}
                      onChange={(e) => setPlan(e.target.value)}
                    >
                      <option value="">{t("inv.allPlans")}</option>
                      {plans.map((p) => (
                        <option key={p.id} value={p.id}>
                          {p.name}
                        </option>
                      ))}
                    </select>
                  </label>
                )}
                {motion && (
                  <>
                    <label>
                      {t("rules.threshold")}
                      <input
                        type="range"
                        min={1}
                        max={255}
                        value={threshold}
                        onChange={(e) => setThreshold(+e.target.value)}
                      />
                    </label>
                    <RegionField
                      region={region}
                      change={setRegion}
                      image={rows[0]?.hits[0]?.preview}
                    />
                  </>
                )}
              </div>
            </details>
          </FilterPanel>
          {message && <p role="status">{message}</p>}
          {running && <progress aria-label={t("inv.searching")} />}
          {last && rows.length > 0 && (
            <>
              <div className="inv-players" data-count={rows.length}>
                {rows.map((row) => (
                  <Track
                    key={row.point_id}
                    {...{ services, site, row, at, playing }}
                    name={name(row.point_id)}
                  />
                ))}
              </div>
              <div className="inv-transport">
                <IconButton
                  icon="chevron-left"
                  label={t("inv.previous")}
                  onClick={() => seek(frames.filter((t) => t < at).at(-1))}
                />
                <IconButton
                  icon={playing ? "pause" : "play"}
                  label={playing ? t("inv.pause") : t("inv.syncPlay")}
                  onClick={() => setPlaying(!playing)}
                />
                <IconButton
                  icon="chevron-right"
                  label={t("inv.next")}
                  onClick={() => seek(frames.find((t) => t > at))}
                />
                <input
                  aria-label={t("inv.position")}
                  type="range"
                  min={Date.parse(last.interval.start)}
                  max={end - 1}
                  step={1}
                  value={at}
                  onChange={(e) => seek(+e.target.value)}
                />
                <output>{clock(iso(at))}</output>
                <IconButton
                  icon="bookmark"
                  label={t("inv.bookmark")}
                  onClick={bookmark}
                />
                {materials.length > 0 && (
                  <Button
                    onClick={() => {
                      setPlaying(false);
                      setTab("cases");
                    }}
                  >
                    {t("inv.materials", { count: materials.length })}
                  </Button>
                )}
              </div>
              {rows.map((r) => (
                <section key={r.point_id} aria-label={name(r.point_id)}>
                  <strong>{name(r.point_id)}</strong>
                  <div className="inv-hits">
                    {r.hits.map((h) => (
                      <button
                        type="button"
                        key={h.at}
                        onClick={() => seek(Date.parse(h.at))}
                      >
                        <Preview pixels={h.preview} />
                        {clock(h.at)}
                      </button>
                    ))}
                  </div>
                  {r.events?.map((ev) => (
                    <Button key={ev.id} onClick={() => seek(Date.parse(ev.at))}>
                      {clock(ev.at)} · {ev.topic}
                    </Button>
                  ))}
                  {r.hits.length === 0 && <small>{t("inv.noHits")}</small>}
                  {r.gaps.map((g) => (
                    <small key={g.interval.start}>
                      {t("inv.gap")} · {clock(g.interval.start)} -{" "}
                      {clock(g.interval.end)}
                    </small>
                  ))}
                </section>
              ))}
              {rows.some((r) => r.next) && (
                <Button disabled={running} onClick={() => void search(true)}>
                  {t("inv.more")}
                </Button>
              )}
            </>
          )}
        </div>
      )}
    </>
  );
}
