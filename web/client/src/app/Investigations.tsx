import { useEffect, useRef, useState } from "react";
import type {
  CaseDraft,
  FloorPlan,
  Investigation,
  Point,
  SearchTrack,
  Services,
  VideoSearch,
} from "../contracts/workspace";
import {
  FilterPanel,
  FilterSegments,
  matchesQuery,
} from "../ui-kit/FilterPanel";
import { Button, IconButton } from "../ui-kit/FormControls";
import { SubsectionMenu } from "./SubsectionMenu";
import FragmentExport from "./FragmentExport";
import { localDateTime, Preview, RegionField } from "./ArchiveFrame";
import "./investigations.css";

type Props = {
  site: string;
  services: Services;
  points: Point[];
  user: string;
};
type Material = CaseDraft["materials"][number];
type Region = NonNullable<VideoSearch["region"]>;
const iso = (ms: number) => new Date(ms).toISOString();
const clock = (value: string) =>
  new Date(value).toLocaleTimeString("ru", {
    hour12: false,
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    fractionalSecondDigits: 3,
  });
const entire: Region = { left: 0, top: 0, right: 1, bottom: 1 };
function Track({
  services,
  site,
  row,
  at,
  playing,
  name,
}: {
  services: Services;
  site: string;
  row: SearchTrack;
  at: number;
  playing: boolean;
  name: string;
}) {
  const video = useRef<HTMLVideoElement>(null),
    cursor = useRef(at);
  useEffect(() => {
    cursor.current = at;
  }, [at]);
  const [origin, setOrigin] = useState<number | null>(null),
    [notice, setNotice] = useState("");
  const current = row.coverage.find(
    (b) =>
      Date.parse(b.interval.start) <= at && at < Date.parse(b.interval.end),
  );
  const binding = current?.binding_revision;
  let start = current?.interval.start,
    end = current?.interval.end;
  if (current) {
    const i = row.coverage.indexOf(current);
    for (let n = i - 1; n >= 0; n--) {
      const previous = row.coverage[n];
      if (
        !previous ||
        previous.binding_revision !== binding ||
        previous.interval.end !== start
      )
        break;
      start = previous.interval.start;
    }
    for (let n = i + 1; n < row.coverage.length; n++) {
      const next = row.coverage[n];
      if (
        !next ||
        next.binding_revision !== binding ||
        next.interval.start !== end
      )
        break;
      end = next.interval.end;
    }
  }
  const reposition = useRef<((at: string) => Promise<number>) | null>(null);
  useEffect(() => {
    const v = video.current,
      ctl = new AbortController();
    setOrigin(null);
    reposition.current = null;
    setNotice("");
    if (v && start && end && binding)
      void services
        .attach(
          v,
          site,
          {
            mode: "archive",
            point: row.point_id,
            position: iso(cursor.current),
            interval: { start, end },
            binding_revision: binding,
          },
          ctl.signal,
          (message) => {
            if (!ctl.signal.aborted) setNotice(message);
          },
          undefined,
          undefined,
          (t, seek) => {
            if (!ctl.signal.aborted) {
              reposition.current = seek;
              setOrigin(t);
            }
          },
        )
        .catch(() => {});
    return () => ctl.abort();
  }, [start, end, binding, site, services, row.point_id]);
  useEffect(() => {
    const v = video.current;
    if (!v || origin === null) return;
    const offset = at / 1000 - origin;
    if (
      !playing &&
      reposition.current &&
      !Array.from({ length: v.buffered.length }, (_, i) => i).some(
        (i) => v.buffered.start(i) <= offset && offset < v.buffered.end(i),
      )
    ) {
      let active = true;
      void reposition
        .current(iso(at))
        .then((t) => {
          if (active) setOrigin(t);
        })
        .catch(() => {
          if (active) setNotice("Кадр недоступен");
        });
      return () => {
        active = false;
      };
    }
    if (
      offset >= 0 &&
      Math.abs(offset - v.currentTime) > (playing ? 0.16 : 0.001)
    )
      v.currentTime = offset;
    if (playing && v.paused)
      void v.play().catch(() => setNotice("Не удалось запустить просмотр"));
    else if (!playing && !v.paused) v.pause();
  }, [at, playing, origin]);
  return (
    <figure className="inv-track">
      <figcaption>{name}</figcaption>
      <video
        ref={video}
        muted
        playsInline
        hidden={!current || !!notice}
        aria-label={name}
      />
      {(!current || notice) && (
        <p role="status">{notice || "Пропуск записи"}</p>
      )}
      <small>
        {current
          ? (current.indexed ? "Проиндексировано" : "Ожидает индексации") +
            " · " +
            (current.uncertainty_us === null
              ? "Время приёма"
              : "± " +
                (current.uncertainty_us / 1000).toLocaleString("ru") +
                " мс")
          : ""}
      </small>
    </figure>
  );
}
export default function Investigations({
  services,
  site,
  points,
  user,
}: Props) {
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
  const [plans, setPlans] = useState<FloorPlan[]>([]),
    [plan, setPlan] = useState(""),
    [event, setEvent] = useState("");
  useEffect(() => {
    const ctl = new AbortController();
    void services
      .maps(site, ctl.signal)
      .then((r) => {
        if (!ctl.signal.aborted) setPlans(r.plans);
      })
      .catch(() => {});
    return () => ctl.abort();
  }, [site, services]);
  const [last, setLast] = useState<VideoSearch | null>(null);
  const controller = useRef<AbortController | null>(null);
  useEffect(() => () => controller.current?.abort(), []);
  const name = (id: string) =>
    points.find((p) => p.id === id)?.name ?? "Точка недоступна";
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
          setMessage("В выбранном интервале запись недоступна");
      }
    } catch {
      if (!ctl.signal.aborted) {
        setMessage("Проверьте камеры, интервал и права доступа");
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
        label="Расследования"
        items={[
          {
            label: "Поиск по архиву",
            icon: "scan-line",
            active: tab === "search",
            select: () => setTab("search"),
          },
          {
            label: "Дела",
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
            label="Поиск по архиву"
            queryLabel="Камера"
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
              С
              <input
                type="datetime-local"
                step=".001"
                value={from}
                onChange={(e) => setFrom(e.target.value)}
              />
            </label>
            <label>
              По
              <input
                type="datetime-local"
                step=".001"
                value={to}
                onChange={(e) => setTo(e.target.value)}
              />
            </label>
            <FilterSegments
              label="Тип поиска"
              options={[
                { value: "all", label: "Все кадры" },
                { value: "motion", label: "Движение" },
              ]}
              value={motion ? "motion" : "all"}
              onChange={(v) => {
                if (v) setMotion(v === "motion");
              }}
            />
            <details className="inv-refine">
              <summary>Фильтры эпизода</summary>
              <div>
                <label>
                  Событие или метаданные
                  <input
                    value={event}
                    onChange={(e) => setEvent(e.target.value)}
                  />
                </label>
                {plans.length > 0 && (
                  <label>
                    Зона наблюдения
                    <select
                      value={plan}
                      onChange={(e) => setPlan(e.target.value)}
                    >
                      <option value="">Все планы</option>
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
                      Порог изменения
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
          {running && <progress aria-label="Поиск" />}
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
                  label="Предыдущий кадр"
                  onClick={() => seek(frames.filter((t) => t < at).at(-1))}
                />
                <IconButton
                  icon={playing ? "pause" : "play"}
                  label={playing ? "Приостановить" : "Синхронный просмотр"}
                  onClick={() => setPlaying(!playing)}
                />
                <IconButton
                  icon="chevron-right"
                  label="Следующий кадр"
                  onClick={() => seek(frames.find((t) => t > at))}
                />
                <input
                  aria-label="Общая позиция камер"
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
                  label="Добавить закладку и интервалы в материалы дела"
                  onClick={bookmark}
                />
                {materials.length > 0 && (
                  <Button
                    onClick={() => {
                      setPlaying(false);
                      setTab("cases");
                    }}
                  >
                    Материалы ({materials.length})
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
                  {r.hits.length === 0 && <small>Совпадений нет</small>}
                  {r.gaps.map((g) => (
                    <small key={g.interval.start}>
                      Пропуск · {clock(g.interval.start)} -{" "}
                      {clock(g.interval.end)}
                    </small>
                  ))}
                </section>
              ))}
              {rows.some((r) => r.next) && (
                <Button disabled={running} onClick={() => void search(true)}>
                  Следующие результаты
                </Button>
              )}
            </>
          )}
        </div>
      )}
    </>
  );
}
function Cases({
  services,
  site,
  points,
  user,
  materials,
  clear,
  reopen,
}: Props & {
  materials: Material[];
  clear: () => void;
  reopen: (material: Material, at: string, all: Material[]) => void;
}) {
  const [items, setItems] = useState<Investigation[]>([]),
    [query, setQuery] = useState(""),
    [refresh, setRefresh] = useState(0),
    [message, setMessage] = useState("");
  const [current, setCurrent] = useState<Investigation | null>(null),
    [editing, setEditing] = useState(false),
    [number, setNumber] = useState(""),
    [notes, setNotes] = useState(""),
    [reason, setReason] = useState(""),
    [owner, setOwner] = useState(user),
    [expiry, setExpiry] = useState(localDateTime(Date.now() + 7 * 86400000)),
    [busy, setBusy] = useState(false);
  const [selected, setSelected] = useState<Material[]>([]);
  const [recipients, setRecipients] = useState<{ id: string; name: string }[]>(
    [],
  );
  const [bundleMode, setBundleMode] = useState<"original" | "derivative">(
      "derivative",
    ),
    [sound, setSound] = useState(false),
    [release, setRelease] = useState("");
  useEffect(() => {
    if (!editing) return;
    const ctl = new AbortController();
    void services
      .caseRecipients(site, selected, ctl.signal)
      .then((r) => {
        if (!ctl.signal.aborted) setRecipients(r);
      })
      .catch(() => {
        if (!ctl.signal.aborted) setRecipients([]);
      });
    return () => ctl.abort();
  }, [editing, site, services, selected]);
  const delivery = useRef<AbortController | null>(null);
  useEffect(() => () => delivery.current?.abort(), []);
  async function download() {
    if (!current) return;
    const ctl = new AbortController();
    delivery.current = ctl;
    setBusy(true);
    try {
      const file = await services.exportCase(
        site,
        current.id,
        {
          mode: bundleMode,
          revision: current.revision,
          audio: sound,
          reason: release,
        },
        ctl.signal,
      );
      if (!ctl.signal.aborted) {
        const a = document.createElement("a");
        a.href = URL.createObjectURL(file);
        a.download = "bacuri-" + current.number + ".tar";
        a.click();
        setTimeout(() => URL.revokeObjectURL(a.href), 1000);
      }
    } catch {
      if (!ctl.signal.aborted)
        setMessage(
          "Выдача дела недоступна. Проверьте права на каждый интервал.",
        );
    } finally {
      if (!ctl.signal.aborted) setBusy(false);
    }
  }
  useEffect(() => {
    const ctl = new AbortController();
    void services
      .cases(site, ctl.signal)
      .then((v) => {
        if (!ctl.signal.aborted) setItems(v);
      })
      .catch(() => {
        if (!ctl.signal.aborted) setMessage("Не удалось загрузить дела");
      });
    return () => ctl.abort();
  }, [services, site, refresh]);
  function open(c: Investigation | null) {
    setCurrent(c);
    setEditing(true);
    setNumber(c?.number ?? "");
    setNotes(c?.notes ?? "");
    setReason("");
    setOwner(c?.assignee ?? user);
    setExpiry(
      localDateTime(c ? Date.parse(c.expires_at) : Date.now() + 7 * 86400000),
    );
    setSelected([
      ...(c?.materials ?? []).map(({ point_id, interval, bookmarks }) => ({
        point_id,
        interval,
        bookmarks,
      })),
      ...materials,
    ]);
    setMessage("");
  }
  async function save() {
    setBusy(true);
    try {
      await services.saveCase(site, current?.id ?? crypto.randomUUID(), {
        number,
        notes,
        assignee: owner,
        revision: current?.revision ?? "0",
        reason,
        expires_at: iso(Date.parse(expiry)),
        materials: selected,
      });
      setEditing(false);
      clear();
      setRefresh((n) => n + 1);
      setMessage("Дело сохранено");
    } catch {
      setMessage(
        "Проверьте права ответственного, срок защиты и актуальность дела",
      );
    } finally {
      setBusy(false);
    }
  }
  return (
    <div className="inv-work">
      <FilterPanel
        label="Поиск дел"
        queryLabel="Номер, примечания"
        query={query}
        onQuery={setQuery}
        onReset={() => setQuery("")}
        refresh={{
          label: "Обновить дела",
          run: () => setRefresh((n) => n + 1),
        }}
      />
      <div className="inv-cases">
        {items
          .filter((c) => matchesQuery(query, c.number, c.notes))
          .map((c) => (
            <Button key={c.id} onClick={() => open(c)}>
              {c.number}
            </Button>
          ))}
        <Button onClick={() => open(null)}>
          Новое дело{materials.length ? " · " + materials.length : ""}
        </Button>
      </div>
      {message && <p role="status">{message}</p>}
      {editing && (
        <form
          className="inv-case"
          onSubmit={(e) => {
            e.preventDefault();
            void save();
          }}
        >
          <label>
            Номер дела
            <input
              required
              maxLength={64}
              value={number}
              onChange={(e) => setNumber(e.target.value)}
            />
          </label>
          <label>
            Ответственный
            <select
              required
              value={owner}
              onChange={(e) => setOwner(e.target.value)}
            >
              {!recipients.some((r) => r.id === owner) && (
                <option value={owner}>
                  {owner === user ? "Я" : "Недоступен"}
                </option>
              )}
              {recipients.map((r) => (
                <option value={r.id} key={r.id}>
                  {r.name}
                </option>
              ))}
            </select>
          </label>
          <label>
            Защита до
            <input
              type="datetime-local"
              required
              step=".001"
              value={expiry}
              onChange={(e) => setExpiry(e.target.value)}
            />
          </label>
          <label>
            Примечания
            <textarea
              value={notes}
              maxLength={8192}
              onChange={(e) => setNotes(e.target.value)}
            />
          </label>
          <label>
            Основание защиты
            <input
              required
              maxLength={512}
              value={reason}
              onChange={(e) => setReason(e.target.value)}
            />
          </label>
          {selected.length === 0 && (
            <p>Добавьте интервал кнопкой закладки в поиске по архиву.</p>
          )}
          {selected.map((m, i) => (
            <div className="inv-material" key={i}>
              <strong>{points.find((p) => p.id === m.point_id)?.name}</strong>
              <Button onClick={() => reopen(m, m.interval.start, selected)}>
                {new Date(m.interval.start).toLocaleString("ru")} -{" "}
                {clock(m.interval.end)}
              </Button>
              {m.bookmarks.map((b) => (
                <Button key={b.at} onClick={() => reopen(m, b.at, selected)}>
                  {b.name}
                </Button>
              ))}
              <IconButton
                icon="circle-x"
                label="Удалить материал"
                onClick={() => setSelected(selected.filter((_, n) => n !== i))}
              />
            </div>
          ))}
          <Button
            variant="primary"
            type="submit"
            disabled={busy || !selected.length}
          >
            Сохранить дело
          </Button>
        </form>
      )}
      {editing && current && (
        <form
          className="inv-transport"
          onSubmit={(e) => {
            e.preventDefault();
            void download();
          }}
        >
          <label>
            Комплект дела
            <select
              value={bundleMode}
              onChange={(e) =>
                setBundleMode(e.target.value as typeof bundleMode)
              }
            >
              <option value="derivative">Копия с масками</option>
              <option value="original">Оригиналы</option>
            </select>
          </label>
          <label>
            <input
              type="checkbox"
              checked={sound}
              onChange={(e) => setSound(e.target.checked)}
            />
            Со звуком
          </label>
          <label>
            Основание выдачи
            <input
              required
              value={release}
              maxLength={512}
              onChange={(e) => setRelease(e.target.value)}
            />
          </label>
          <IconButton
            icon="file-down"
            type="submit"
            disabled={busy}
            label="Скачать подписанный комплект дела"
          />
        </form>
      )}
      {editing &&
        current?.materials.map((m, i) => (
          <div key={i}>
            <strong>{points.find((p) => p.id === m.point_id)?.name}</strong>
            <FragmentExport
              services={services}
              site={site}
              point={m.point_id}
              alert={null}
              interval={m.interval}
            />
          </div>
        ))}
    </div>
  );
}
