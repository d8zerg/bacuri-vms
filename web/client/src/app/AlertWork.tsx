import "../i18n/extended";
import { catalogMessage, describeError, retryOf } from "../adapters/errors";
import {
  FilterPanel,
  matchesQuery,
  needsListSearch,
} from "../ui-kit/FilterPanel";
import { useEffect, useState } from "react";
import type { FormEvent } from "react";
import { useSearchParams } from "react-router";
import { RouteButton } from "./RouteButton";
import type {
  AlertCommand,
  AlertWork,
  AuditEntry,
  AuditFilter,
  Point,
  Services,
} from "../contracts/workspace";
import { HttpFailure } from "../adapters/api";
import { formatUtc } from "../ui-kit/utc";
import { Button } from "../ui-kit/FormControls";
import "./alert-work.css";
import { ptzLabel } from "./cameraCommands";

import { alertStateLabel } from "../ui-kit/alertLabels";
import { t as translate, useT, type MessageKey } from "../i18n";
const actions = {
  claim: "alert.actions.claim",
  record_result: "alert.actions.record_result",
  close: "alert.actions.close",
} as const satisfies Record<string, MessageKey>;
const events = {
  claim: "alert.events.claim",
  record_result: "alert.events.record_result",
  close: "alert.events.close",
  delegate: "alert.events.delegate",
  revoke: "alert.events.revoke",
  export: "alert.events.export",
  recording_policy: "alert.events.recording_policy",
  manual_recording: "alert.events.manual_recording",
  recording_storage: "alert.events.recording_storage",
  camera_control: "alert.events.camera_control",
} as const satisfies Record<string, MessageKey>;
const outcomes = {
  applied: "alert.outcomes.applied",
  dispatched: "alert.outcomes.dispatched",
  unknown: "alert.outcomes.unknown",
  rejected: "alert.outcomes.rejected",
  denied: "alert.outcomes.denied",
  conflict: "alert.outcomes.conflict",
} as const satisfies Record<string, MessageKey>;
const reasons: Record<string, MessageKey> = {
  forbidden: "alert.reasons.forbidden",
  media_unavailable: "alert.reasons.media_unavailable",
  rate_limited: "alert.reasons.rate_limited",
  export_too_large: "alert.reasons.export_too_large",
  not_found: "alert.reasons.not_found",
  alert_state_conflict: "alert.reasons.alert_state_conflict",
  alert_result_required: "alert.reasons.alert_result_required",
  idempotency_conflict: "alert.reasons.idempotency_conflict",
};
// The name of a refusal reason: the wording of the journal, the cause of the error catalog, or the code itself.
function reasonText(code: string) {
  const key = reasons[code];
  if (key) return translate(key);
  return catalogMessage(code) ?? code;
}
const changes = {
  policy_deleted: "audit.change.policy_deleted",
  policy_saved: "audit.change.policy_saved",
  manual_recording_started: "audit.change.manual_recording_started",
  manual_recording_stopped: "audit.change.manual_recording_stopped",
  storage_changed: "audit.change.storage_changed",
} as const satisfies Record<string, MessageKey>;
// A change of recording settings named by its code with its values. Entries written before the codes keep their
// recorded text.
function changeText(
  code: string,
  details: Record<string, string | number | boolean> = {},
) {
  return Object.hasOwn(changes, code)
    ? translate(
        changes[code as keyof typeof changes],
        Object.fromEntries(
          Object.entries(details).map(([k, v]) => [
            k,
            typeof v === "boolean" ? String(v) : v,
          ]),
        ),
      )
    : code;
}
type Context = { services: Services; site: string };

