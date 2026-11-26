import { useState } from "react";
import {
  FilterPanel,
  matchesQuery,
  type ListRefresh,
} from "../ui-kit/FilterPanel";
import type { components } from "../../../../build/contracts/api";
import { Button, StatusIndicator } from "../ui-kit/FormControls";
import { HttpFailure } from "../adapters/api";
import { cameraError } from "./cameraError";

import { channelName, groupChannels } from "./cameraChannels";
import { useT, type MessageKey } from "../i18n";

type Receipt = components["schemas"]["CameraBatchReceipt"];

const labels = {
  prepared: "batch.receipt.prepared",
  applying: "batch.receipt.applying",
  failed: "batch.receipt.failed",
  saved: "batch.receipt.saved",
  rolling_back: "batch.receipt.rollingBack",
  rolled_back: "batch.receipt.rolledBack",
  rollback_failed: "batch.receipt.rollbackFailed",
} as const satisfies Record<string, MessageKey>;

function ReceiptStatus({ row }: { row: Receipt }) {
  const t = useT();
  const { current, revision, state } = row;
  let text = t(labels[state]);
  let tone: "neutral" | "success" | "danger" =
    state === "failed" || state === "rollback_failed" ? "danger" : "neutral";
  if (state === "saved" && current) {
    if (current.desired_revision !== revision) text = t("batch.newer");
    else if (current.state === "rejected") {
      text = t("batch.recorderRejected");
      tone = "danger";
    } else if (
      current.state === "applied" &&
      current.applied_revision === revision
    ) {
      text = t("batch.recorderApplied");
      tone = "success";
    } else text = t("batch.recorderWaiting");
  }
  return <StatusIndicator tone={tone}>{text}</StatusIndicator>;
}

function ChangeValue({
  before,
  after,
}: {
  before?: string | undefined;
  after: string;
}) {
  return (
    <dd>
      {before && before !== after && <del>{before}</del>}
      {after}
    </dd>
  );
}

function ChannelRow({
  row,
  disabled,
  rollback,
}: {
  row: Receipt;
  disabled: boolean;
  rollback: (row: Receipt) => void;
}) {
  const t = useT();
  const { current, camera, before, after, image } = row;
  return (
    <details className="ws-batch-channel">
      <summary>
        <strong>
          {channelName(after.name, camera.model, camera.channel_token)}
        </strong>
        <ReceiptStatus row={row} />
        {row.error && (
          <span className="ws-batch-error">
            {cameraError(new HttpFailure(409, row.error)) ?? row.error}
          </span>
        )}
      </summary>
      <div className="ws-channel-detail">
        <dl>
          <dt>{t("batch.cameraName")}</dt>
          <ChangeValue before={before?.name} after={after.name} />
          <dt>{t("batch.point")}</dt>
          <dd>{current?.point_id || after.point_id || t("batch.newPoint")}</dd>
          <dt>{t("batch.recordStream")}</dt>
          <ChangeValue before={before?.main_url} after={after.main_url} />
          <dt>{t("batch.liveStream")}</dt>
          <ChangeValue
            before={before?.secondary_url}
            after={after.secondary_url}
          />
          {(image ||
            row.brightness != null ||
            row.observed_brightness != null) && (
            <>
              <dt>{t("batch.brightnessShort")}</dt>
              <dd>
                {image && (
                  <span>
                    {t("batch.range", {
                      value: image.value,
                      min: image.min,
                      max: image.max,
                    })}
                  </span>
                )}
                {row.brightness != null && (
                  <span>{t("batch.set", { value: row.brightness })}</span>
                )}
                {row.observed_brightness != null && (
                  <span>
                    {t("batch.confirmed", { value: row.observed_brightness })}
                  </span>
                )}
              </dd>
            </>
          )}
          <dt>{t("batch.capabilities")}</dt>
          <dd>
            {[
              camera.ptz && "PTZ",
              camera.events && t("batch.events"),
              image && t("batch.brightnessShort"),
            ]
              .filter(Boolean)
              .join(", ") || t("batch.noFeatures")}
          </dd>
          {camera.manufacturer && (
            <>
              <dt>{t("batch.manufacturer")}</dt>
              <dd>{camera.manufacturer}</dd>
            </>
          )}
          {camera.firmware && (
            <>
              <dt>{t("batch.firmware")}</dt>
              <dd>{camera.firmware}</dd>
            </>
          )}
          {camera.serial && (
            <>
              <dt>{t("batch.serial")}</dt>
              <dd>{camera.serial}</dd>
            </>
          )}
          {camera.endpoint_id !== camera.serial && (
            <>
              <dt>{t("batch.onvifDevice")}</dt>
              <dd>{camera.endpoint_id}</dd>
            </>
          )}
          {camera.channel_token && (
            <>
              <dt>{t("batch.onvifChannel")}</dt>
              <dd>{camera.channel_token}</dd>
            </>
          )}
          {row.template && (
            <>
              <dt>{t("batch.templateRevision")}</dt>
              <dd>
                {row.template.name} · {row.template.revision}
              </dd>
            </>
          )}
          {row.expected_revision != null && (
            <>
              <dt>{t("batch.sourceRevision")}</dt>
              <dd>{row.expected_revision}</dd>
            </>
          )}
          {row.revision != null && (
            <>
              <dt>{t("batch.savedRevision")}</dt>
              <dd>{row.revision}</dd>
            </>
          )}
          {current && (
            <>
              <dt>{t("batch.currentRevision")}</dt>
              <dd>
                {current.desired_revision} ·{" "}
                {
                  {
                    applied: t("batch.state.applied"),
                    applying: t("batch.state.applying"),
                    rejected: t("batch.state.rejected"),
                  }[current.state]
                }
              </dd>
            </>
          )}
          {current?.applied_revision != null && (
            <>
              <dt>{t("batch.recorderRevision")}</dt>
              <dd>{current.applied_revision}</dd>
            </>
          )}
        </dl>
        {row.state !== "rolled_back" && (
          <Button disabled={disabled} onClick={() => rollback(row)}>
            {row.state === "prepared" ? t("batch.cancel") : t("batch.rollback")}
          </Button>
        )}
      </div>
    </details>
  );
}

