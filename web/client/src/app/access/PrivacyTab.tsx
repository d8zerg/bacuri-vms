import "../../i18n/extended";
// Privacy masks and their effect on viewing and export.

import { FilterPanel } from "../../ui-kit/FilterPanel";
import { PrivacyEditor } from "../PrivacyEditor";
import { type AccessContext } from "./model";

export function PrivacyTab({ ctx }: { ctx: AccessContext }) {
  const {
    t,
    save,
    refresh,
    query,
    setQuery,
    setPoint,
    matchingPoints,
    selectedPoint,
    privacy,
    busy,
  } = ctx;
  return (
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
      <div hidden={!matchingPoints?.some((p) => p.id === selectedPoint)}>
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
  );
}