function Entries({
  items,
  site,
  points,
}: {
  items: AuditEntry[];
  site: string;
  points?: Point[];
}) {
  const t = useT();
  const [params] = useSearchParams();
  function monitorUrl(alert: string, point: string) {
    const next = new URLSearchParams(params);
    if (next.get("alert") !== alert) {
      for (const name of ["at", "from", "to", "scroll", "focus"])
        next.delete(name);
    }
    next.delete("alert_id");
    next.set("site", site);
    next.set("point", point);
    next.set("alert", alert);
    next.set("mode", "archive");
    return "/monitor?" + next.toString();
  }
  return (
    <ol className="ws-history">
      {items.map((e) => {
        const pointName = points?.find((p) => p.id === e.point_id)?.name;
        const actor =
          e.actor_kind === "service"
            ? t("alert.serviceActor", {
                service: e.service ?? "",
                initiator: e.initiator,
              })
            : e.initiator;
        const [command = "", ...target] = e.reason.split(" / ");
        const result =
          e.action === "camera_control"
            ? [ptzLabel(command) ?? reasonText(command), ...target].join(" / ")
            : e.outcome !== "applied"
              ? reasonText(e.reason)
              : e.action === "export" ||
                  e.action === "delegate" ||
                  e.action === "revoke"
                ? ""
                : e.action === "recording_policy" ||
                    e.action === "manual_recording" ||
                    e.action === "recording_storage"
                  ? changeText(e.reason, e.details)
                  : e.reason;
        return (
          <li key={e.id}>
            <details>
              <summary>
                <strong>
                  {t(events[e.action])}
                  {e.outcome !== "applied" &&
                    " · " +
                      (e.action === "export" && e.outcome === "denied"
                        ? t("alert.exportDenied")
                        : t(outcomes[e.outcome]))}
                </strong>
                <span className="ws-history-meta">
                  <time dateTime={e.recorded_at}>
                    {formatUtc(e.recorded_at)}
                  </time>{" "}
                  · {actor}
                  {pointName && " · " + pointName}
                </span>
                {result && <span className="ws-history-result">{result}</span>}
              </summary>
              <dl>
                <dt>{t("alert.actionResult")}</dt>
                <dd>
                  {e.action === "camera_control" && e.outcome === "applied"
                    ? t("alert.cameraAck")
                    : t(outcomes[e.outcome])}
                </dd>
                {e.from_state && e.to_state && e.from_state !== e.to_state && (
                  <>
                    <dt>{t("alert.state")}</dt>
                    <dd>
                      {alertStateLabel(e.from_state)} {"->"}{" "}
                      {alertStateLabel(e.to_state)}
                    </dd>
                  </>
                )}
                {e.owner && (
                  <>
                    <dt>{t("inv.owner")}</dt>
                    <dd>{e.owner}</dd>
                  </>
                )}
                {e.delegated_action && e.expires_at && (
                  <>
                    <dt>{t("alert.delegation")}</dt>
                    <dd>
                      {e.service} · {t(actions[e.delegated_action])} ·{" "}
                      {t("alert.until", { time: formatUtc(e.expires_at) })}
                    </dd>
                  </>
                )}
                {e.point_id && (
                  <>
                    <dt>{t("batch.point")}</dt>
                    <dd>{e.point_id}</dd>
                  </>
                )}
                {e.alert_id && (
                  <>
                    <dt>{t("alert.alarm")}</dt>
                    <dd>{e.alert_id}</dd>
                  </>
                )}
                {e.export && (
                  <>
                    <dt>{t("alert.package")}</dt>
                    <dd>{e.export.id}</dd>
                    <dt>{t("alert.interval")}</dt>
                    <dd>
                      {formatUtc(e.export.interval.start)} -{" "}
                      {formatUtc(e.export.interval.end)}
                    </dd>
                  </>
                )}
              </dl>
            </details>
            {points && e.alert_id && e.point_id && (
              <RouteButton to={monitorUrl(e.alert_id, e.point_id)}>
                {t("alert.open")}
              </RouteButton>
            )}
          </li>
        );
      })}
    </ol>
  );
}

