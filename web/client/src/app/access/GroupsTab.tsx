// Resource groups of observation points.
import { Button } from "../../ui-kit/FormControls";
import {
  FilterPanel,
  matchesQuery,
  needsListSearch,
} from "../../ui-kit/FilterPanel";

import { field, type AccessContext } from "./model";

export function GroupsTab({ ctx }: { ctx: AccessContext }) {
  const {
    t,
    policy,
    save,
    form,
    refresh,
    query,
    setQuery,
    pointQuery,
    setPointQuery,
    group,
    setGroup,
    matchingGroups,
    selected,
  } = ctx;
  return (
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
            value={matchingGroups?.some((g) => g.id === group) ? group : ""}
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
            !policy.points.some((p) => matchesQuery(pointQuery, p.name)) && (
              <p role="status">{t("maps.noPoints")}</p>
            )}
          {policy.points.map((p) => (
            <label key={p.id} hidden={!matchesQuery(pointQuery, p.name)}>
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
  );
}
