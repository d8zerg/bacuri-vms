import {
  FilterPanel,
  matchesQuery,
  needsListSearch,
} from "../ui-kit/FilterPanel";
import { useEffect, useState } from "react";
import type { FormEvent } from "react";
import type { components } from "../../../../build/contracts/api";
import type { Point, Session } from "../contracts/workspace";
import { api, data } from "../adapters/api";
import { SubsectionMenu } from "./SubsectionMenu";
import { Button } from "../ui-kit/FormControls";
import { AccessAudit } from "./AccessAudit";
import { PrivacyEditor } from "./PrivacyEditor";
import "./access.css";
import { t as translate, useT, type MessageKey } from "../i18n";

type Schema = components["schemas"];
type User = Schema["AccessUser"];
type Group = Schema["AccessGroup"];
type Grant = Schema["AccessGrant"];
const actions = {
  view: "access.action.view",
  audio: "access.action.audio",
  record: "access.action.record",
  audio_record: "access.action.audio_record",
  audio_talk: "access.action.audio_talk",
  ptz: "access.action.ptz",
  configure: "access.action.configure",
  protect: "access.action.protect",
  export: "access.action.export",
  export_masked: "access.action.export_masked",
  view_ptz: "access.action.view_ptz",
} as const satisfies Record<string, MessageKey>;
const tabs = [
  ["users", "user-round"],
  ["groups", "users-round"],
  ["grants", "key-round"],
  ["privacy", "eye-off"],
  ["login", "log-in"],
  ["audit", "clipboard-list"],
] as const;
type Tab = (typeof tabs)[number][0];
interface Policy {
  revision: string;
  users: User[];
  groups: Group[];
  grants: Grant[];
  privacy: Schema["PrivacyEntry"][];
  provider: Schema["ProviderInfo"];
  audit: Schema["AccessAuditEvent"][];
  points: Point[];
}
async function load(
  site: string,
  csrf: string,
  signal: AbortSignal,
): Promise<Policy> {
  const params = { path: { siteId: site } };
  const [users, groups, grants, privacy, provider, audit, points] =
    await Promise.all([
      api.GET("/sites/{siteId}/users", { params, signal }).then(data),
      api.GET("/sites/{siteId}/resource-groups", { params, signal }).then(data),
      api.GET("/sites/{siteId}/grants", { params, signal }).then(data),
      api
        .GET("/sites/{siteId}/privacy-policies", { params, signal })
        .then(data),
      api
        .GET("/sites/{siteId}/identity/provider", { params, signal })
        .then(data),
      api
        .POST("/sites/{siteId}/audit/search", {
          params: { ...params, header: { "X-CSRF-Token": csrf } },
          body: {},
          signal,
        })
        .then(data),
      api.GET("/sites/{siteId}/points", { params, signal }).then(data),
    ]);
  if (
    ![groups, grants, privacy, provider, audit].every(
      (r) => r.revision === users.revision,
    )
  )
    throw Error(translate("access.changedDuringLoad"));
  return {
    revision: users.revision,
    users: users.data,
    groups: groups.data,
    grants: grants.data,
    privacy: privacy.data,
    provider: provider.data,
    audit: audit.data,
    points,
  };
}
const field = (form: FormData, key: string) => {
  const value = form.get(key);
  return typeof value === "string" ? value : "";
};
function Roles({ value = "operator" }: { value?: string }) {
  const t = useT();
  return (
    <label>
      {t("access.role")}
      <select name="role" defaultValue={value}>
        <option value="operator">{t("access.role.operator")}</option>
        <option value="administrator">{t("access.role.administrator")}</option>
      </select>
    </label>
  );
}
function People({ users }: { users: User[] }) {
  const t = useT();
  return (
    <label>
      {t("access.user")}
      <select required name="user">
        {users.map((u) => (
          <option value={u.id} key={u.id}>
            {u.username}
          </option>
        ))}
      </select>
    </label>
  );
}

