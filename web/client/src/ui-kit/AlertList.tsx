import { FilterPanel, FilterSegments, matchesQuery } from "./FilterPanel";
import "./media-panels.css";
import { useState, useMemo, type Ref } from "react";
import type { AlertItem, Feed, Point } from "../contracts/workspace";
import { alertName, alertStates } from "./alertLabels";
import { formatUtc, milliseconds } from "./utc";
export { alertName } from "./alertLabels";

export function AlertList({
  feed,
  points,
  selected,
  onOpen,
  listRef,
  filter,
  onFilter,
  search,
}: {
  feed: Feed;
  points: Point[];
  selected: string | null;
  onOpen: (item: AlertItem) => void;
  listRef: Ref<HTMLUListElement>;
  filter: string;
  onFilter: (value: string) => void;
  search?: { query: string; change: (value: string) => void };
}) {
  const [localQuery, setLocalQuery] = useState("");
  const query = search?.query ?? localQuery,
    setQuery = search?.change ?? setLocalQuery;
  const names = useMemo(
    () => new Map(points.map((p) => [p.id, p.name])),
    [points],
  );
  const visible = useMemo(
    () =>
      feed.items
        .filter(
          (item) =>
            (!filter || item.work_state === filter) &&
            matchesQuery(
              query,
              alertName(item.type),
              names.get(item.point_id),
              item.id,
            ),
        )
        .sort(
          (a, b) => milliseconds(b.observed_at) - milliseconds(a.observed_at),
        ),
    [feed.items, filter, names, query],
  );
  return (
    <aside className="ws-panel ws-alerts" aria-labelledby="feed-heading">
      <header className="ws-panel-head">
        <h2 id="feed-heading" tabIndex={-1}>
          Тревоги
        </h2>
        <span
          className="ws-feed-count"
          aria-label={`Показано ${visible.length} из ${feed.items.length}`}
        >
          {filter || query ? visible.length + " / " : ""}
          {feed.items.length}
        </span>
      </header>
      <div className="ws-feed-tools">
        <p role="status" className="ws-feed-status" data-status={feed.status}>
          {feed.status === "loading"
            ? "Загрузка тревог…"
            : feed.status === "denied"
              ? "Доступ к ленте закрыт"
              : feed.status === "reconnecting"
                ? "Восстановление связи"
                : feed.items.length
                  ? "Лента подключена"
                  : "Тревог пока нет"}
        </p>
        <FilterPanel
          label="Фильтры ленты тревог"
          queryLabel="Тревога или точка"
          query={query}
          onQuery={setQuery}
          filtered={!!filter}
          onReset={() => {
            setQuery("");
            onFilter("");
          }}
        >
          <FilterSegments
            label="Состояние тревог в ленте"
            value={filter}
            onChange={onFilter}
            options={[
              { value: "new", label: "Новые" },
              { value: "in_progress", label: "В работе" },
              { value: "closed", label: "Закрытые" },
            ]}
          />
        </FilterPanel>
      </div>
      {feed.status === "online" && !!(filter || query) && !visible.length && (
        <p className="ws-feed-empty" role="status">
          {query
            ? "Тревоги по заданным условиям не найдены."
            : "В этом состоянии тревог нет."}
        </p>
      )}
      <ul ref={listRef}>
        {visible.map((item) => (
          <li key={item.id}>
            <button
              type="button"
              id={"alert-" + item.id}
              aria-current={selected === item.id || undefined}
              onClick={() => onOpen(item)}
            >
              <strong>{alertName(item.type)}</strong>
              <small className="ws-alert-state" data-state={item.work_state}>
                {item.work_state
                  ? alertStates[item.work_state]
                  : "Состояние не указано"}
              </small>
              <span>{names.get(item.point_id) ?? "Точка наблюдения"}</span>
              <time dateTime={item.observed_at}>
                {formatUtc(item.observed_at)}
              </time>
            </button>
          </li>
        ))}
      </ul>
      {!!visible.length && <p className="ws-feed-note">Приоритет не задан</p>}
    </aside>
  );
}
