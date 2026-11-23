import {
  FilterPanel,
  matchesQuery,
  needsListSearch,
} from "../ui-kit/FilterPanel";
import { useEffect, useState } from "react";
import type { FormEvent } from "react";
import { useSearchParams } from "react-router";
import { RouteButton } from "./RouteButton";
import type {
  AlertCommand,
  AlertWork,
  AuditEntry,
  AuditFilter,
  Point,
  PtzCommand,
  Services,
} from "../contracts/workspace";
import { HttpFailure } from "../adapters/api";
import { formatUtc } from "../ui-kit/utc";
import { Button } from "../ui-kit/FormControls";
import "./alert-work.css";
import { ptzLabels } from "./cameraCommands";

import { alertStates as states } from "../ui-kit/alertLabels";
const actions = {
  claim: "Взять в работу",
  record_result: "Записать результат",
  close: "Закрыть тревогу",
} as const;
const events = {
  claim: "Принятие в работу",
  record_result: "Результат проверки",
  close: "Закрытие тревоги",
  delegate: "Выдача поручения",
  revoke: "Отзыв поручения",
  export: "Экспорт фрагмента",
  camera_control: "Управление камерой",
};
const outcomes = {
  applied: "Выполнено",
  dispatched: "Отправка команды",
  unknown: "Исход неизвестен",
  rejected: "Команда отклонена",
  denied: "Отказ в доступе",
  conflict: "Конфликт",
};
const reasons: Record<string, string> = {
  forbidden: "Действие запрещено",
  media_unavailable: "Запись недоступна",
  rate_limited: "Достигнут предел одновременных запросов",
  export_too_large: "Превышен предел размера экспорта",
  not_found: "Точка или тревога недоступна",
  alert_state_conflict: "Состояние или версия тревоги изменились",
  alert_result_required: "Требуется результат проверки",
  idempotency_conflict: "Ключ команды уже использован с другими параметрами",
};
type Context = { services: Services; site: string };

function Entries({
  items,
  site,
  points,
}: {
  items: AuditEntry[];
  site: string;
  points?: Point[];
}) {
  const [params] = useSearchParams();
  function monitorUrl(alert: string, point: string) {
    const next = new URLSearchParams(params);
    if (next.get("alert") !== alert) {
      for (const name of ["at", "from", "to", "scroll", "focus"])
        next.delete(name);
    }
    next.delete("alert_id");
    next.set("site", site);
    next.set("point", point);
    next.set("alert", alert);
    next.set("mode", "archive");
    return "/monitor?" + next.toString();
  }
  return (
    <ol className="ws-history">
      {items.map((e) => {
        const pointName = points?.find((p) => p.id === e.point_id)?.name;
        const actor =
          e.actor_kind === "service"
            ? "Сервис " + e.service + " по поручению " + e.initiator
            : e.initiator;
        const [command = "", ...target] = e.reason.split(" / ");
        const result =
          e.action === "camera_control"
            ? [
                ptzLabels[command as PtzCommand] ?? reasons[command] ?? command,
                ...target,
              ].join(" / ")
            : e.outcome !== "applied"
              ? (reasons[e.reason] ?? e.reason)
              : e.action === "export" ||
                  e.action === "delegate" ||
                  e.action === "revoke"
                ? ""
                : e.reason;
        return (
          <li key={e.id}>
            <details>
              <summary>
                <strong>
                  {events[e.action]}
                  {e.outcome !== "applied" &&
                    " · " +
                      (e.action === "export" && e.outcome === "denied"
                        ? "В выдаче отказано"
                        : outcomes[e.outcome])}
                </strong>
                <span className="ws-history-meta">
                  <time dateTime={e.recorded_at}>
                    {formatUtc(e.recorded_at)}
                  </time>{" "}
                  · {actor}
                  {pointName && " · " + pointName}
                </span>
                {result && <span className="ws-history-result">{result}</span>}
              </summary>
              <dl>
                <dt>Результат действия</dt>
                <dd>
                  {e.action === "camera_control" && e.outcome === "applied"
                    ? "Камера подтвердила команду"
                    : outcomes[e.outcome]}
                </dd>
                {e.from_state && e.to_state && e.from_state !== e.to_state && (
                  <>
                    <dt>Состояние</dt>
                    <dd>
                      {states[e.from_state]} {"->"} {states[e.to_state]}
                    </dd>
                  </>
                )}
                {e.owner && (
                  <>
                    <dt>Ответственный</dt>
                    <dd>{e.owner}</dd>
                  </>
                )}
                {e.delegated_action && e.expires_at && (
                  <>
                    <dt>Поручение</dt>
                    <dd>
                      {e.service} · {actions[e.delegated_action]} · до{" "}
                      {formatUtc(e.expires_at)}
                    </dd>
                  </>
                )}
                {e.point_id && (
                  <>
                    <dt>Точка</dt>
                    <dd>{e.point_id}</dd>
                  </>
                )}
                {e.alert_id && (
                  <>
                    <dt>Тревога</dt>
                    <dd>{e.alert_id}</dd>
                  </>
                )}
                {e.export && (
                  <>
                    <dt>Комплект</dt>
                    <dd>{e.export.id}</dd>
                    <dt>Интервал</dt>
                    <dd>
                      {formatUtc(e.export.interval.start)} -{" "}
                      {formatUtc(e.export.interval.end)}
                    </dd>
                  </>
                )}
              </dl>
            </details>
            {points && e.alert_id && e.point_id && (
              <RouteButton to={monitorUrl(e.alert_id, e.point_id)}>
                Открыть тревогу
              </RouteButton>
            )}
          </li>
        );
      })}
    </ol>
  );
}

