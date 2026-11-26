// Membership of the cameras of a recording group.
import { useState } from "react";
import type { CapturePolicies, Point } from "../../contracts/workspace";
import { ControlGlyph } from "../../ui-kit/FormControls";
import {
  FilterPanel,
  matchesQuery,
  needsListSearch,
} from "../../ui-kit/FilterPanel";

import { useT } from "../../i18n";
import { type CaptureGroup } from "./rules";

export function GroupMembers({
  group,
  points,
  policies,
  openCamera,
}: {
  group: CaptureGroup;
  points: Point[];
  policies: CapturePolicies;
  openCamera: (id: string) => void;
}) {
  const t = useT();
  const [query, setQuery] = useState("");
  const policy = policies.policies.find(
    (p) => p.scope === "group" && p.target === group.id,
  );
  const applies = (id: string) =>
    !!policy && policies.effective[id]?.id === policy.id;
  const overrides = new Set(
    policies.policies.filter((p) => p.scope === "point").map((p) => p.target),
  );
  const ids = new Set(group.points);
  const members = points.filter((p) => ids.has(p.id));
  const filtered = members.filter((p) => matchesQuery(query, p.name));
  return (
    <details className="rp-members">
      <summary data-policy-focus>
        <strong>{t("rp.members")}</strong>
        <span>
          {policy
            ? t("rp.appliesTo", {
                count: group.points.filter(applies).length,
                total: group.points.length,
              })
            : t("rp.groupSize", { count: group.points.length })}
        </span>
      </summary>
      {needsListSearch(members.length, query) && (
        <FilterPanel
          label={t("rp.members")}
          queryLabel={t("rp.findInGroup")}
          query={query}
          onQuery={setQuery}
          onReset={() => setQuery("")}
        />
      )}
      <ul>
        {filtered.map((p) => (
          <li key={p.id}>
            <button onClick={() => openCamera(p.id)}>
              <ControlGlyph icon="cctv" />
              <span>
                <strong>{p.name}</strong>
                <small>
                  {applies(p.id)
                    ? t("rp.byGroup")
                    : !policies.effective[p.id]
                      ? t("rp.site")
                      : overrides.has(p.id)
                        ? t("rp.individual")
                        : t("rp.byOtherGroup")}
                </small>
              </span>
              <ControlGlyph icon="chevron-right" />
            </button>
          </li>
        ))}
      </ul>
      {!filtered.length && (
        <p>
          {query
            ? t("inv.noHits")
            : group.points.length
              ? t("rp.camerasUnavailable")
              : t("rp.emptyGroup")}
        </p>
      )}
    </details>
  );
}
