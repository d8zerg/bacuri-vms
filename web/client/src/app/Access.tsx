import { useEffect, useState } from "react";
import type { FormEvent } from "react";
import type { components } from "../../../../build/contracts/api";
import type { Point, Session } from "../contracts/workspace";
import { api, data } from "../adapters/api";
import { Button } from "../ui-kit/FormControls";
import "./access.css";

type Schema = components["schemas"];
type User = Schema["AccessUser"];
type Group = Schema["AccessGroup"];
type Grant = Schema["AccessGrant"];
const actions = {
  view: "Просмотр видео",
  audio: "Звук",
  ptz: "Управление PTZ",
  configure: "Настройка камеры",
  protect: "Защита записи",
  export: "Экспорт оригинала",
  view_ptz: "Просмотр и PTZ",
};
const outputs = {
  live: "Эфир",
  archive: "Архив",
  thumbnail: "Миниатюры",
  export: "Экспорт",
};
const tabs = [
  "Пользователи",
  "Группы",
  "Разрешения",
  "Приватность",
  "Вход",
  "Аудит",
] as const;
type Tab = (typeof tabs)[number];
interface Policy {
  revision: string;
  users: User[];
  groups: Group[];
  grants: Grant[];
  privacy: Schema["PrivacyEntry"][];
  provider: Schema["ProviderInfo"];
  audit: Schema["AccessAuditEvent"][];
  points: Point[];
}
async function load(
  site: string,
  csrf: string,
  signal: AbortSignal,
): Promise<Policy> {
  const params = { path: { siteId: site } };
  const [users, groups, grants, privacy, provider, audit, points] =
    await Promise.all([
      api.GET("/sites/{siteId}/users", { params, signal }).then(data),
      api.GET("/sites/{siteId}/resource-groups", { params, signal }).then(data),
      api.GET("/sites/{siteId}/grants", { params, signal }).then(data),
      api
        .GET("/sites/{siteId}/privacy-policies", { params, signal })
        .then(data),
      api
        .GET("/sites/{siteId}/identity/provider", { params, signal })
        .then(data),
      api
        .POST("/sites/{siteId}/audit/search", {
          params: { ...params, header: { "X-CSRF-Token": csrf } },
          body: {},
          signal,
        })
        .then(data),
      api.GET("/sites/{siteId}/points", { params, signal }).then(data),
    ]);
  if (
    ![groups, grants, privacy, provider, audit].every(
      (r) => r.revision === users.revision,
    )
  )
    throw Error("Права изменились во время загрузки. Обновите данные.");
  return {
    revision: users.revision,
    users: users.data,
    groups: groups.data,
    grants: grants.data,
    privacy: privacy.data,
    provider: provider.data,
    audit: audit.data,
    points,
  };
}
const field = (form: FormData, key: string) => {
  const value = form.get(key);
  return typeof value === "string" ? value : "";
};
function Roles({ value = "operator" }: { value?: string }) {
  return (
    <label>
      Роль
      <select name="role" defaultValue={value}>
        <option value="operator">Оператор</option>
        <option value="administrator">Администратор</option>
      </select>
    </label>
  );
}
function People({ users }: { users: User[] }) {
  return (
    <label>
      Пользователь
      <select required name="user">
        {users.map((u) => (
          <option value={u.id} key={u.id}>
            {u.username}
          </option>
        ))}
      </select>
    </label>
  );
}

