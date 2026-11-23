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

type Receipt = components["schemas"]["CameraBatchReceipt"];

const labels = {
  prepared: "Готов к применению",
  applying: "Применяется",
  failed: "Не применено",
  saved: "Конфигурация сохранена",
  rolling_back: "Выполняется откат",
  rolled_back: "Откат выполнен",
  rollback_failed: "Ошибка отката",
};

function ReceiptStatus({ row }: { row: Receipt }) {
  const { current, revision, state } = row;
  let text = labels[state];
  let tone: "neutral" | "success" | "danger" =
    state === "failed" || state === "rollback_failed" ? "danger" : "neutral";
  if (state === "saved" && current) {
    if (current.desired_revision !== revision) text = "Есть более новая версия";
    else if (current.state === "rejected") {
      text = "Рекордер отклонил";
      tone = "danger";
    } else if (
      current.state === "applied" &&
      current.applied_revision === revision
    ) {
      text = "Применено рекордером";
      tone = "success";
    } else text = "Ожидает рекордера";
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
          <dt>Имя камеры</dt>
          <ChangeValue before={before?.name} after={after.name} />
          <dt>Точка</dt>
          <dd>{current?.point_id || after.point_id || "Новая точка"}</dd>
          <dt>Поток записи</dt>
          <ChangeValue before={before?.main_url} after={after.main_url} />
          <dt>Поток эфира</dt>
          <ChangeValue
            before={before?.secondary_url}
            after={after.secondary_url}
          />
          {(image ||
            row.brightness != null ||
            row.observed_brightness != null) && (
            <>
              <dt>Яркость</dt>
              <dd>
                {image && (
                  <span>
                    {image.value} (диапазон {image.min} - {image.max})
                  </span>
                )}
                {row.brightness != null && <span>Задано {row.brightness}</span>}
                {row.observed_brightness != null && (
                  <span>Подтверждено {row.observed_brightness}</span>
                )}
              </dd>
            </>
          )}
          <dt>Возможности</dt>
          <dd>
            {[
              camera.ptz && "PTZ",
              camera.events && "События",
              image && "Яркость",
            ]
              .filter(Boolean)
              .join(", ") || "Нет дополнительных функций"}
          </dd>
          {camera.manufacturer && (
            <>
              <dt>Производитель</dt>
              <dd>{camera.manufacturer}</dd>
            </>
          )}
          {camera.firmware && (
            <>
              <dt>Прошивка</dt>
              <dd>{camera.firmware}</dd>
            </>
          )}
          {camera.serial && (
            <>
              <dt>Серийный номер</dt>
              <dd>{camera.serial}</dd>
            </>
          )}
          {camera.endpoint_id !== camera.serial && (
            <>
              <dt>Устройство ONVIF</dt>
              <dd>{camera.endpoint_id}</dd>
            </>
          )}
          {camera.channel_token && (
            <>
              <dt>Канал ONVIF</dt>
              <dd>{camera.channel_token}</dd>
            </>
          )}
          {row.template && (
            <>
              <dt>Версия шаблона</dt>
              <dd>
                {row.template.name} · {row.template.revision}
              </dd>
            </>
          )}
          {row.expected_revision != null && (
            <>
              <dt>Исходная версия</dt>
              <dd>{row.expected_revision}</dd>
            </>
          )}
          {row.revision != null && (
            <>
              <dt>Сохранённая версия</dt>
              <dd>{row.revision}</dd>
            </>
          )}
          {current && (
            <>
              <dt>Текущая версия</dt>
              <dd>
                {current.desired_revision} ·{" "}
                {
                  {
                    applied: "применена",
                    applying: "ожидает рекордера",
                    rejected: "отклонена",
                  }[current.state]
                }
              </dd>
            </>
          )}
          {current?.applied_revision != null && (
            <>
              <dt>Версия рекордера</dt>
              <dd>{current.applied_revision}</dd>
            </>
          )}
        </dl>
        {row.state !== "rolled_back" && (
          <Button disabled={disabled} onClick={() => rollback(row)}>
            {row.state === "prepared" ? "Отменить изменение" : "Откатить канал"}
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
        label="Фильтры результатов подключения"
        refresh={refresh}
        queryLabel="Устройство или канал"
        query={query}
        onQuery={setQuery}
        filtered={!!state}
        onReset={() => {
          setQuery("");
          setState("");
        }}
      >
        <label>
          Состояние операции
          <select value={state} onChange={(e) => setState(e.target.value)}>
            <option value="">Все состояния</option>
            {Object.entries(labels).map(([v, text]) => (
              <option key={v} value={v}>
                {text}
              </option>
            ))}
          </select>
        </label>
      </FilterPanel>
      {!visible.length && <p role="status">Каналы не найдены.</p>}
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
              {sharedTemplate && <span>Шаблон «{template.name}»</span>}
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