function AlertHistory({ work, site }: { work: AlertWork; site: string }) {
  const [query, setQuery] = useState("");
  const rows = work.history.filter((e) =>
    matchesQuery(
      query,
      events[e.action],
      outcomes[e.outcome],
      e.initiator,
      e.owner,
      e.service,
      reasons[e.reason] ?? e.reason,
    ),
  );
  return (
    <>
      {needsListSearch(work.history.length, query) && (
        <FilterPanel
          label="Фильтры истории тревоги"
          queryLabel="Действие, исполнитель или результат"
          query={query}
          onQuery={setQuery}
          onReset={() => setQuery("")}
        />
      )}
      {rows.length ? (
        <Entries items={rows} site={site} />
      ) : (
        <p role="status">
          {query ? "Действия не найдены." : "Действий пока нет"}
        </p>
      )}
    </>
  );
}

function Delegations({
  services,
  site,
  work,
  refresh,
}: { work: AlertWork; refresh: () => void } & Context) {
  const [query, setQuery] = useState("");
  const shown = work.delegations.filter((d) =>
    matchesQuery(query, d.service, actions[d.action]),
  );
  const [secret, setSecret] = useState(""),
    [notice, setNotice] = useState(""),
    [busy, setBusy] = useState(false);
  async function issue(e: FormEvent<HTMLFormElement>) {
    e.preventDefault();
    if (busy) return;
    const fields = new FormData(e.currentTarget),
      service = fields.get("service"),
      action = fields.get("action");
    if (
      typeof service !== "string" ||
      !(action === "claim" || action === "record_result" || action === "close")
    )
      return;
    setBusy(true);
    setSecret("");
    setNotice("");
    try {
      const grant = await services.delegate(site, {
        point_id: work.point_id,
        service,
        action,
        ttl_seconds: Number(fields.get("minutes")) * 60,
      });
      setSecret(grant.token);
      setNotice("Поручение выдано");
      refresh();
    } catch {
      setNotice("Не удалось выдать поручение");
    } finally {
      setBusy(false);
    }
  }
  async function revoke(id: string) {
    setBusy(true);
    setSecret("");
    try {
      await services.revokeDelegation(site, id);
      setNotice("Поручение отозвано");
      refresh();
    } catch {
      setNotice("Не удалось отозвать поручение");
    } finally {
      setBusy(false);
    }
  }
  return (
    <details
      onToggle={(e) => {
        if (!e.currentTarget.open) setSecret("");
      }}
    >
      <summary>Поручения сервисам на этой точке</summary>
      <form onSubmit={(e) => void issue(e)}>
        <label>
          Имя сервиса
          <input name="service" required maxLength={128} />
        </label>
        <label>
          Разрешённое действие
          <select name="action" defaultValue="record_result">
            {Object.entries(actions).map(([id, label]) => (
              <option key={id} value={id}>
                {label}
              </option>
            ))}
          </select>
        </label>
        <label>
          Срок в минутах
          <input
            name="minutes"
            type="number"
            min={1}
            max={1440}
            required
            defaultValue={15}
          />
        </label>
        <Button type="submit" disabled={busy}>
          Выдать поручение
        </Button>
      </form>
      {secret && (
        <label>
          Токен для передачи сервису
          <input readOnly value={secret} autoComplete="off" />
        </label>
      )}
      {needsListSearch(work.delegations.length, query) && (
        <FilterPanel
          label="Фильтры поручений"
          queryLabel="Сервис или действие"
          query={query}
          onQuery={setQuery}
          onReset={() => setQuery("")}
        />
      )}
      {query && !shown.length && <p role="status">Поручения не найдены.</p>}
      {shown.map((d) => (
        <div key={d.id}>
          {d.service} · {actions[d.action]} · до {formatUtc(d.expires_at)}{" "}
          <Button disabled={busy} onClick={() => void revoke(d.id)}>
            Отозвать
          </Button>
        </div>
      ))}
      <span role="status">{notice}</span>
    </details>
  );
}

