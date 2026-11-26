// Investigation cases: their materials, assignee, retention and the signed release.
import { useEffect, useRef, useState } from "react";
import type { Investigation } from "../../contracts/workspace";
import { FilterPanel, matchesQuery } from "../../ui-kit/FilterPanel";
import { Button, IconButton } from "../../ui-kit/FormControls";
import FragmentExport from "../FragmentExport";
import { localDateTime } from "../ArchiveFrame";
import { locale, useT } from "../../i18n";
import { useResource } from "../../adapters/resource";
import { clock, iso, type Material, type Props } from "./shared";

export function Cases({
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
  const t = useT();
  const list = useResource(services, "cases:" + site, (signal) =>
    services.cases(site, signal),
  );
  const items: Investigation[] = list.value ?? [];
  const [query, setQuery] = useState(""),
    [notice, setMessage] = useState("");
  const message = notice || (list.error ? t("inv.casesFailed") : "");
  const [current, setCurrent] = useState<Investigation | null>(null),
    [editing, setEditing] = useState(false),
    [number, setNumber] = useState(""),
    [notes, setNotes] = useState(""),
    [reason, setReason] = useState(""),
    [owner, setOwner] = useState(user),
    [expiry, setExpiry] = useState(localDateTime(Date.now() + 7 * 86400000)),
    [busy, setBusy] = useState(false);
  const [selected, setSelected] = useState<Material[]>([]);
  const recipients: { id: string; name: string }[] =
    useResource(
      services,
      editing
        ? "case-recipients:" + site + ":" + JSON.stringify(selected)
        : null,
      (signal) => services.caseRecipients(site, selected, signal),
    ).value ?? [];
  const [bundleMode, setBundleMode] = useState<"original" | "derivative">(
      "derivative",
    ),
    [sound, setSound] = useState(false),
    [release, setRelease] = useState("");
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
      if (!ctl.signal.aborted) setMessage(t("inv.releaseDenied"));
    } finally {
      if (!ctl.signal.aborted) setBusy(false);
    }
  }
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
      list.reload();
      setMessage(t("inv.caseSaved"));
    } catch {
      setMessage(t("inv.caseSaveFailed"));
    } finally {
      setBusy(false);
    }
  }
  return (
    <div className="inv-work">
      <FilterPanel
        label={t("inv.caseSearch")}
        queryLabel={t("inv.caseQuery")}
        query={query}
        onQuery={setQuery}
        onReset={() => setQuery("")}
        refresh={{
          label: t("inv.refreshCases"),
          run: () => list.reload(),
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
          {t("inv.newCase")}
          {materials.length ? " · " + materials.length : ""}
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
            {t("inv.caseNumber")}
            <input
              required
              maxLength={64}
              value={number}
              onChange={(e) => setNumber(e.target.value)}
            />
          </label>
          <label>
            {t("inv.owner")}
            <select
              required
              value={owner}
              onChange={(e) => setOwner(e.target.value)}
            >
              {!recipients.some((r) => r.id === owner) && (
                <option value={owner}>
                  {owner === user ? t("inv.me") : t("inv.ownerUnavailable")}
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
            {t("inv.protectUntil")}
            <input
              type="datetime-local"
              required
              step=".001"
              value={expiry}
              onChange={(e) => setExpiry(e.target.value)}
            />
          </label>
          <label>
            {t("inv.notes")}
            <textarea
              value={notes}
              maxLength={8192}
              onChange={(e) => setNotes(e.target.value)}
            />
          </label>
          <label>
            {t("inv.protectReason")}
            <input
              required
              maxLength={512}
              value={reason}
              onChange={(e) => setReason(e.target.value)}
            />
          </label>
          {selected.length === 0 && <p>{t("inv.addHint")}</p>}
          {selected.map((m, i) => (
            <div className="inv-material" key={i}>
              <strong>{points.find((p) => p.id === m.point_id)?.name}</strong>
              <Button onClick={() => reopen(m, m.interval.start, selected)}>
                {new Date(m.interval.start).toLocaleString(locale())} -{" "}
                {clock(m.interval.end)}
              </Button>
              {m.bookmarks.map((b) => (
                <Button key={b.at} onClick={() => reopen(m, b.at, selected)}>
                  {b.name}
                </Button>
              ))}
              <IconButton
                icon="circle-x"
                label={t("inv.removeMaterial")}
                onClick={() => setSelected(selected.filter((_, n) => n !== i))}
              />
            </div>
          ))}
          <Button
            variant="primary"
            type="submit"
            disabled={busy || !selected.length}
          >
            {t("inv.saveCase")}
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
            {t("inv.package")}
            <select
              value={bundleMode}
              onChange={(e) =>
                setBundleMode(e.target.value as typeof bundleMode)
              }
            >
              <option value="derivative">{t("export.derivative")}</option>
              <option value="original">{t("inv.originals")}</option>
            </select>
          </label>
          <label>
            <input
              type="checkbox"
              checked={sound}
              onChange={(e) => setSound(e.target.checked)}
            />
            {t("inv.withAudio")}
          </label>
          <label>
            {t("export.reason")}
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
            label={t("inv.download")}
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
