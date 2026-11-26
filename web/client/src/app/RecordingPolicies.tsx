import { useEffect, useRef, useState } from "react";
import type { Point, Services } from "../contracts/workspace";
import { IconButton } from "../ui-kit/FormControls";
import {
  FilterPanel,
  FilterSegments,
  matchesQuery,
} from "../ui-kit/FilterPanel";
import "./recording-policies.css";
import { useResource } from "../adapters/resource";

import { useT } from "../i18n";
import { CameraSummary } from "./recording-policies/CameraSummary";
import { GroupMembers } from "./recording-policies/GroupMembers";
import { Editor } from "./recording-policies/Editor";
import { Volume } from "./recording-policies/Volume";
import { modes } from "./recording-policies/rules";

export default function RecordingPolicies({
  site,
  points,
  services,
}: {
  site: string;
  points: Point[];
  services: Services;
}) {
  const t = useT();
  const [view, setView] = useState("point"),
    [query, search] = useState(""),
    [chosen, choose] = useState(""),
    [editing, edit] = useState(""),
    [version, refresh] = useState(0);
  const data = useResource(
    services,
    "recording-policies:" + site,
    async (signal) => {
      const [policies, storage, groups] = await Promise.all([
        services.capturePolicies(site, signal),
        services.captureStorage(site, signal),
        services.captureGroups(site, signal),
      ]);
      return { policies, storage, groups };
    },
  );
  const result = data.value ?? null,
    error = !!data.error,
    busy = data.loading;
  // A refresh reads the policies again and resets the storage form to the read values.
  function reload() {
    data.reload();
    refresh((count) => count + 1);
  }
  const items = (view === "point" ? points : (result?.groups ?? [])).filter(
    (p) => matchesQuery(query, p.name),
  );
  const item = items.find((p) => p.id === chosen) ?? items[0];
  const policies = result?.policies.policies ?? [],
    direct = policies.find((p) => p.target === item?.id && p.scope === view);
  const applied =
    item && view === "point" ? result?.policies.effective[item.id] : undefined;
  const group = policies.find(
    (p) => p.id === applied?.id && p.scope === "group",
  );
  const selectedGroup = result?.groups.find(
    (g) => g.id === item?.id && view === "group",
  );
  const detail = useRef<HTMLDivElement>(null);
  const focusDetail = useRef(false);
  useEffect(() => {
    if (focusDetail.current && detail.current) {
      const heading = detail.current.querySelector<HTMLElement>(
        "[data-policy-focus]",
      );
      if (heading) {
        heading.focus({ preventScroll: true });
        const bounds = heading.getBoundingClientRect();
        if (bounds.top < 0 || bounds.bottom > window.innerHeight)
          heading.scrollIntoView({ block: "nearest" });
      }
      focusDetail.current = false;
    }
  }, [view, item?.id, editing]);

  function editCamera(id: string) {
    focusDetail.current = true;
    edit(id);
  }

  function navigate(scope: string, id = "") {
    if (!scope) return;
    focusDetail.current = !!id;
    setView(scope);
    choose(id);
    search("");
    edit("");
  }
  return (
    <section
      className="ws-panel ws-pad rp-workspace"
      aria-label={t("rp.title")}
    >
      <FilterSegments
        label={t("rp.scope")}
        value={view}
        options={[
          { value: "point", label: t("rp.cameras") },
          { value: "group", label: t("access.tab.groups") },
          { value: "volume", label: t("rp.volume") },
        ]}
        onChange={navigate}
      />
      {view !== "volume" && (
        <FilterPanel
          label={t("rp.filter")}
          queryLabel={
            view === "point" ? t("layout.findCamera") : t("rp.findGroup")
          }
          query={query}
          onQuery={(text) => {
            search(text);
            edit("");
          }}
          onReset={() => search("")}
          refresh={{
            label: t("rp.refresh"),
            run: () => reload(),
            busy,
          }}
        />
      )}
      {view === "volume" && (
        <IconButton
          icon="rotate-cw"
          label={t("rp.refreshVolume")}
          onClick={() => reload()}
        />
      )}
      {error && <p role="alert">{t("rp.loadFailed")}</p>}
      {!result && busy && <p role="status">{t("action.loading")}</p>}
      {result &&
        (view === "volume" ? (
          <Volume
            key={version}
            value={result.storage}
            save={async (d, r) => {
              const storage = await services.setCaptureStorage(
                site,
                d,
                r,
                result.storage.revision,
              );
              data.set({ ...result, storage });
            }}
          />
        ) : (
          <div className="rp-split">
            <div
              className="rp-targets"
              aria-label={
                view === "point" ? t("rp.cameras") : t("access.tab.groups")
              }
            >
              {items.map((p) => {
                const rule =
                  view === "point"
                    ? result.policies.effective[p.id]?.rule
                    : policies.find(
                        (x) => x.scope === "group" && x.target === p.id,
                      )?.rule;
                return (
                  <button
                    type="button"
                    key={p.id}
                    aria-pressed={p.id === item?.id}
                    onClick={() => {
                      if (p.id !== item?.id) {
                        choose(p.id);
                        edit("");
                      }
                    }}
                  >
                    <strong>{p.name}</strong>
                    <small>
                      {rule
                        ? modes(rule)
                        : view === "point"
                          ? t("rp.continuous")
                          : t("rp.noPolicy")}
                    </small>
                  </button>
                );
              })}
              {!items.length && (
                <p>
                  {query
                    ? t("inv.noHits")
                    : view === "group"
                      ? t("rp.noGroups")
                      : t("rp.noCameras")}
                </p>
              )}
            </div>
            {item && (
              <div
                className="rp-detail"
                key={view + "/" + item.id}
                ref={detail}
                role="group"
                aria-label={item.name}
              >
                {selectedGroup && (
                  <GroupMembers
                    group={selectedGroup}
                    points={points}
                    policies={result.policies}
                    openCamera={(id) => navigate("point", id)}
                  />
                )}
                {view === "point" && editing !== item.id ? (
                  <CameraSummary
                    point={item}
                    policy={direct ?? group}
                    group={
                      group && result.groups.find((g) => g.id === group.target)
                    }
                    storage={result.storage}
                    edit={() => editCamera(item.id)}
                    openGroup={(id) => navigate("group", id)}
                  />
                ) : (
                  <Editor
                    key={[
                      direct?.id,
                      direct?.revision,
                      applied?.id,
                      applied?.revision,
                    ].join("/")}
                    scope={view === "point" ? "point" : "group"}
                    target={item.id}
                    name={item.name}
                    site={site}
                    services={services}
                    policy={direct}
                    inherited={applied?.rule}
                    done={() => {
                      if (view === "point") editCamera("");
                      reload();
                    }}
                    {...(view === "point"
                      ? { cancel: () => editCamera("") }
                      : {})}
                  />
                )}
              </div>
            )}
          </div>
        ))}
    </section>
  );
}
