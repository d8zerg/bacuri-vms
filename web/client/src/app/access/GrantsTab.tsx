// Permissions of users on points and groups.
import { Button } from "../../ui-kit/FormControls";
import { FilterPanel } from "../../ui-kit/FilterPanel";

import { actions, field, type AccessContext } from "./model";
import { People } from "./fields";

export function GrantsTab({ ctx }: { ctx: AccessContext }) {
  const {
    t,
    policy,
    save,
    form,
    refresh,
    query,
    setQuery,
    resetFilters,
    actionFilter,
    setActionFilter,
    grants,
    setMessage,
  } = ctx;
  return (
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
            <Button onClick={() => void save("grants/" + g.id, "DELETE")}>
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
          if (Boolean(start) !== Boolean(end) || (start && start >= end)) {
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
  );
}