export default function Access({
  site,
  session,
  expire,
}: {
  site: string;
  session: Session;
  expire: () => void;
}) {
  const t = useT();
  const [tab, setTab] = useState<Tab>("users");
  const [loading, setLoading] = useState(true);
  const [policy, setPolicy] = useState<Policy | null>(null);
  const [attempt, setAttempt] = useState(0),
    [busy, setBusy] = useState(false),
    [message, setMessage] = useState("");
  const [group, setGroup] = useState(""),
    [point, setPoint] = useState("");
  useEffect(() => {
    const ctl = new AbortController();
    setLoading(true);
    void load(site, session.csrf_token, ctl.signal)
      .then((p) => {
        if (!ctl.signal.aborted) setPolicy(p);
      })
      .catch((err: unknown) => {
        if (ctl.signal.aborted) return;
        setPolicy(null);
        if (err instanceof Error && ["401", "403"].includes(err.message))
          expire();
        else setMessage(translate("access.loadFailed"));
      })
      .finally(() => {
        if (!ctl.signal.aborted) setLoading(false);
      });
    return () => ctl.abort();
  }, [site, session.csrf_token, attempt, expire]);

  useEffect(() => {
    const controller = new AbortController();
    const timer = window.setInterval(() => {
      void api
        .GET("/session", { signal: controller.signal })
        .then((result) => {
          if (
            !controller.signal.aborted &&
            (result.response.status === 401 ||
              (result.data &&
                !result.data.site_roles.some(
                  (r) => r.site_id === site && r.role === "administrator",
                )))
          )
            expire();
        })
        .catch(() => {});
    }, 4000);
    return () => {
      controller.abort();
      window.clearInterval(timer);
    };
  }, [site, expire]);

  async function save(path: string, method: string, body?: unknown) {
    if (!policy || busy) return;
    setBusy(true);
    setMessage("");
    try {
      const response = await fetch("/api/v1/sites/" + site + "/" + path, {
        method,
        credentials: "same-origin",
        cache: "no-store",
        headers: {
          "Content-Type": "application/json",
          "X-CSRF-Token": session.csrf_token,
          "If-Match": '"' + policy.revision + '"',
        },
        body: body === undefined ? null : JSON.stringify(body),
      });
      if (response.status === 401 || response.status === 403) {
        expire();
        return;
      }
      if (response.status === 412) {
        setMessage(t("access.conflict"));
      } else if (!response.ok) {
        const problem = (await response.json()) as { code: string };
        setMessage(
          (
            {
              last_administrator: t("access.lastAdministrator"),
              account_conflict: t("access.accountConflict"),
              identity_conflict: t("access.identityConflict"),
              group_conflict: t("access.groupConflict"),
            } as Record<string, string>
          )[problem.code] ?? t("access.saveFailed"),
        );
      } else setMessage(t("access.saved"));
      setAttempt((n) => n + 1);
    } catch {
      setMessage(t("access.disconnected"));
    } finally {
      setBusy(false);
    }
  }
  function form(action: (f: FormData) => void) {
    return (e: FormEvent<HTMLFormElement>) => {
      e.preventDefault();
      action(new FormData(e.currentTarget));
    };
  }
  const [query, setQuery] = useState("");
  const [pointQuery, setPointQuery] = useState("");
  const [auditMethod, setAuditMethod] = useState("");
  const [roleFilter, setRoleFilter] = useState("");
  const [actionFilter, setActionFilter] = useState("");
  function resetFilters() {
    setQuery("");
    setPointQuery("");
    setAuditMethod("");
    setRoleFilter("");
    setActionFilter("");
  }
  const users = policy?.users.filter(
    (u) =>
      (!roleFilter || u.role === roleFilter) &&
      matchesQuery(
        query,
        u.username,
        u.enabled ? t("access.active") : t("access.blocked"),
      ),
  );
  const grants = policy?.grants.filter(
    (g) =>
      (!actionFilter || g.action === actionFilter) &&
      matchesQuery(
        query,
        policy.users.find((u) => u.id === g.user_id)?.username,
        t(actions[g.action]),
        policy.points.find((p) => p.id === g.point_id)?.name,
        policy.groups.find((r) => r.id === g.group_id)?.name,
      ),
  );
  const identities = policy?.users.filter((u) => u.subject);
  const shownIdentities = identities?.filter((u) =>
    matchesQuery(query, u.username, u.issuer, u.subject),
  );
  const refresh = {
    label: t("access.refresh"),
    busy: loading,
    disabled: busy,
    run: () => {
      setMessage("");
      setAttempt((n) => n + 1);
    },
  };
  const matchingGroups = policy?.groups.filter((g) =>
    matchesQuery(query, g.name),
  );
  const matchingPoints = policy?.points.filter((p) =>
    matchesQuery(query, p.name),
  );
  const selected = policy?.groups.find((g) => g.id === group);
  const selectedPoint = point || policy?.points[0]?.id || "";
  const privacy = policy?.privacy.find((p) => p.point_id === selectedPoint);
  return (
    <section className="ws-panel ws-pad ws-access">
      <SubsectionMenu
        label={t("access.title")}
        items={tabs.map(([id, icon]) => ({
          label: t(`access.tab.${id}`),
          icon,
          active: tab === id,
          select: () => {
            if (id !== tab) resetFilters();
            setTab(id);
            setMessage("");
          },
        }))}
      />
      <p role="status">{message}</p>
      {!policy ? (
        <>
          <FilterPanel
            label={t("access.filters")}
            queryLabel={t("inv.searching")}
            query={query}
            onQuery={setQuery}
            onReset={resetFilters}
            searchDisabled
            refresh={refresh}
          />
          {loading && <p role="status">{t("access.waiting")}</p>}
        </>
      ) : (
        <fieldset
          aria-label={t(`access.tab.${tab}`)}
          disabled={busy}
          onChangeCapture={() => setMessage("")}
          key={site + ":" + tab + ":" + policy.revision}
        >
          {tab === "users" && (
            <>
              <FilterPanel
                refresh={refresh}
                label={t("access.userFilters")}
                queryLabel={t("access.userSearch")}
                query={query}
                onQuery={setQuery}
                onReset={resetFilters}
                filtered={!!roleFilter}
              >
                <label>
                  {t("access.roleFilter")}
                  <select
                    value={roleFilter}
                    onChange={(e) => setRoleFilter(e.target.value)}
                  >
                    <option value="">{t("access.allRoles")}</option>
                    <option value="operator">
                      {t("access.role.operator")}
                    </option>
                    <option value="administrator">
                      {t("access.role.administrator")}
                    </option>
                  </select>
                </label>
              </FilterPanel>
              <details className="ws-new-user" name="access-user">
                <summary>{t("access.addUser")}</summary>
                <form
                  onSubmit={form(
                    (f) =>
                      void save("users", "POST", {
                        username: field(f, "name"),
                        password: field(f, "password"),
                        role: field(f, "role"),
                      }),
                  )}
                >
                  <label>
                    {t("access.name")}
                    <input required name="name" maxLength={128} />
                  </label>
                  <label>
                    {t("access.initialPassword")}
                    <input
                      required
                      type="password"
                      autoComplete="new-password"
                      name="password"
                    />
                  </label>
                  <Roles />
                  <Button type="submit">{t("access.createUser")}</Button>
                </form>
              </details>
              {!users?.length && <p role="status">{t("access.noUsers")}</p>}
              <ul className="ws-users">
                {users?.map((u) => (
                  <li key={u.id}>
                    <details name="access-user">
                      <summary>
                        <strong>{u.username}</strong>
                        <span>
                          {u.role === "administrator"
                            ? t("access.role.administrator")
                            : t("access.role.operator")}
                        </span>
                        <span>
                          {u.enabled ? t("access.active") : t("access.blocked")}
                        </span>
                      </summary>
                      <form
                        aria-label={t("access.editUser", { name: u.username })}
                        onSubmit={form(
                          (f) =>
                            void save("users/" + u.id, "PATCH", {
                              role: field(f, "role"),
                              enabled: f.has("enabled"),
                            }),
                        )}
                      >
                        <Roles value={u.role} />
                        <label>
                          <input
                            type="checkbox"
                            name="enabled"
                            defaultChecked={u.enabled}
                          />
                          {t("access.accountActive")}
                        </label>
                        <Button type="submit">{t("access.saveUser")}</Button>
                      </form>
                    </details>
                  </li>
                ))}
              </ul>
            </>
          )}
          {tab === "groups" && (
            <>
              <FilterPanel
                label={t("access.groupFilters")}
                queryLabel={t("access.groupSearch")}
                query={query}
                onQuery={setQuery}
                onReset={() => setQuery("")}
                refresh={refresh}
              >
                <label>
                  {t("access.resourceGroup")}
                  <select
                    value={
                      matchingGroups?.some((g) => g.id === group) ? group : ""
                    }
                    onChange={(e) => setGroup(e.target.value)}
                  >
                    <option value="">
                      {query ? t("access.chooseGroup") : t("access.newGroup")}
                    </option>
                    {matchingGroups?.map((g) => (
                      <option key={g.id} value={g.id}>
                        {g.name}
                      </option>
                    ))}
                  </select>
                </label>
              </FilterPanel>
              {query && !matchingGroups?.length && (
                <p role="status">{t("access.noGroups")}</p>
              )}
              <form
                hidden={!!query && !matchingGroups?.some((g) => g.id === group)}
                key={group}
                onSubmit={form(
                  (f) =>
                    void save(
                      "resource-groups" + (group ? "/" + group : ""),
                      group ? "PUT" : "POST",
                      { name: field(f, "name"), points: f.getAll("points") },
                    ),
                )}
              >
                <label>
                  {t("camera.form.name")}
                  <input name="name" required defaultValue={selected?.name} />
                </label>
                <fieldset>
                  <legend>{t("maps.points")}</legend>
                  {needsListSearch(policy.points.length, pointQuery) && (
                    <FilterPanel
                      label={t("access.groupPointFilters")}
                      queryLabel={t("maps.pointSearch")}
                      query={pointQuery}
                      onQuery={setPointQuery}
                      onReset={() => setPointQuery("")}
                    />
                  )}
                  {pointQuery &&
                    !policy.points.some((p) =>
                      matchesQuery(pointQuery, p.name),
                    ) && <p role="status">{t("maps.noPoints")}</p>}
                  {policy.points.map((p) => (
                    <label
                      key={p.id}
                      hidden={!matchesQuery(pointQuery, p.name)}
                    >
                      <input
                        type="checkbox"
                        name="points"
                        value={p.id}
                        defaultChecked={selected?.points.includes(p.id)}
                      />
                      {p.name}
                    </label>
                  ))}
                </fieldset>
                <Button type="submit">{t("access.saveGroup")}</Button>
              </form>
            </>
          )}
          {tab === "grants" && (
            <>
              <FilterPanel
                refresh={refresh}
                label={t("access.grantFilters")}
                queryLabel={t("access.grantQuery")}
                query={query}
                onQuery={setQuery}
                filtered={!!actionFilter}
                onReset={resetFilters}
              >
                <label>
                  {t("access.actionFilter")}
                  <select
                    value={actionFilter}
                    onChange={(e) => setActionFilter(e.target.value)}
                  >
                    <option value="">{t("access.allActions")}</option>
                    {Object.entries(actions).map(([id, title]) => (
                      <option key={id} value={id}>
                        {t(title)}
                      </option>
                    ))}
                  </select>
                </label>
              </FilterPanel>
              {!grants?.length && (
                <p role="status">
                  {policy.grants.length
                    ? t("access.noGrantsFound")
                    : t("access.noGrants")}
                </p>
              )}
              <ul>
                {grants?.map((g) => (
                  <li key={g.id}>
                    {policy.users.find((u) => u.id === g.user_id)?.username} ·{" "}
                    {t(actions[g.action])} ·{" "}
                    {policy.points.find((p) => p.id === g.point_id)?.name ??
                      policy.groups.find((r) => r.id === g.group_id)?.name}
                    {g.begin_us &&
                      g.end_us &&
                      " · " +
                        new Date(Number(g.begin_us) / 1000).toLocaleString() +
                        " - " +
                        new Date(Number(g.end_us) / 1000).toLocaleString()}
                    <Button
                      onClick={() => void save("grants/" + g.id, "DELETE")}
                    >
                      {t("alert.revoke")}
                    </Button>
                  </li>
                ))}
              </ul>
              <form
                onSubmit={form((f) => {
                  const target = field(f, "target"),
                    start = field(f, "start"),
                    end = field(f, "end");
                  if (
                    Boolean(start) !== Boolean(end) ||
                    (start && start >= end)
                  ) {
                    setMessage(t("access.checkInterval"));
                    return;
                  }
                  void save("grants/" + crypto.randomUUID(), "PUT", {
                    user_id: field(f, "user"),
                    action: field(f, "action"),
                    [target.startsWith("g:") ? "group_id" : "point_id"]:
                      target.slice(2),
                    ...(start && end
                      ? {
                          interval: {
                            start: new Date(start).toISOString(),
                            end: new Date(end).toISOString(),
                          },
                        }
                      : {}),
                  });
                })}
              >
                <People users={policy.users} />
                <label>
                  {t("access.scope")}
                  <select name="target" required>
                    {policy.points.map((p) => (
                      <option key={p.id} value={"p:" + p.id}>
                        {p.name}
                      </option>
                    ))}
                    {policy.groups.map((g) => (
                      <option key={g.id} value={"g:" + g.id}>
                        {t("access.groupNamed", { name: g.name })}
                      </option>
                    ))}
                  </select>
                </label>
                <label>
                  {t("access.action")}
                  <select name="action">
                    {Object.entries(actions)
                      .filter(([k]) => k !== "view_ptz")
                      .map(([k, v]) => (
                        <option key={k} value={k}>
                          {t(v)}
                        </option>
                      ))}
                  </select>
                </label>
                <label>
                  {t("access.intervalStart")}
                  <input type="datetime-local" name="start" />
                </label>
                <label>
                  {t("access.intervalEnd")}
                  <input type="datetime-local" name="end" />
                </label>
                <Button type="submit">{t("access.grant")}</Button>
              </form>
            </>
          )}
          {tab === "privacy" && (
            <>
              <FilterPanel
                label={t("access.privacyFilters")}
                queryLabel={t("maps.pointSearch")}
                query={query}
                onQuery={setQuery}
                onReset={() => setQuery("")}
                refresh={refresh}
              >
                <label>
                  {t("batch.point")}
                  <select
                    value={
                      matchingPoints?.some((p) => p.id === selectedPoint)
                        ? selectedPoint
                        : ""
                    }
                    onChange={(e) => setPoint(e.target.value)}
                  >
                    {!matchingPoints?.some((p) => p.id === selectedPoint) && (
                      <option value="">{t("access.choosePoint")}</option>
                    )}
                    {matchingPoints?.map((p) => (
                      <option key={p.id} value={p.id}>
                        {p.name}
                      </option>
                    ))}
                  </select>
                </label>
              </FilterPanel>
              {query && !matchingPoints?.length && (
                <p role="status">{t("maps.noPoints")}</p>
              )}
              <div
                hidden={!matchingPoints?.some((p) => p.id === selectedPoint)}
              >
                <PrivacyEditor
                  key={selectedPoint}
                  available={!!selectedPoint && !busy}
                  policy={privacy}
                  save={(body) =>
                    void save("privacy-policies/" + selectedPoint, "PUT", body)
                  }
                />
              </div>
            </>
          )}
          {tab === "login" && (
            <>
              <FilterPanel
                refresh={refresh}
                label={t("access.identityFilters")}
                queryLabel={t("access.identityQuery")}
                query={query}
                onQuery={setQuery}
                onReset={() => setQuery("")}
              />
              {!shownIdentities?.length && (
                <p role="status">
                  {query
                    ? t("access.noIdentitiesFound")
                    : t("access.noIdentities")}
                </p>
              )}
              <ul>
                {shownIdentities?.map((u) => (
                  <li key={u.id}>
                    {u.username} · {u.issuer} · {u.subject}{" "}
                    <Button
                      onClick={() =>
                        void save(
                          "users/" + u.id + "/external-identity",
                          "DELETE",
                        )
                      }
                    >
                      {t("access.unlink")}
                    </Button>
                  </li>
                ))}
              </ul>
              <form
                onSubmit={form((f) => {
                  const secret = field(f, "secret");
                  void save("identity/provider", "PUT", {
                    enabled: f.has("enabled"),
                    issuer: field(f, "issuer"),
                    client_id: field(f, "client"),
                    admin_acr: field(f, "acr"),
                    ...(secret ? { client_secret: secret } : {}),
                  });
                })}
              >
                <h2>OpenID Connect</h2>
                <label>
                  <input
                    type="checkbox"
                    name="enabled"
                    defaultChecked={policy.provider.enabled}
                  />
                  {t("access.keycloak")}
                </label>
                <label>
                  Issuer HTTPS
                  <input
                    name="issuer"
                    type="url"
                    required
                    defaultValue={policy.provider.issuer}
                  />
                </label>
                <label>
                  Client ID
                  <input
                    name="client"
                    required
                    defaultValue={policy.provider.client_id}
                  />
                </label>
                <label>
                  {t("access.clientSecret")}
                  <input
                    type="password"
                    autoComplete="new-password"
                    name="secret"
                    required={!policy.provider.issuer}
                  />
                </label>
                <label>
                  {t("access.mfa")}
                  <input
                    required
                    name="acr"
                    defaultValue={policy.provider.admin_acr}
                  />
                </label>
                <Button type="submit">{t("access.saveProvider")}</Button>
              </form>
              <form
                onSubmit={form(
                  (f) =>
                    void save(
                      "users/" + field(f, "user") + "/external-identity",
                      "PUT",
                      {
                        issuer: policy.provider.issuer,
                        subject: field(f, "subject"),
                      },
                    ),
                )}
              >
                <h2>{t("access.externalAccount")}</h2>
                <People users={policy.users} />
                <label>
                  {t("access.subject")}
                  <input required name="subject" />
                </label>
                <Button type="submit" disabled={!policy.provider.issuer}>
                  {t("access.link")}
                </Button>
              </form>
            </>
          )}
          {tab === "audit" && (
            <AccessAudit
              refresh={refresh}
              query={query}
              onQuery={setQuery}
              method={auditMethod}
              onMethod={setAuditMethod}
              events={policy.audit}
              users={policy.users}
              groups={policy.groups}
              points={policy.points}
            />
          )}
        </fieldset>
      )}
    </section>
  );
}