function AlertHistory({ work, site }: { work: AlertWork; site: string }) {
  const t = useT();
  const [query, setQuery] = useState("");
  const rows = work.history.filter((e) =>
    matchesQuery(
      query,
      t(events[e.action]),
      t(outcomes[e.outcome]),
      e.initiator,
      e.owner,
      e.service,
      reasonText(e.reason),
    ),
  );
  return (
    <>
      {needsListSearch(work.history.length, query) && (
        <FilterPanel
          label={t("alert.historyFilters")}
          queryLabel={t("alert.historyQuery")}
          query={query}
          onQuery={setQuery}
          onReset={() => setQuery("")}
        />
      )}
      {rows.length ? (
        <Entries items={rows} site={site} />
      ) : (
        <p role="status">
          {query ? t("alert.noActionsFound") : t("alert.noActions")}
        </p>
      )}
    </>
  );
}

function Delegations({
  services,
  site,
  work,
  refresh,
}: { work: AlertWork; refresh: () => void } & Context) {
  const t = useT();
  const [query, setQuery] = useState("");
  const shown = work.delegations.filter((d) =>
    matchesQuery(query, d.service, t(actions[d.action])),
  );
  const [secret, setSecret] = useState(""),
    [notice, setNotice] = useState(""),
    [busy, setBusy] = useState(false);
  async function issue(e: FormEvent<HTMLFormElement>) {
    e.preventDefault();
    if (busy) return;
    const fields = new FormData(e.currentTarget),
      service = fields.get("service"),
      action = fields.get("action");
    if (
      typeof service !== "string" ||
      !(action === "claim" || action === "record_result" || action === "close")
    )
      return;
    setBusy(true);
    setSecret("");
    setNotice("");
    try {
      const grant = await services.delegate(site, {
        point_id: work.point_id,
        service,
        action,
        ttl_seconds: Number(fields.get("minutes")) * 60,
      });
      setSecret(grant.token);
      setNotice(t("alert.delegated"));
      refresh();
    } catch {
      setNotice(t("alert.delegateFailed"));
    } finally {
      setBusy(false);
    }
  }
  async function revoke(id: string) {
    setBusy(true);
    setSecret("");
    try {
      await services.revokeDelegation(site, id);
      setNotice(t("alert.revoked"));
      refresh();
    } catch {
      setNotice(t("alert.revokeFailed"));
    } finally {
      setBusy(false);
    }
  }
  return (
    <details
      onToggle={(e) => {
        if (!e.currentTarget.open) setSecret("");
      }}
    >
      <summary>{t("alert.delegations")}</summary>
      <form onSubmit={(e) => void issue(e)}>
        <label>
          {t("alert.serviceName")}
          <input name="service" required maxLength={128} />
        </label>
        <label>
          {t("alert.allowedAction")}
          <select name="action" defaultValue="record_result">
            {Object.entries(actions).map(([id, label]) => (
              <option key={id} value={id}>
                {t(label)}
              </option>
            ))}
          </select>
        </label>
        <label>
          {t("alert.minutes")}
          <input
            name="minutes"
            type="number"
            min={1}
            max={1440}
            required
            defaultValue={15}
          />
        </label>
        <Button type="submit" disabled={busy}>
          {t("alert.delegate")}
        </Button>
      </form>
      {secret && (
        <label>
          {t("alert.token")}
          <input readOnly value={secret} autoComplete="off" />
        </label>
      )}
      {needsListSearch(work.delegations.length, query) && (
        <FilterPanel
          label={t("alert.delegationFilters")}
          queryLabel={t("alert.delegationQuery")}
          query={query}
          onQuery={setQuery}
          onReset={() => setQuery("")}
        />
      )}
      {query && !shown.length && (
        <p role="status">{t("alert.noDelegations")}</p>
      )}
      {shown.map((d) => (
        <div key={d.id}>
          {d.service} · {t(actions[d.action])} ·{" "}
          {t("alert.until", { time: formatUtc(d.expires_at) })}{" "}
          <Button disabled={busy} onClick={() => void revoke(d.id)}>
            {t("alert.revoke")}
          </Button>
        </div>
      ))}
      <span role="status">{notice}</span>
    </details>
  );
}