export default function Access({
  site,
  session,
  expire,
}: {
  site: string;
  session: Session;
  expire: () => void;
}) {
  const [tab, setTab] = useState<Tab>("Пользователи");
  const [policy, setPolicy] = useState<Policy | null>(null);
  const [attempt, setAttempt] = useState(0),
    [busy, setBusy] = useState(false),
    [message, setMessage] = useState("");
  const [group, setGroup] = useState(""),
    [point, setPoint] = useState("");
  useEffect(() => {
    const ctl = new AbortController();
    void load(site, session.csrf_token, ctl.signal)
      .then((p) => {
        if (!ctl.signal.aborted) setPolicy(p);
      })
      .catch((err: unknown) => {
        if (ctl.signal.aborted) return;
        setPolicy(null);
        if (err instanceof Error && ["401", "403"].includes(err.message))
          expire();
        else
          setMessage("Не удалось получить актуальные права. Обновите данные.");
      });
    return () => ctl.abort();
  }, [site, session.csrf_token, attempt, expire]);

  useEffect(() => {
    const controller = new AbortController();
    const timer = window.setInterval(() => {
      void api
        .GET("/session", { signal: controller.signal })
        .then((result) => {
          if (
            !controller.signal.aborted &&
            (result.response.status === 401 ||
              (result.data &&
                !result.data.site_roles.some(
                  (r) => r.site_id === site && r.role === "administrator",
                )))
          )
            expire();
        })
        .catch(() => {});
    }, 4000);
    return () => {
      controller.abort();
      window.clearInterval(timer);
    };
  }, [site, expire]);

  async function save(path: string, method: string, body?: unknown) {
    if (!policy || busy) return;
    setBusy(true);
    setMessage("");
    try {
      const response = await fetch("/api/v1/sites/" + site + "/" + path, {
        method,
        credentials: "same-origin",
        cache: "no-store",
        headers: {
          "Content-Type": "application/json",
          "X-CSRF-Token": session.csrf_token,
          "If-Match": '"' + policy.revision + '"',
        },
        body: body === undefined ? null : JSON.stringify(body),
      });
      if (response.status === 401 || response.status === 403) {
        expire();
        return;
      }
      if (response.status === 412) {
        setMessage(
          "Другой администратор изменил политику. Проверьте обновлённые данные.",
        );
      } else if (!response.ok) {
        const problem = (await response.json()) as { code: string };
        setMessage(
          (
            {
              last_administrator:
                "На площадке нужен действующий администратор.",
              account_conflict: "Имя пользователя занято.",
              identity_conflict: "Эта внешняя учётная запись уже привязана.",
              group_conflict: "Название группы занято.",
            } as Record<string, string>
          )[problem.code] ??
            "Не удалось сохранить изменение. Проверьте значения.",
        );
      } else setMessage("Изменение сохранено.");
      setAttempt((n) => n + 1);
    } catch {
      setMessage("Связь прервана. Обновите данные перед повтором.");
    } finally {
      setBusy(false);
    }
  }
  function form(action: (f: FormData) => void) {
    return (e: FormEvent<HTMLFormElement>) => {
      e.preventDefault();
      action(new FormData(e.currentTarget));
    };
  }
  const selected = policy?.groups.find((g) => g.id === group);
  const selectedPoint = point || policy?.points[0]?.id || "";
  const privacy = policy?.privacy.find((p) => p.point_id === selectedPoint);
  return (
    <section className="ws-panel ws-pad ws-access">
      <h1>Доступ и пользователи</h1>
      <nav aria-label="Управление доступом">
        {tabs.map((t) => (
          <button key={t} aria-pressed={tab === t} onClick={() => setTab(t)}>
            {t}
          </button>
        ))}
      </nav>
      <p role="status">{message}</p>
      <Button onClick={() => setAttempt((n) => n + 1)}>Обновить данные</Button>
      {!policy ? (
        <p role="status">Ожидание данных площадки</p>
      ) : (
        <fieldset
          disabled={busy}
          key={site + ":" + tab + ":" + policy.revision}
        >
          <legend>{tab}</legend>
          {tab === "Пользователи" && (
            <>
              <form
                onSubmit={form(
                  (f) =>
                    void save("users", "POST", {
                      username: field(f, "name"),
                      password: field(f, "password"),
                      role: field(f, "role"),
                    }),
                )}
              >
                <h2>Новый пользователь</h2>
                <label>
                  Имя
                  <input required name="name" maxLength={128} />
                </label>
                <label>
                  Начальный пароль
                  <input
                    required
                    type="password"
                    autoComplete="new-password"
                    name="password"
                  />
                </label>
                <Roles />
                <Button type="submit">Создать пользователя</Button>
              </form>
              {policy.users.map((u) => (
                <form
                  key={u.id}
                  onSubmit={form(
                    (f) =>
                      void save("users/" + u.id, "PATCH", {
                        role: field(f, "role"),
                        enabled: f.has("enabled"),
                      }),
                  )}
                >
                  <h2>{u.username}</h2>
                  <Roles value={u.role} />
                  <label>
                    <input
                      type="checkbox"
                      name="enabled"
                      defaultChecked={u.enabled}
                    />
                    Учётная запись активна
                  </label>
                  <Button type="submit">Сохранить пользователя</Button>
                </form>
              ))}
            </>
          )}
          {tab === "Группы" && (
            <>
              <label>
                Группа ресурсов
                <select
                  value={group}
                  onChange={(e) => setGroup(e.target.value)}
                >
                  <option value="">Новая группа</option>
                  {policy.groups.map((g) => (
                    <option key={g.id} value={g.id}>
                      {g.name}
                    </option>
                  ))}
                </select>
              </label>
              <form
                key={group}
                onSubmit={form(
                  (f) =>
                    void save(
                      "resource-groups" + (group ? "/" + group : ""),
                      group ? "PUT" : "POST",
                      { name: field(f, "name"), points: f.getAll("points") },
                    ),
                )}
              >
                <label>
                  Название
                  <input name="name" required defaultValue={selected?.name} />
                </label>
                <fieldset>
                  <legend>Точки наблюдения</legend>
                  {policy.points.map((p) => (
                    <label key={p.id}>
                      <input
                        type="checkbox"
                        name="points"
                        value={p.id}
                        defaultChecked={selected?.points.includes(p.id)}
                      />
                      {p.name}
                    </label>
                  ))}
                </fieldset>
                <Button type="submit">Сохранить группу</Button>
              </form>
            </>
          )}
          {tab === "Разрешения" && (
            <>
              <form
                onSubmit={form((f) => {
                  const target = field(f, "target"),
                    start = field(f, "start"),
                    end = field(f, "end");
                  if (
                    Boolean(start) !== Boolean(end) ||
                    (start && start >= end)
                  ) {
                    setMessage("Проверьте обе границы интервала.");
                    return;
                  }
                  void save("grants/" + crypto.randomUUID(), "PUT", {
                    user_id: field(f, "user"),
                    action: field(f, "action"),
                    [target.startsWith("g:") ? "group_id" : "point_id"]:
                      target.slice(2),
                    ...(start && end
                      ? {
                          interval: {
                            start: new Date(start).toISOString(),
                            end: new Date(end).toISOString(),
                          },
                        }
                      : {}),
                  });
                })}
              >
                <People users={policy.users} />
                <label>
                  Область
                  <select name="target" required>
                    {policy.points.map((p) => (
                      <option key={p.id} value={"p:" + p.id}>
                        {p.name}
                      </option>
                    ))}
                    {policy.groups.map((g) => (
                      <option key={g.id} value={"g:" + g.id}>
                        Группа {g.name}
                      </option>
                    ))}
                  </select>
                </label>
                <label>
                  Действие
                  <select name="action">
                    {Object.entries(actions)
                      .filter(([k]) => k !== "view_ptz")
                      .map(([k, v]) => (
                        <option key={k} value={k}>
                          {v}
                        </option>
                      ))}
                  </select>
                </label>
                <label>
                  Начало интервала просмотра или экспорта
                  <input type="datetime-local" name="start" />
                </label>
                <label>
                  Конец интервала
                  <input type="datetime-local" name="end" />
                </label>
                <Button type="submit">Выдать разрешение</Button>
              </form>
              <ul>
                {policy.grants.map((g) => (
                  <li key={g.id}>
                    {policy.users.find((u) => u.id === g.user_id)?.username} ·{" "}
                    {actions[g.action]} ·{" "}
                    {policy.points.find((p) => p.id === g.point_id)?.name ??
                      policy.groups.find((r) => r.id === g.group_id)?.name}
                    {g.begin_us &&
                      g.end_us &&
                      " · " +
                        new Date(Number(g.begin_us) / 1000).toLocaleString() +
                        " - " +
                        new Date(Number(g.end_us) / 1000).toLocaleString()}
                    <Button
                      onClick={() => void save("grants/" + g.id, "DELETE")}
                    >
                      Отозвать
                    </Button>
                  </li>
                ))}
              </ul>
            </>
          )}
          {tab === "Приватность" && (
            <>
              <label>
                Точка
                <select
                  value={selectedPoint}
                  onChange={(e) => setPoint(e.target.value)}
                >
                  {policy.points.map((p) => (
                    <option key={p.id} value={p.id}>
                      {p.name}
                    </option>
                  ))}
                </select>
              </label>
              <form
                key={selectedPoint}
                onSubmit={form((f) => {
                  const lines = field(f, "regions").trim();
                  const regions = lines
                    ? lines.split(/\r?\n/).map((line) => {
                        const [left, top, right, bottom, ...extra] = line
                          .trim()
                          .split(/\s+/)
                          .map(Number);
                        if (
                          extra.length ||
                          left === undefined ||
                          top === undefined ||
                          right === undefined ||
                          bottom === undefined ||
                          ![left, top, right, bottom].every(
                            (v) => Number.isFinite(v) && v >= 0 && v <= 1,
                          ) ||
                          left >= right ||
                          top >= bottom
                        )
                          return null;
                        return { left, top, right, bottom };
                      })
                    : [];
                  if (regions.includes(null) || regions.length > 32) {
                    setMessage(
                      "Проверьте координаты областей маски. Допускается до 32 прямоугольников.",
                    );
                    return;
                  }
                  void save("privacy-policies/" + selectedPoint, "PUT", {
                    ...Object.fromEntries(
                      Object.keys(outputs).map((k) => [k, f.has(k)]),
                    ),
                    regions,
                  });
                })}
              >
                <fieldset>
                  <legend>Закрыть выдачу изображения</legend>
                  {(Object.keys(outputs) as (keyof typeof outputs)[]).map(
                    (k) => (
                      <label key={k}>
                        <input
                          name={k}
                          type="checkbox"
                          defaultChecked={privacy?.[k]}
                        />
                        {outputs[k]}
                      </label>
                    ),
                  )}
                </fieldset>
                <label>
                  Области маски (слева, сверху, справа, снизу)
                  <textarea
                    name="regions"
                    aria-describedby="mask-help"
                    defaultValue={
                      privacy?.regions
                        ?.map((r) =>
                          [r.left, r.top, r.right, r.bottom].join(" "),
                        )
                        .join("\n") ?? ""
                    }
                  />
                </label>
                <p id="mask-help">
                  Введите четыре числа от 0 до 1 через пробел. Каждый
                  прямоугольник занимает одну строку. Пустое поле задаёт весь
                  кадр. До подключения маскирования выбранные виды выдачи
                  закрываются полностью.
                </p>
                <Button type="submit" disabled={!selectedPoint}>
                  Применить политику
                </Button>
              </form>
            </>
          )}
          {tab === "Вход" && (
            <>
              <form
                onSubmit={form((f) => {
                  const secret = field(f, "secret");
                  void save("identity/provider", "PUT", {
                    enabled: f.has("enabled"),
                    issuer: field(f, "issuer"),
                    client_id: field(f, "client"),
                    admin_acr: field(f, "acr"),
                    ...(secret ? { client_secret: secret } : {}),
                  });
                })}
              >
                <h2>OpenID Connect</h2>
                <label>
                  <input
                    type="checkbox"
                    name="enabled"
                    defaultChecked={policy.provider.enabled}
                  />
                  Корпоративный вход через Keycloak
                </label>
                <label>
                  Issuer HTTPS
                  <input
                    name="issuer"
                    type="url"
                    required
                    defaultValue={policy.provider.issuer}
                  />
                </label>
                <label>
                  Client ID
                  <input
                    name="client"
                    required
                    defaultValue={policy.provider.client_id}
                  />
                </label>
                <label>
                  Новый секрет клиента
                  <input
                    type="password"
                    autoComplete="new-password"
                    name="secret"
                    required={!policy.provider.issuer}
                  />
                </label>
                <label>
                  Уровень MFA администратора (acr)
                  <input
                    required
                    name="acr"
                    defaultValue={policy.provider.admin_acr}
                  />
                </label>
                <Button type="submit">Сохранить провайдера</Button>
              </form>
              <form
                onSubmit={form(
                  (f) =>
                    void save(
                      "users/" + field(f, "user") + "/external-identity",
                      "PUT",
                      {
                        issuer: policy.provider.issuer,
                        subject: field(f, "subject"),
                      },
                    ),
                )}
              >
                <h2>Внешняя учётная запись</h2>
                <People users={policy.users} />
                <label>
                  Идентификатор в Keycloak (sub)
                  <input required name="subject" />
                </label>
                <Button type="submit" disabled={!policy.provider.issuer}>
                  Привязать
                </Button>
              </form>
              <ul>
                {policy.users
                  .filter((u) => u.subject)
                  .map((u) => (
                    <li key={u.id}>
                      {u.username} · {u.issuer} · {u.subject}{" "}
                      <Button
                        onClick={() =>
                          void save(
                            "users/" + u.id + "/external-identity",
                            "DELETE",
                          )
                        }
                      >
                        Отвязать
                      </Button>
                    </li>
                  ))}
              </ul>
            </>
          )}
          {tab === "Аудит" && (
            <ol>
              {policy.audit.map((a) => (
                <li key={a.id}>
                  <time>{new Date(a.recorded_at).toLocaleString()}</time> ·{" "}
                  {policy.users.find((u) => u.id === a.actor)?.username ??
                    a.actor}{" "}
                  ({a.method}) · {a.action} · {a.resource}
                </li>
              ))}
            </ol>
          )}
        </fieldset>
      )}
    </section>
  );
}
