import { useState } from "react";
import {
  FilterPanel,
  matchesQuery,
  needsListSearch,
} from "../ui-kit/FilterPanel";
import type { Camera, CameraInspection, Point } from "../contracts/workspace";
import { channelName, groupChannels } from "./cameraChannels";

export type Channel = {
  id: string;
  info: CameraInspection;
  channel: string;
  name: string;
  point: string;
  username: string;
  password: string;
  selected: boolean;
  bound: boolean;
};

export function BatchChannels({
  channels,
  points,
  inventory,
  edit,
}: {
  channels: Channel[];
  points: Point[];
  inventory: Camera[];
  edit: (id: string, patch: Partial<Channel>) => void;
}) {
  const groups = groupChannels(channels, (ch) => ({
    device: ch.info,
    key: ch.info.endpoint_id,
    token: ch.channel,
  }));
  const [query, setQuery] = useState("");
  const names = new Map(points.map((p) => [p.id, p.name]));
  const visible = groups
    .map((g) => ({
      ...g,
      items: g.items.filter((ch) =>
        matchesQuery(
          query,
          g.title,
          ch.info.serial,
          ch.name,
          ch.channel,
          channelName(ch.name, ch.info.model, ch.channel),
          names.get(ch.point),
        ),
      ),
    }))
    .filter((g) => g.items.length);
  const used = new Set(inventory.map((c) => c.point_id));
  const free = points.filter((p) => !used.has(p.id));
  return (
    <div className="ws-batch-results">
      {needsListSearch(channels.length, query) && (
        <FilterPanel
          label="Фильтры каналов подключения"
          queryLabel="Устройство, канал или точка"
          query={query}
          onQuery={setQuery}
          onReset={() => setQuery("")}
        />
      )}
      {query && (
        <p role="status">
          {visible.length ? "" : "Каналы не найдены. "}Выбрано каналов в партии:{" "}
          {channels.filter((ch) => ch.selected).length}
        </p>
      )}
      {visible.map(({ key, title, items }) => (
        <section className="ws-batch-device" key={key}>
          <header>
            <h3>{title}</h3>
          </header>
          {items.map((ch) => {
            const name =
              channelName(ch.name, ch.info.model, ch.channel) ||
              ch.channel ||
              "Канал";
            const point =
              points.find((p) => p.id === ch.point)?.name ||
              (ch.bound
                ? inventory.find((c) => c.point_id === ch.point)?.name
                : "") ||
              "Выбранная точка";
            return (
              <div className="ws-draft-row" key={ch.id}>
                <label className="ws-draft-select">
                  <input
                    type="checkbox"
                    aria-label={"Включить " + name}
                    checked={ch.selected}
                    onChange={(e) =>
                      edit(ch.id, { selected: e.target.checked })
                    }
                  />
                </label>
                <details className="ws-batch-channel">
                  <summary>
                    <strong>{name}</strong>
                    <span>{ch.point ? point : "Новая точка"}</span>
                  </summary>
                  <div className="ws-draft-fields">
                    <label className="bk-field">
                      Имя камеры
                      <input
                        value={ch.name}
                        maxLength={256}
                        onChange={(e) => edit(ch.id, { name: e.target.value })}
                      />
                    </label>
                    <label className="bk-field">
                      Точка
                      <select
                        disabled={ch.bound}
                        value={ch.point}
                        onChange={(e) => edit(ch.id, { point: e.target.value })}
                      >
                        <option value="">Создать точку</option>
                        {ch.point && !free.some((p) => p.id === ch.point) && (
                          <option value={ch.point}>{point}</option>
                        )}
                        {free.map((p) => (
                          <option key={p.id} value={p.id}>
                            {p.name}
                          </option>
                        ))}
                      </select>
                    </label>
                  </div>
                </details>
              </div>
            );
          })}
        </section>
      ))}
    </div>
  );
}
