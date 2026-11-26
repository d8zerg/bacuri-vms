// Local accounts, their roles and blocking.
import { Button } from "../../ui-kit/FormControls";
import { FilterPanel } from "../../ui-kit/FilterPanel";

import { field, type AccessContext } from "./model";
import { Roles } from "./fields";

export function UsersTab({ ctx }: { ctx: AccessContext }) {
  const {
    t,
    save,
    form,
    refresh,
    query,
    setQuery,
    resetFilters,
    roleFilter,
    setRoleFilter,
    users,
  } = ctx;
  return (
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
            <option value="operator">{t("access.role.operator")}</option>
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
  );
}
