import { useEffect, useState } from "react";
import type { FormEvent } from "react";
import { Link, useSearchParams } from "react-router";
import type {
  AlertCommand,
  AlertWork,
  AuditEntry,
  AuditFilter,
  Point,
  Services,
} from "../contracts/workspace";
import { HttpFailure } from "../adapters/api";
import { Button } from "../ui-kit/FormControls";
import "./alert-work.css";

const states = {
  new: "Новая",
  in_progress: "В работе",
  closed: "Закрыта",
} as const;
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
};
const outcomes = {
  applied: "Выполнено",
  denied: "Отказ в доступе",
  conflict: "Конфликт",
};
const reasons: Record<string, string> = {
  forbidden: "Действие запрещено",
  not_found: "Точка или тревога недоступна",
  alert_state_conflict: "Состояние или версия тревоги изменились",
  alert_result_required: "Требуется результат проверки",
  idempotency_conflict: "Ключ команды уже использован с другими параметрами",
};
const date = new Intl.DateTimeFormat("ru-RU", {
  dateStyle: "short",
  timeStyle: "medium",
  timeZone: "UTC",
});
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
      {items.map((e) => (
        <li key={e.id}>
          <strong>
            {events[e.action]} · {outcomes[e.outcome]}
          </strong>
          <time dateTime={e.recorded_at}>
            {date.format(new Date(e.recorded_at))} UTC
          </time>
          <span>
            Исполнитель ·{" "}
            {e.actor_kind === "service"
              ? "Сервис " + e.service + " по поручению " + e.initiator
              : e.initiator}
          </span>
          {e.from_state && e.to_state && (
            <span>
              {states[e.from_state]} {"->"} {states[e.to_state]}
            </span>
          )}
          {e.owner && <span>Ответственный · {e.owner}</span>}
          {e.delegated_action && e.expires_at && (
            <span>
              Поручение · {e.service} · {actions[e.delegated_action]} · до{" "}
              {date.format(new Date(e.expires_at))} UTC
            </span>
          )}
          {points && e.point_id && (
            <span>
              Точка ·{" "}
              {points.find((p) => p.id === e.point_id)?.name ?? e.point_id}
            </span>
          )}
          {points && e.alert_id && <span>Тревога · {e.alert_id}</span>}
          {e.outcome !== "applied" ? (
            <span>{reasons[e.reason] ?? e.reason}</span>
          ) : (
            e.action !== "revoke" &&
            e.action !== "delegate" && <span>{e.reason}</span>
          )}
          {points && e.alert_id && e.point_id && (
            <Link to={monitorUrl(e.alert_id, e.point_id)}>Открыть тревогу</Link>
          )}
        </li>
      ))}
    </ol>
  );
}

function Delegations({
  services,
  site,
  work,
  refresh,
}: { work: AlertWork; refresh: () => void } & Context) {
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
      {work.delegations.map((d) => (
        <div key={d.id}>
          {d.service} · {actions[d.action]} · до{" "}
          {date.format(new Date(d.expires_at))} UTC{" "}
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
          {work.history.length ? (
            <Entries items={work.history} site={site} />
          ) : (
            <p>Действий пока нет</p>
          )}
          <Link to={"/audit?" + auditParams.toString()}>
            Открыть журнал этой тревоги
          </Link>
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
  function submit(e: FormEvent<HTMLFormElement>) {
    e.preventDefault();
    const fields = new FormData(e.currentTarget),
      id = fields.get("alert"),
      q = fields.get("query");
    setSearch({
      ...(typeof q === "string" && q.trim() ? { query: q.trim() } : {}),
      ...(typeof id === "string" && id.trim() ? { alert_id: id.trim() } : {}),
    });
  }
  return (
    <section className="ws-panel ws-pad ws-alert-work">
      <h1>Журнал тревог</h1>
      <form onSubmit={submit}>
        <label>
          Идентификатор тревоги
          <input name="alert" defaultValue={initial} maxLength={36} />
        </label>
        <label>
          Результат или основание
          <input name="query" maxLength={256} />
        </label>
        <Button type="submit">Найти</Button>
      </form>
      {page ? (
        page.items.length ? (
          <Entries items={page.items} site={site} points={points} />
        ) : (
          <p role="status">Записи не найдены</p>
        )
      ) : (
        <p role="status">
          {error ? "Не удалось получить записи журнала" : "Загрузка журнала…"}
        </p>
      )}
      {page?.next_cursor && (
        <Button
          onClick={() => setSearch({ ...search, before: page.next_cursor! })}
        >
          Следующая страница
        </Button>
      )}
    </section>
  );
}