export function AlertWorkPanel({
  services,
  site,
  alert,
  point,
}: Context & { alert: string; point: string }) {
  const [params] = useSearchParams();
  const auditParams = new URLSearchParams(params);
  auditParams.set("site", site);
  auditParams.set("alert_id", alert);
  const [work, setWork] = useState<AlertWork | null>(null),
    [failed, fail] = useState(false),
    [refresh, update] = useState(0);
  const [note, setNote] = useState(""),
    [notice, setNotice] = useState(""),
    [busy, setBusy] = useState(false);
  const [retry, setRetry] = useState<{
    body: AlertCommand;
    key: string;
  } | null>(null);
  useEffect(() => {
    const ctl = new AbortController();
    let running = false;
    async function read() {
      if (running) return;
      running = true;
      try {
        const next = await services.alertWork(
          site,
          alert,
          AbortSignal.any([ctl.signal, AbortSignal.timeout(3500)]),
        );
        if (!ctl.signal.aborted) {
          if (next.point_id !== point) throw Error();
          setWork(next);
          fail(false);
        }
      } catch {
        if (!ctl.signal.aborted) {
          setWork(null);
          fail(true);
        }
      } finally {
        running = false;
      }
    }
    void read();
    const interval = setInterval(() => void read(), 2000);
    return () => {
      ctl.abort();
      clearInterval(interval);
    };
  }, [services, site, alert, point, refresh]);
  async function send(body: AlertCommand, key: string) {
    if (busy) return;
    setBusy(true);
    setNotice("");
    try {
      await services.alertCommand(site, alert, body, key);
      setRetry(null);
      setNote("");
      setNotice("Действие сохранено");
    } catch (e) {
      if (e instanceof HttpFailure && e.status < 500) {
        setRetry(null);
        setNotice(
          e.status === 409
            ? "Состояние тревоги изменилось. Проверьте карточку."
            : "Действие недоступно",
        );
      } else {
        setRetry({ body, key });
        setNotice("Ответ не получен. Повторите отправку.");
      }
    } finally {
      setBusy(false);
      update((n) => n + 1);
    }
  }
  return (
    <section
      className="ws-panel ws-pad ws-alert-work"
      aria-labelledby="alert-work-heading"
    >
      <h2 id="alert-work-heading">Обработка тревоги</h2>
      {work ? (
        <>
          <p>
            <strong>{states[work.state]}</strong>
            {work.owner && " · Ответственный: " + work.owner}
          </p>
          {work.result && <p>Результат проверки · {work.result}</p>}
          {work.actions.includes("record_result") && (
            <label>
              Результат или основание закрытия
              <textarea
                maxLength={1024}
                value={note}
                onChange={(e) => setNote(e.target.value)}
              />
            </label>
          )}
          <div className="ws-work-actions">
            {work.actions.map((action) => (
              <Button
                key={action}
                variant={action === "close" ? "secondary" : "primary"}
                disabled={
                  busy || !!retry || (action !== "claim" && !note.trim())
                }
                onClick={() =>
                  void send(
                    {
                      action,
                      version: work.version,
                      reason:
                        action === "claim" ? "Принято дежурным" : note.trim(),
                    },
                    crypto.randomUUID(),
                  )
                }
              >
                {actions[action]}
              </Button>
            ))}
            {retry && (
              <Button
                disabled={busy}
                onClick={() => void send(retry.body, retry.key)}
              >
                Повторить отправку
              </Button>
            )}
          </div>
          <h3>История тревоги</h3>
          <AlertHistory key={alert} work={work} site={site} />
          <RouteButton to={"/audit?" + auditParams.toString()}>
            Открыть журнал этой тревоги
          </RouteButton>
          <Delegations
            services={services}
            site={site}
            work={work}
            refresh={() => update((n) => n + 1)}
          />
        </>
      ) : (
        <p role="status">
          {failed ? "Карточка недоступна" : "Загрузка карточки…"}
        </p>
      )}
      <p role="status">{notice}</p>
    </section>
  );
}