export function AlertWorkPanel({
  services,
  site,
  alert,
  point,
}: Context & { alert: string; point: string }) {
  const t = useT();
  const [params] = useSearchParams();
  const auditParams = new URLSearchParams(params);
  auditParams.set("site", site);
  auditParams.set("alert_id", alert);
  const [work, setWork] = useState<AlertWork | null>(null),
    [failed, fail] = useState(false),
    [refresh, update] = useState(0);
  const [note, setNote] = useState(""),
    [notice, setNotice] = useState(""),
    [busy, setBusy] = useState(false);
  const [retry, setRetry] = useState<{
    body: AlertCommand;
    key: string;
  } | null>(null);
  useEffect(() => {
    const ctl = new AbortController();
    let running = false;
    async function read() {
      if (running) return;
      running = true;
      try {
        const next = await services.alertWork(
          site,
          alert,
          AbortSignal.any([ctl.signal, AbortSignal.timeout(3500)]),
        );
        if (!ctl.signal.aborted) {
          if (next.point_id !== point) throw Error();
          setWork(next);
          fail(false);
        }
      } catch {
        if (!ctl.signal.aborted) {
          setWork(null);
          fail(true);
        }
      } finally {
        running = false;
      }
    }
    void read();
    const interval = setInterval(() => void read(), 2000);
    return () => {
      ctl.abort();
      clearInterval(interval);
    };
  }, [services, site, alert, point, refresh]);
  async function send(body: AlertCommand, key: string) {
    if (busy) return;
    setBusy(true);
    setNotice("");
    try {
      await services.alertCommand(site, alert, body, key);
      setRetry(null);
      setNote("");
      setNotice(t("alert.saved"));
    } catch (e) {
      if (e instanceof HttpFailure && retryOf(e) !== "later") {
        setRetry(null);
        setNotice(
          describeError(e, {
            alert_state_conflict: "alert.changed",
            forbidden: "alert.unavailableAction",
          }),
        );
      } else {
        setRetry({ body, key });
        setNotice(t("alert.noReply"));
      }
    } finally {
      setBusy(false);
      update((n) => n + 1);
    }
  }
  return (
    <section
      className="ws-panel ws-pad ws-alert-work"
      aria-labelledby="alert-work-heading"
    >
      <h2 id="alert-work-heading">{t("alert.work")}</h2>
      {work ? (
        <>
          <p>
            <strong>{alertStateLabel(work.state)}</strong>
            {work.owner && " · " + t("alert.ownerIs", { owner: work.owner })}
          </p>
          {work.rule && (
            <p>
              {t("alert.rule", { revision: work.rule.revision })} ·{" "}
              {work.rule.source === "local" ? t("rules.local") : "ONVIF"} ·{" "}
              {t("alert.binding", { binding: work.rule.binding })}
            </p>
          )}
          {work.result && (
            <p>
              {t("alert.events.record_result")} · {work.result}
            </p>
          )}
          {work.actions.includes("record_result") && (
            <label>
              {t("alert.resultOrReason")}
              <textarea
                maxLength={1024}
                value={note}
                onChange={(e) => setNote(e.target.value)}
              />
            </label>
          )}
          <div className="ws-work-actions">
            {work.actions.includes("record_result") && (
              <Button
                disabled={busy || !!retry}
                onClick={() =>
                  void send(
                    {
                      action: "record_result",
                      version: work.version,
                      reason: t("alert.falseAlarm"),
                    },
                    crypto.randomUUID(),
                  )
                }
              >
                {t("alert.falseAlarm")}
              </Button>
            )}
            {work.actions.map((action) => (
              <Button
                key={action}
                variant={action === "close" ? "secondary" : "primary"}
                disabled={
                  busy || !!retry || (action !== "claim" && !note.trim())
                }
                onClick={() =>
                  void send(
                    {
                      action,
                      version: work.version,
                      reason:
                        action === "claim" ? t("alert.claimed") : note.trim(),
                    },
                    crypto.randomUUID(),
                  )
                }
              >
                {t(actions[action])}
              </Button>
            ))}
            {retry && (
              <Button
                disabled={busy}
                onClick={() => void send(retry.body, retry.key)}
              >
                {t("alert.resend")}
              </Button>
            )}
          </div>
          <h3>{t("alert.history")}</h3>
          <AlertHistory key={alert} work={work} site={site} />
          <RouteButton to={"/audit?" + auditParams.toString()}>
            {t("alert.openLog")}
          </RouteButton>
          <Delegations
            services={services}
            site={site}
            work={work}
            refresh={() => update((n) => n + 1)}
          />
        </>
      ) : (
        <p role="status">
          {failed ? t("alert.cardUnavailable") : t("alert.cardLoading")}
        </p>
      )}
      <p role="status">{notice}</p>
    </section>
  );
}

