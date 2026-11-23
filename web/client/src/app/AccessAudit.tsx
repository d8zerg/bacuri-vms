import {
  FilterPanel,
  matchesQuery,
  type ListRefresh,
} from "../ui-kit/FilterPanel";
import type { components } from "../../../../build/contracts/api";
import { formatUtc } from "../ui-kit/utc";
type Event = components["schemas"]["AccessAuditEvent"];
const operations: Record<string, readonly [string, string, string]> = {
  site_roles: [
    "Назначена роль на площадке",
    "Изменена учётная запись на площадке",
    "Удалена роль на площадке",
  ],
  point_grants: [
    "Открыт доступ к точке",
    "Изменён доступ к точке",
    "Отозван доступ к точке",
  ],
  export_grants: [
    "Разрешён экспорт записи",
    "Изменено разрешение на экспорт",
    "Отозвано разрешение на экспорт",
  ],
  scoped_grants: [
    "Выдано разрешение",
    "Изменено разрешение",
    "Отозвано разрешение",
  ],
  resource_groups: ["Создана группа", "Изменена группа", "Удалена группа"],
  group_points: [
    "Точка добавлена в группу",
    "Изменена привязка точки к группе",
    "Точка удалена из группы",
  ],
  privacy_policies: [
    "Добавлена политика приватности",
    "Изменена политика приватности",
    "Удалена политика приватности",
  ],
  identity_providers: [
    "Добавлен провайдер входа",
    "Изменён провайдер входа",
    "Удалён провайдер входа",
  ],
  external_identities: [
    "Привязана внешняя учётная запись",
    "Изменена внешняя учётная запись",
    "Отвязана внешняя учётная запись",
  ],
};
function describe(code: string) {
  if (code === "session.login") return "Вход в систему";
  if (code === "session.logout") return "Выход из системы";
  const [table, verb] = code.split(".");
  const n = ["insert", "update", "delete"].indexOf(verb ?? "");
  return operations[table ?? ""]?.[n] ?? "Событие управления доступом";
}

export function AccessAudit({
  refresh,
  query,
  onQuery,
  method,
  onMethod,
  events,
  users,
  groups,
  points,
}: {
  refresh: ListRefresh;
  query: string;
  onQuery: (query: string) => void;
  method: string;
  onMethod: (method: string) => void;
  events: Event[];
  users: { id: string; username: string }[];
  groups: { id: string; name: string }[];
  points: { id: string; name: string }[];
}) {
  const names = new Map([
    ...users.map((u) => [u.id, u.username] as const),
    ...[...groups, ...points].map((r) => [r.id, r.name] as const),
  ]);
  const rows = events
    .map((e) => {
      const actor =
        users.find((u) => u.id === e.actor)?.username ??
        (e.actor === "local-console"
          ? "Локальная консоль"
          : "Неизвестный исполнитель");
      const target = e.resource
        .split("/")
        .filter((id) => !(e.action.startsWith("session.") && id === e.actor))
        .map((id) => names.get(id))
        .filter(Boolean)
        .join(" · ");
      return { e, actor, target };
    })
    .filter(
      ({ e, actor, target }) =>
        (!method || e.method === method) &&
        matchesQuery(
          query,
          actor,
          target,
          e.actor,
          e.resource,
          describe(e.action),
          e.action,
        ),
    );
  return (
    <>
      <FilterPanel
        refresh={refresh}
        label="Фильтры аудита доступа"
        queryLabel={
          events.length === 200
            ? "Поиск в последних 200 записях"
            : "Действие, исполнитель или ресурс"
        }
        query={query}
        onQuery={onQuery}
        filtered={!!method}
        onReset={() => {
          onQuery("");
          onMethod("");
        }}
      >
        <label>
          Способ доступа
          <select value={method} onChange={(e) => onMethod(e.target.value)}>
            <option value="">Все способы</option>
            {[...new Set(events.map((e) => e.method))].map((m) => (
              <option key={m}>{m}</option>
            ))}
          </select>
        </label>
      </FilterPanel>
      {!rows.length && (
        <p role="status">
          {events.length ? "Записи не найдены." : "Записей пока нет."}
        </p>
      )}
      <ol className="ws-access-audit">
        {rows.map(({ e, actor, target }) => {
          return (
            <li key={e.id}>
              <details>
                <summary>
                  <strong>
                    {describe(e.action)}
                    {target && " · " + target}
                  </strong>
                  <span>
                    {actor} ·{" "}
                    <time dateTime={e.recorded_at}>
                      {formatUtc(e.recorded_at)}
                    </time>
                  </span>
                </summary>
                <dl>
                  <dt>Код действия</dt>
                  <dd>{e.action}</dd>
                  <dt>Ресурс</dt>
                  <dd>{e.resource}</dd>
                  <dt>Исполнитель</dt>
                  <dd>{e.actor}</dd>
                  <dt>Способ доступа</dt>
                  <dd>{e.method}</dd>
                </dl>
              </details>
            </li>
          );
        })}
      </ol>
    </>
  );
}
