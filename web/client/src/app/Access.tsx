import "../i18n/extended";
import { FilterPanel, matchesQuery } from "../ui-kit/FilterPanel";
import { useEffect, useState } from "react";
import type { FormEvent } from "react";

import type { Session } from "../contracts/workspace";
import { api, data, HttpFailure } from "../adapters/api";
import { useResource } from "../adapters/resource";
import { describeError } from "../adapters/errors";
import { SubsectionMenu } from "./SubsectionMenu";

import { AccessAudit } from "./AccessAudit";

import "./access.css";
import { t as translate, useT } from "../i18n";

import {
  actions,
  tabs,
  type AccessContext,
  type Policy,
  type Tab,
} from "./access/model";
import { UsersTab } from "./access/UsersTab";
import { GroupsTab } from "./access/GroupsTab";
import { GrantsTab } from "./access/GrantsTab";
import { PrivacyTab } from "./access/PrivacyTab";
import { LoginTab } from "./access/LoginTab";

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
  const access = useResource(session, "access:" + site, (signal) =>
    load(site, session.csrf_token, signal),
  );
  // A refused read means that the session or the administrator role ended.
  const refused =
    access.error instanceof HttpFailure &&
    (access.error.status === 401 || access.error.status === 403);
  useEffect(() => {
    if (refused) expire();
  }, [refused, expire]);
  const policy: Policy | null = access.error ? null : (access.value ?? null),
    loading = access.loading;
  const [busy, setBusy] = useState(false),
    [notice, setMessage] = useState("");
  const message =
    notice || (access.error && !refused ? t("access.loadFailed") : "");
  const [group, setGroup] = useState(""),
    [point, setPoint] = useState("");

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
      if (!response.ok) {
        setMessage(
          describeError(await HttpFailure.fromResponse(response), {
            invalid_request: "access.saveFailed",
          }),
        );
      } else setMessage(t("access.saved"));
      access.reload();
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
      access.reload();
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
  const ctx: AccessContext | null = policy
    ? {
        t,
        site,
        session,
        save,
        form,
        refresh,
        query,
        setQuery,
        resetFilters,
        pointQuery,
        setPointQuery,
        roleFilter,
        setRoleFilter,
        actionFilter,
        setActionFilter,
        group,
        setGroup,
        point,
        setPoint,
        selected,
        selectedPoint,
        privacy,
        setMessage,
        busy,
        policy,
        users: users ?? [],
        grants: grants ?? [],
        identities: identities ?? [],
        shownIdentities: shownIdentities ?? [],
        matchingGroups: matchingGroups ?? [],
        matchingPoints: matchingPoints ?? [],
      }
    : null;
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
        ctx && (
          <fieldset
            aria-label={t(`access.tab.${tab}`)}
            disabled={busy}
            onChangeCapture={() => setMessage("")}
            key={site + ":" + tab + ":" + policy.revision}
          >
            {tab === "users" && <UsersTab ctx={ctx} />}
            {tab === "groups" && <GroupsTab ctx={ctx} />}
            {tab === "grants" && <GrantsTab ctx={ctx} />}
            {tab === "privacy" && <PrivacyTab ctx={ctx} />}
            {tab === "login" && <LoginTab ctx={ctx} />}
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
        )
      )}
    </section>
  );
}