export function AlertAudit({
  services,
  site,
  points,
}: Context & { points: Point[] }) {
  const t = useT();
  const [params] = useSearchParams(),
    initial = params.get("alert_id") ?? "";
  const [search, setSearch] = useState<AuditFilter>(() =>
    initial ? { alert_id: initial } : {},
  );
  const [previous, setPrevious] = useState<AuditFilter[]>([]);
  const [page, setPage] = useState<{
      items: AuditEntry[];
      next_cursor?: string;
    } | null>(null),
    [error, setError] = useState(false);

  useEffect(() => {
    const ctl = new AbortController();
    setPage(null);
    setError(false);
    let running = false;
    async function read() {
      if (running) return;
      running = true;
      try {
        const data = await services.audit(
          site,
          search,
          AbortSignal.any([ctl.signal, AbortSignal.timeout(3500)]),
        );
        if (!ctl.signal.aborted) {
          setPage(data);
          setError(false);
        }
      } catch {
        if (!ctl.signal.aborted) {
          setPage(null);
          setError(true);
        }
      } finally {
        running = false;
      }
    }
    void read();
    const interval = setInterval(() => void read(), 2000);
    return () => {
      ctl.abort();
      clearInterval(interval);
    };
  }, [search, site, services]);
  const [query, setQuery] = useState("");
  const [alertId, setAlertId] = useState(initial);
  function apply(query: string, id: string) {
    setPrevious([]);
    setSearch({
      ...(query.trim() ? { query: query.trim() } : {}),
      ...(id.trim() ? { alert_id: id.trim() } : {}),
    });
  }
  return (
    <>
      <section
        className="ws-panel ws-pad ws-alert-work ws-audit"
        aria-label={t("alert.records")}
      >
        <FilterPanel
          label={t("alert.logFilters")}
          queryLabel={t("alert.logQuery")}
          query={query}
          onQuery={setQuery}
          maxLength={256}
          filtered={!!(alertId || search.query || search.alert_id)}
          onApply={() => apply(query, alertId)}
          onReset={() => {
            setQuery("");
            setAlertId("");
            apply("", "");
          }}
        >
          <label>
            {t("alert.id")}
            <input
              name="alert"
              value={alertId}
              onChange={(e) => setAlertId(e.target.value)}
              maxLength={36}
            />
          </label>
        </FilterPanel>
        <div role="region" aria-label={t("alert.results")}>
          {page ? (
            page.items.length ? (
              <Entries items={page.items} site={site} points={points} />
            ) : (
              <p role="status">
                {search.alert_id || search.query || search.before
                  ? t("alert.noRecords")
                  : t("alert.emptyLog")}
              </p>
            )
          ) : (
            <p role="status">
              {error ? t("alert.logFailed") : t("console.loadingLog")}
            </p>
          )}
          {(previous.length > 0 || page?.next_cursor) && (
            <div
              className="ws-work-actions"
              role="group"
              aria-label={t("alert.pages")}
            >
              {previous.length > 0 && (
                <Button
                  onClick={() => {
                    setSearch(previous[previous.length - 1]!);
                    setPrevious(previous.slice(0, -1));
                  }}
                >
                  {t("alert.prevPage")}
                </Button>
              )}
              {page?.next_cursor && (
                <Button
                  onClick={() => {
                    setPrevious([...previous, search]);
                    setSearch({ ...search, before: page.next_cursor! });
                  }}
                >
                  {t("alert.nextPage")}
                </Button>
              )}
            </div>
          )}
        </div>
      </section>
    </>
  );
}
