import {
  FilterPanel,
  matchesQuery,
  type ListRefresh,
} from "../ui-kit/FilterPanel";
import type { components } from "../../../../build/contracts/api";
import { formatUtc } from "../ui-kit/utc";
import { t, useT, type MessageKey } from "../i18n";
type Event = components["schemas"]["AccessAuditEvent"];
const operations = new Set([
  "export_grants",
  "external_identities",
  "group_points",
  "identity_providers",
  "point_grants",
  "privacy_policies",
  "resource_groups",
  "scoped_grants",
  "site_roles",
]);
function describe(code: string) {
  if (code === "session.login") return t("audit.login");
  if (code === "session.logout") return t("audit.logout");
  const [table, verb] = code.split(".");
  const n = ["insert", "update", "delete"].indexOf(verb ?? "");
  return table && n >= 0 && operations.has(table)
    ? t(`audit.${table}.${verb}` as MessageKey)
    : t("audit.unknown");
}

export function AccessAudit({
  refresh,
  query,
  onQuery,
  method,
  onMethod,
  events,
  users,
  groups,
  points,
}: {
  refresh: ListRefresh;
  query: string;
  onQuery: (query: string) => void;
  method: string;
  onMethod: (method: string) => void;
  events: Event[];
  users: { id: string; username: string }[];
  groups: { id: string; name: string }[];
  points: { id: string; name: string }[];
}) {
  const t = useT();
  const names = new Map([
    ...users.map((u) => [u.id, u.username] as const),
    ...[...groups, ...points].map((r) => [r.id, r.name] as const),
  ]);
  const rows = events
    .map((e) => {
      const actor =
        users.find((u) => u.id === e.actor)?.username ??
        (e.actor === "local-console"
          ? t("audit.localConsole")
          : t("audit.unknownActor"));
      const target = e.resource
        .split("/")
        .filter((id) => !(e.action.startsWith("session.") && id === e.actor))
        .map((id) => names.get(id))
        .filter(Boolean)
        .join(" · ");
      return { e, actor, target };
    })
    .filter(
      ({ e, actor, target }) =>
        (!method || e.method === method) &&
        matchesQuery(
          query,
          actor,
          target,
          e.actor,
          e.resource,
          describe(e.action),
          e.action,
        ),
    );
  return (
    <>
      <FilterPanel
        refresh={refresh}
        label={t("audit.filters")}
        queryLabel={
          events.length === 200 ? t("audit.searchRecent") : t("audit.query")
        }
        query={query}
        onQuery={onQuery}
        filtered={!!method}
        onReset={() => {
          onQuery("");
          onMethod("");
        }}
      >
        <label>
          {t("audit.method")}
          <select value={method} onChange={(e) => onMethod(e.target.value)}>
            <option value="">{t("audit.allMethods")}</option>
            {[...new Set(events.map((e) => e.method))].map((m) => (
              <option key={m}>{m}</option>
            ))}
          </select>
        </label>
      </FilterPanel>
      {!rows.length && (
        <p role="status">
          {events.length ? t("audit.notFound") : t("audit.empty")}
        </p>
      )}
      <ol className="ws-access-audit">
        {rows.map(({ e, actor, target }) => {
          return (
            <li key={e.id}>
              <details>
                <summary>
                  <strong>
                    {describe(e.action)}
                    {target && " · " + target}
                  </strong>
                  <span>
                    {actor} ·{" "}
                    <time dateTime={e.recorded_at}>
                      {formatUtc(e.recorded_at)}
                    </time>
                  </span>
                </summary>
                <dl>
                  <dt>{t("audit.action")}</dt>
                  <dd>{e.action}</dd>
                  <dt>{t("audit.resource")}</dt>
                  <dd>{e.resource}</dd>
                  <dt>{t("audit.actor")}</dt>
                  <dd>{e.actor}</dd>
                  <dt>{t("audit.method")}</dt>
                  <dd>{e.method}</dd>
                </dl>
              </details>
            </li>
          );
        })}
      </ol>
    </>
  );
}
