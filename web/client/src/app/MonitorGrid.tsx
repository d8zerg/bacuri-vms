import { useEffect, useMemo, useRef, useState } from "react";
import { useSearchParams } from "react-router";
import type {
  Layout,
  LayoutDraft,
  Point,
  Services,
  Target,
} from "../contracts/workspace";
import { Button, IconButton } from "../ui-kit/FormControls";
import { FilterPanel, matchesQuery } from "../ui-kit/FilterPanel";
import { SessionPlayer } from "./media";
import "./monitor-grid.css";
import { useT } from "../i18n";

function Tile({
  site,
  point,
  show,
  services,
  expanded,
  expand,
}: {
  site: string;
  point: Point;
  show: boolean;
  services: Services;
  expanded: boolean;
  expand: () => void;
}) {
  const target = useMemo<Target | null>(
    () => (show ? { mode: "live", point: point.id } : null),
    [show, point.id],
  );
  const [attempt, retry] = useState(0);
  return (
    <div hidden={!show} className="mw-tile">
      <SessionPlayer
        services={services}
        site={site}
        target={target}
        title={point.name}
        mode="live"
        notice=""
        attempt={attempt}
        retry={() => retry((v) => v + 1)}
        onExpand={expand}
        expanded={expanded}
      />
    </div>
  );
}
function Editor({
  layout,
  points,
  initial,
  admin,
  save,
  remove,
  close,
}: {
  layout: Layout | undefined;
  points: Point[];
  initial: (string | null)[];
  admin: boolean;
  save: (draft: LayoutDraft) => Promise<void>;
  remove: (() => Promise<void>) | undefined;
  close: () => void;
}) {
  const t = useT();
  const ref = useRef<HTMLDialogElement>(null);
  const [name, setName] = useState(layout?.name ?? ""),
    [shared, setShared] = useState(layout?.shared ?? false);
  const [ids, setIds] = useState(
    initial.filter(
      (id): id is string => !!id && points.some((p) => p.id === id),
    ),
  );
  const [query, setQuery] = useState(""),
    [busy, setBusy] = useState(false),
    [error, setError] = useState(""),
    [confirm, setConfirm] = useState(false);
  useEffect(() => {
    const d = ref.current;
    d?.showModal();
    return () => d?.close();
  }, []);
  async function submit(deleting = false) {
    setBusy(true);
    setError("");
    try {
      if (deleting && remove) await remove();
      else
        await save({
          name: name.trim(),
          points: ids,
          shared,
          revision: layout?.revision ?? "0",
        });
      close();
    } catch {
      setError(t("layout.saveFailed"));
    } finally {
      setBusy(false);
    }
  }
  function shift(id: string, by: number) {
    setIds((current) => {
      const index = current.indexOf(id),
        next = index + by;
      if (index < 0 || next < 0 || next >= current.length) return current;
      const copy = [...current];
      copy.splice(index, 1);
      copy.splice(next, 0, id);
      return copy;
    });
  }
  return (
    <dialog
      ref={ref}
      className="mw-editor"
      aria-label={t("layout.configure")}
      onCancel={(e) => {
        if (busy) e.preventDefault();
        else close();
      }}
    >
      <header>
        <label>
          {t("layout.name")}
          <input
            value={name}
            maxLength={80}
            onChange={(e) => setName(e.target.value)}
          />
        </label>
        <IconButton
          icon="circle-x"
          label={t("layout.closeEditor")}
          disabled={busy}
          onClick={close}
        />
      </header>
      <FilterPanel
        label={t("layout.cameraFilter")}
        queryLabel={t("layout.findCamera")}
        query={query}
        onQuery={setQuery}
        onReset={() => setQuery("")}
      />
      <div className="mw-choices">
        {[
          ...ids
            .map((id) => points.find((p) => p.id === id))
            .filter((p): p is Point => !!p),
          ...points.filter((p) => !ids.includes(p.id)),
        ]
          .filter((p) => matchesQuery(query, p.name))
          .map((p) => (
            <div key={p.id}>
              <label>
                <input
                  type="checkbox"
                  checked={ids.includes(p.id)}
                  disabled={busy || (!ids.includes(p.id) && ids.length === 16)}
                  onChange={(e) =>
                    setIds((current) =>
                      e.target.checked
                        ? [...current, p.id]
                        : current.filter((id) => id !== p.id),
                    )
                  }
                />
                {p.name}
              </label>
              {ids.includes(p.id) && (
                <>
                  <IconButton
                    icon="chevron-up"
                    label={t("layout.moveUp", { name: p.name })}
                    disabled={ids[0] === p.id}
                    onClick={() => shift(p.id, -1)}
                  />
                  <IconButton
                    icon="chevron-down"
                    label={t("layout.moveDown", { name: p.name })}
                    disabled={ids.at(-1) === p.id}
                    onClick={() => shift(p.id, 1)}
                  />
                </>
              )}
            </div>
          ))}
      </div>
      {admin && (
        <label>
          <input
            type="checkbox"
            checked={shared}
            onChange={(e) => setShared(e.target.checked)}
          />{" "}
          {t("layout.shared")}
        </label>
      )}
      {error && <p role="alert">{error}</p>}
      <footer>
        {remove && (
          <Button
            disabled={busy}
            onClick={() => (confirm ? void submit(true) : setConfirm(true))}
          >
            {confirm ? t("layout.confirmDelete") : t("layout.delete")}
          </Button>
        )}
        <Button
          variant="primary"
          disabled={busy || !name.trim() || !ids.length}
          onClick={() => void submit()}
        >
          {t("action.save")}
        </Button>
      </footer>
    </dialog>
  );
}