export function CameraBatchResults({
  rows,
  disabled,
  rollback,
  refresh,
}: {
  rows: Receipt[];
  refresh: ListRefresh;
  disabled: boolean;
  rollback: (row: Receipt) => void;
}) {
  const t = useT();
  const groups = groupChannels(rows, (r) => ({
    device: r.camera,
    key: r.camera.endpoint_id,
    token: r.camera.channel_token,
  }));
  const [query, setQuery] = useState("");
  const [state, setState] = useState("");
  const visible = groups
    .map((g) => ({
      ...g,
      items: g.items.filter(
        (r) =>
          (!state || state === r.state) &&
          matchesQuery(
            query,
            g.title,
            r.camera.serial,
            r.camera.channel_token,
            r.after.name,
            channelName(r.after.name, r.camera.model, r.camera.channel_token),
            r.template?.name,
          ),
      ),
    }))
    .filter((g) => g.items.length);
  return (
    <div className="ws-batch-results ws-batch-receipts">
      <FilterPanel
        label={t("batch.resultFilters")}
        refresh={refresh}
        queryLabel={t("batch.resultQuery")}
        query={query}
        onQuery={setQuery}
        filtered={!!state}
        onReset={() => {
          setQuery("");
          setState("");
        }}
      >
        <label>
          {t("batch.operationState")}
          <select value={state} onChange={(e) => setState(e.target.value)}>
            <option value="">{t("batch.allStates")}</option>
            {Object.entries(labels).map(([v, text]) => (
              <option key={v} value={v}>
                {t(text)}
              </option>
            ))}
          </select>
        </label>
      </FilterPanel>
      {!visible.length && <p role="status">{t("batch.noChannels")}</p>}
      {visible.map(({ key, title, items: group }) => {
        const { template } = group[0]!;
        const sharedTemplate =
          template &&
          group.every(
            (r) =>
              r.template?.id === template.id &&
              r.template.revision === template.revision,
          );
        return (
          <section className="ws-batch-device" key={key}>
            <header>
              <h3>{title}</h3>
              {sharedTemplate && (
                <span>{t("batch.templateNamed", { name: template.name })}</span>
              )}
            </header>
            {group.map((row) => (
              <ChannelRow
                row={row}
                key={row.item_id}
                disabled={disabled}
                rollback={rollback}
              />
            ))}
          </section>
        );
      })}
    </div>
  );
}