export function AlertAudit({
  services,
  site,
  points,
}: Context & { points: Point[] }) {
  const [params] = useSearchParams(),
    initial = params.get("alert_id") ?? "";
  const [search, setSearch] = useState<AuditFilter>(() =>
    initial ? { alert_id: initial } : {},
  );
  const [previous, setPrevious] = useState<AuditFilter[]>([]);
  const [page, setPage] = useState<{
      items: AuditEntry[];
      next_cursor?: string;
    } | null>(null),
    [error, setError] = useState(false);

  useEffect(() => {
    const ctl = new AbortController();
    setPage(null);
    setError(false);
    let running = false;
    async function read() {
      if (running) return;
      running = true;
      try {
        const data = await services.audit(
          site,
          search,
          AbortSignal.any([ctl.signal, AbortSignal.timeout(3500)]),
        );
        if (!ctl.signal.aborted) {
          setPage(data);
          setError(false);
        }
      } catch {
        if (!ctl.signal.aborted) {
          setPage(null);
          setError(true);
        }
      } finally {
        running = false;
      }
    }
    void read();
    const interval = setInterval(() => void read(), 2000);
    return () => {
      ctl.abort();
      clearInterval(interval);
    };
  }, [search, site, services]);
  const [query, setQuery] = useState("");
  const [alertId, setAlertId] = useState(initial);
  function apply(query: string, id: string) {
    setPrevious([]);
    setSearch({
      ...(query.trim() ? { query: query.trim() } : {}),
      ...(id.trim() ? { alert_id: id.trim() } : {}),
    });
  }
  return (
    <>
      <section
        className="ws-panel ws-pad ws-alert-work ws-audit"
        aria-label="Записи журнала"
      >
        <FilterPanel
          label="Поиск и фильтры журнала"
          queryLabel="Результат или основание"
          query={query}
          onQuery={setQuery}
          maxLength={256}
          filtered={!!(alertId || search.query || search.alert_id)}
          onApply={() => apply(query, alertId)}
          onReset={() => {
            setQuery("");
            setAlertId("");
            apply("", "");
          }}
        >
          <label>
            Идентификатор тревоги
            <input
              name="alert"
              value={alertId}
              onChange={(e) => setAlertId(e.target.value)}
              maxLength={36}
            />
          </label>
        </FilterPanel>
        <div role="region" aria-label="Результаты поиска">
          {page ? (
            page.items.length ? (
              <Entries items={page.items} site={site} points={points} />
            ) : (
              <p role="status">
                {search.alert_id || search.query || search.before
                  ? "Записи не найдены"
                  : "Действий пока нет. Когда оператор возьмёт тревогу в работу, здесь появится запись."}
              </p>
            )
          ) : (
            <p role="status">
              {error
                ? "Не удалось получить записи журнала"
                : "Загрузка журнала…"}
            </p>
          )}
          {(previous.length > 0 || page?.next_cursor) && (
            <div
              className="ws-work-actions"
              role="group"
              aria-label="Страницы журнала"
            >
              {previous.length > 0 && (
                <Button
                  onClick={() => {
                    setSearch(previous[previous.length - 1]!);
                    setPrevious(previous.slice(0, -1));
                  }}
                >
                  Предыдущая страница
                </Button>
              )}
              {page?.next_cursor && (
                <Button
                  onClick={() => {
                    setPrevious([...previous, search]);
                    setSearch({ ...search, before: page.next_cursor! });
                  }}
                >
                  Следующая страница
                </Button>
              )}
            </div>
          )}
        </div>
      </section>
    </>
  );
}