export function MonitorGrid({
  site,
  services,
  points,
  admin,
  back,
}: {
  site: string;
  services: Services;
  points: Point[];
  admin: boolean;
  back: () => void;
}) {
  const t = useT();
  const [params, setParams] = useSearchParams(),
    id = params.get("layout"),
    wall = params.get("wall") === "1";
  const [layouts, setLayouts] = useState<Layout[]>([]),
    [failed, setFailed] = useState(false),
    [edit, setEdit] = useState<"new" | "edit" | null>(null);
  const [focus, setFocus] = useState<string | null>(null),
    [refresh, reload] = useState(0),
    [wallError, setWallError] = useState("");
  const root = useRef<HTMLElement>(null);
  function select(next: string | null) {
    setFocus(null);
    const q = new URLSearchParams(params);
    if (next) q.set("layout", next);
    else q.delete("layout");
    void setParams(q, { replace: true });
  }
  useEffect(() => {
    const ctl = new AbortController();
    let timer: ReturnType<typeof setTimeout>;
    async function read() {
      try {
        const rows = await services.layouts(site, ctl.signal);
        if (!ctl.signal.aborted) {
          setLayouts(rows);
          setFailed(false);
        }
      } catch {
        if (!ctl.signal.aborted) setFailed(true);
      } finally {
        if (wall && !ctl.signal.aborted)
          timer = setTimeout(() => void read(), 5000);
      }
    }
    void read();
    return () => {
      ctl.abort();
      clearTimeout(timer);
    };
  }, [site, services, wall, refresh]);
  useEffect(() => {
    if (!focus) return;
    const exit = (e: globalThis.KeyboardEvent) => {
      if (e.key === "Escape") setFocus(null);
    };
    window.addEventListener("keydown", exit);
    return () => window.removeEventListener("keydown", exit);
  }, [focus]);
  const selected = layouts.find((l) => l.id === id);
  const cells = id
    ? (selected?.points ?? [])
    : points.slice(0, 4).map((p) => p.id);
  const expanded =
    cells.includes(focus) && points.some((p) => p.id === focus) ? focus : null;
  async function fullscreen() {
    try {
      if (document.fullscreenElement === root.current)
        await document.exitFullscreen();
      else await root.current?.requestFullscreen();
    } catch {
      setWallError(t("layout.fullscreenDenied"));
    }
  }
  function openWall() {
    const url = new URL("/monitor", location.origin);
    url.search = new URLSearchParams({
      site,
      grid: "1",
      wall: "1",
      ...(id ? { layout: id } : {}),
    }).toString();
    window.open(url, "_blank", "noopener");
  }
  return (
    <section
      ref={root}
      className={"mw-workspace" + (wall ? " mw-wall" : "")}
      aria-label={t("layout.label")}
    >
      <div className="mw-toolbar">
        <IconButton
          icon="monitor-play"
          label={t("layout.single")}
          onClick={back}
        />
        <select
          aria-label={t("layout.layout")}
          value={id ?? ""}
          onChange={(e) => select(e.target.value || null)}
        >
          <option value="">{t("layout.available")}</option>
          {id && !selected && (
            <option value={id}>{t("layout.unavailable")}</option>
          )}
          {layouts.map((l) => (
            <option key={l.id} value={l.id}>
              {l.name}
              {l.shared ? " · " + t("layout.sharedShort") : ""}
            </option>
          ))}
        </select>
        {!wall && (
          <>
            <IconButton
              icon="plus"
              label={t("layout.create")}
              onClick={() => setEdit("new")}
            />
            {selected?.editable && (
              <IconButton
                icon="settings-2"
                label={t("layout.configure")}
                onClick={() => setEdit("edit")}
              />
            )}
          </>
        )}
        <div className="mw-display">
          {!wall && (
            <IconButton
              icon="monitor"
              label={t("layout.wall")}
              onClick={openWall}
            />
          )}
          <IconButton
            icon="expand"
            label={t("layout.fullscreen")}
            onClick={() => void fullscreen()}
          />
        </div>
      </div>
      {failed && (
        <div role="alert">
          {t("layout.loadFailed")}{" "}
          <IconButton
            icon="rotate-cw"
            label={t("layout.reload")}
            onClick={() => reload((n) => n + 1)}
          />
        </div>
      )}
      {wallError && <p role="status">{wallError}</p>}
      {!cells.length && (
        <p role="status">
          {id ? t("layout.unavailable") : t("layout.noCameras")}
        </p>
      )}
      <div
        className="mw-grid"
        style={
          {
            "--rows": expanded
              ? 1
              : Math.ceil(
                  cells.length / Math.ceil(Math.sqrt(cells.length) || 1),
                ),
            "--columns": expanded ? 1 : Math.ceil(Math.sqrt(cells.length)),
          } as React.CSSProperties
        }
      >
        {cells.map((cell, i) => {
          const point = points.find((p) => p.id === cell);
          return point ? (
            <Tile
              key={point.id}
              services={services}
              site={site}
              point={point}
              show={!expanded || expanded === point.id}
              expanded={expanded === point.id}
              expand={() => setFocus(expanded ? null : point.id)}
            />
          ) : (
            <div className="mw-missing" hidden={!!expanded} key={"missing" + i}>
              {t("layout.noAccess")}
            </div>
          );
        })}
      </div>
      {edit && (
        <Editor
          layout={edit === "edit" ? selected : undefined}
          initial={cells}
          points={points}
          admin={admin}
          close={() => setEdit(null)}
          save={async (draft) => {
            const targetId =
              edit === "edit" && selected ? selected.id : crypto.randomUUID();
            const saved = await services.saveLayout(site, targetId, draft);
            setLayouts((current) => [
              ...current.filter((l) => l.id !== targetId),
              { ...draft, ...saved, editable: true },
            ]);
            select(saved.id);
            reload((n) => n + 1);
          }}
          remove={
            edit === "edit" && selected
              ? async () => {
                  await services.deleteLayout(
                    site,
                    selected.id,
                    selected.revision,
                  );
                  select(null);
                  reload((n) => n + 1);
                }
              : undefined
          }
        />
      )}
    </section>
  );
}
