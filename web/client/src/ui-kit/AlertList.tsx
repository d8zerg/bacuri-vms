import "./media-panels.css";
import type { Ref } from "react";
import type { AlertItem, Feed, Point } from "../contracts/workspace";
import { alertName, alertStates } from "./alertLabels";
export { alertName } from "./alertLabels";
export function AlertList({
  feed,
  points,
  selected,
  onOpen,
  listRef,
}: {
  feed: Feed;
  points: Point[];
  selected: string | null;
  onOpen: (item: AlertItem) => void;
  listRef: Ref<HTMLUListElement>;
}) {
  return (
    <aside className="ws-panel ws-alerts" aria-label="Тревоги">
      <header className="ws-panel-head">
        <h2>Тревоги</h2>
        <span>{feed.items.length}</span>
      </header>
      <p role="status" className="ws-feed-status">
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
      <ul ref={listRef}>
        {feed.items.map((item) => (
          <li key={item.id}>
            <button
              type="button"
              id={"alert-" + item.id}
              aria-current={selected === item.id || undefined}
              onClick={() => onOpen(item)}
            >
              <strong>{alertName(item.type)}</strong>
              <span>
                {points.find((p) => p.id === item.point_id)?.name ??
                  "Точка наблюдения"}
              </span>
              <small>
                {item.work_state
                  ? alertStates[item.work_state]
                  : "Состояние не указано"}{" "}
                · Приоритет не задан
              </small>
              <time dateTime={item.observed_at}>
                {item.observed_at.replace("T", " ").replace("Z", " UTC")}
              </time>
            </button>
          </li>
        ))}
      </ul>
    </aside>
  );
}
