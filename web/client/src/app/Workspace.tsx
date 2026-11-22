import { lazy, Suspense, useEffect, useRef, useState } from "react";
import type { FormEvent } from "react";
import {
  NavLink,
  RouterProvider,
  createBrowserRouter,
  createMemoryRouter,
  replace,
  useLocation,
  useSearchParams,
  type RouteObject,
} from "react-router";
import type { Feed, Point, Services, Session } from "../contracts/workspace";
import { HttpFailure } from "../adapters/api";
import { Installer } from "./installer";
import { Monitor } from "./monitor";
import { ThemeSwitch, useWorkspaceTheme } from "./WorkspaceTheme";
import { Button } from "../ui-kit/FormControls";
import icons from "../../../../design/bacuri/icons/sprite.svg";
import mark from "../../../../design/bacuri/brand/mark.svg";
import "./workspace.css";
const Audit = lazy(() =>
  import("./AlertWork").then((m) => ({ default: m.AlertAudit })),
);
const Access = lazy(() => import("./Access"));
const Maps = lazy(() => import("./Maps"));
const waiting: Feed = { items: [], status: "loading" };
function Login({
  services,
  onLogin,
  message,
}: {
  services: Services;
  onLogin: (user: Session) => void;
  message: string;
}) {
  const [notice, setNotice] = useState(message),
    [busy, setBusy] = useState(false),
    [enroll, setEnroll] = useState(false);
  const [corporate, setCorporate] = useState(false);
  useEffect(() => {
    const controller = new AbortController();
    void fetch("/api/v1/session/oidc/methods", {
      signal: controller.signal,
      cache: "no-store",
    })
      .then(
        async (res) => res.ok && ((await res.json()) as { oidc: boolean }).oidc,
      )
      .then((enabled) => {
        if (!controller.signal.aborted) setCorporate(enabled === true);
      })
      .catch(() => {});
    return () => controller.abort();
  }, []);
  const enrollmentInput = useRef<HTMLInputElement>(null);
  useEffect(() => {
    if (enroll) enrollmentInput.current?.focus();
  }, [enroll]);
  async function submit(e: FormEvent<HTMLFormElement>) {
    e.preventDefault();
    if (busy) return;
    const fields = new FormData(e.currentTarget);
    const username = fields.get("username"),
      password = fields.get("password"),
      enrollment = fields.get("enrollment");
    if (typeof username !== "string" || typeof password !== "string") return;
    setBusy(true);
    try {
      onLogin(
        await services.login(
          username,
          password,
          typeof enrollment === "string" ? enrollment : undefined,
        ),
      );
    } catch (error) {
      if (
        error instanceof HttpFailure &&
        error.code === "enrollment_required"
      ) {
        setEnroll(true);
        setNotice(
          "Введите одноразовый код из локальной консоли площадки и подключите ключ безопасности.",
        );
        return;
      }
      if (error instanceof HttpFailure && error.code === "invalid_credentials")
        setNotice("Проверьте имя пользователя и пароль.");
      else if (error instanceof HttpFailure && error.status === 429)
        setNotice("Слишком много попыток входа. Попробуйте позже.");
      else if (
        (error instanceof HttpFailure && error.code === "invalid_factor") ||
        (error instanceof DOMException && error.name === "NotAllowedError")
      )
        setNotice(
          "Ключ безопасности не подтвердил вход. Повторите попытку с подключённым ключом.",
        );
      else
        setNotice(
          "Не удалось завершить вход. Проверьте связь с площадкой и повторите попытку.",
        );
    } finally {
      setBusy(false);
    }
  }
  return (
    <main className="ws-login ws-panel ws-pad">
      <img className="ws-login-mark" alt="" src={mark} />
      <h1>Вход в Bacuri VMS</h1>
      <form onSubmit={(e) => void submit(e)}>
        <label>
          Имя пользователя
          <input required name="username" autoComplete="username" />
        </label>
        <label>
          Пароль
          <input
            required
            type="password"
            name="password"
            autoComplete="current-password"
          />
        </label>
        {enroll && (
          <label>
            Код первичной привязки
            <input
              type="password"
              ref={enrollmentInput}
              name="enrollment"
              autoComplete="off"
              required
            />
          </label>
        )}
        <Button type="submit" variant="primary" disabled={busy}>
          {busy ? "Выполняется вход…" : "Войти"}
        </Button>
      </form>
      {corporate && (
        <a href="/api/v1/session/oidc/start">Войти через Keycloak</a>
      )}
      <p role="status">{notice}</p>
    </main>
  );
}
function Site({
  services,
  site,
  admin,
  userId,
  cameras,
  maps,
  audit,
  onExpired,
}: {
  services: Services;
  site: string;
  admin: boolean;
  userId: string;
  cameras: boolean;
  maps: boolean;
  audit: boolean;
  onExpired: () => void;
}) {
  const [params] = useSearchParams();
  const [points, setPoints] = useState<Point[] | null>(null),
    [feed, setFeed] = useState<Feed>(waiting),
    [failed, setFailed] = useState(false);
  useEffect(() => {
    const ctl = new AbortController();
    let running = false;
    async function refresh() {
      if (running) return;
      running = true;
      try {
        const signal = AbortSignal.any([ctl.signal, AbortSignal.timeout(1500)]);
        const [identity, next] = await Promise.all([
          services.session(signal),
          services.points(site, signal),
        ]);
        if (ctl.signal.aborted) return;
        const role = identity.site_roles.find((s) => s.site_id === site)?.role;
        if (
          identity.user_id !== userId ||
          !role ||
          admin !== (role === "administrator")
        ) {
          onExpired();
          return;
        }
        setPoints((current) =>
          JSON.stringify(current) === JSON.stringify(next) ? current : next,
        );
        setFailed(false);
      } catch (err) {
        if (
          !ctl.signal.aborted &&
          err instanceof Error &&
          err.message === "401"
        ) {
          onExpired();
          return;
        }
        if (!ctl.signal.aborted) {
          setPoints(null);
          setFailed(true);
        }
      } finally {
        running = false;
      }
    }
    void refresh();
    const timer = setInterval(() => void refresh(), 2000);
    const unsubscribe = services.subscribe(site, (next) => {
      if (!ctl.signal.aborted) {
        setFeed(next);
        if (next.status === "denied") {
          setPoints(null);
          void refresh();
        }
      }
    });
    return () => {
      ctl.abort();
      clearInterval(timer);
      unsubscribe();
    };
  }, [services, site, userId, admin, onExpired]);
  useEffect(() => {
    const available =
      points &&
      (cameras
        ? admin
        : feed.status === "online" &&
          feed.items.some((item) =>
            points.some((point) => point.id === item.point_id),
          ));
    if (available && !performance.getEntriesByName("workspace-action").length)
      performance.mark("workspace-action");
  }, [points, cameras, admin, feed]);
  const notice = failed
    ? "Нет связи или доступа к площадке. Восстанавливаем подключение…"
    : "Загрузка площадки…";
  if (cameras && admin)
    return (
      <Installer
        services={services}
        site={site}
        online={!!points}
        connectionNotice={notice}
      />
    );
  if (!points) return <p role="status">{notice}</p>;
  if (audit)
    return (
      <Suspense fallback={<p role="status">Загрузка журнала…</p>}>
        <Audit
          key={params.get("alert_id") ?? ""}
          services={services}
          site={site}
          points={points}
        />
      </Suspense>
    );
  if (maps)
    return (
      <Suspense fallback={<p role="status">Загрузка карт и планов…</p>}>
        <Maps
          services={services}
          site={site}
          admin={admin}
          points={points}
          feed={feed}
        />
      </Suspense>
    );
  if (cameras)
    return <p role="status">Недостаточно прав для подключения камеры</p>;
  return (
    <Monitor
      services={services}
      site={site}
      points={points}
      feed={{
        ...feed,
        items: feed.items.filter((item) =>
          points.some((p) => p.id === item.point_id),
        ),
      }}
    />
  );
}
function Workspace({ services }: { services: Services }) {
  const theme = useWorkspaceTheme();
  const [identity, setIdentity] = useState<Session | null | undefined>(
    undefined,
  );
  const [message, setMessage] = useState("");
  const [sessionFailed, setSessionFailed] = useState(false);
  const [sessionAttempt, setSessionAttempt] = useState(0);
  useEffect(() => {
    const channel = new BroadcastChannel("bacuri-session");
    channel.onmessage = () => setSessionAttempt((n) => n + 1);
    return () => channel.close();
  }, []);
  const [params, setParams] = useSearchParams();
  const location = useLocation();
  const path = location.pathname.replace(/\/$/, "");
  const cameras = path === "/cameras";
  useEffect(() => {
    const ctl = new AbortController();
    void services
      .session(AbortSignal.any([ctl.signal, AbortSignal.timeout(8000)]))
      .then((user) => {
        if (!ctl.signal.aborted) setIdentity(user);
      })
      .catch((error: unknown) => {
        if (ctl.signal.aborted) return;
        if (error instanceof HttpFailure && error.status === 401)
          setIdentity(null);
        else setSessionFailed(true);
      });
    return () => ctl.abort();
  }, [services, sessionAttempt]);
  const [expired] = useState(() => () => {
    setMessage("Сеанс или права изменились. Войдите снова.");
    setIdentity(null);
  });
  const section =
    {
      "/maps": "Карты и планы",
      "/audit": "Журнал тревог",
      "/access": "Доступ и пользователи",
      "/cameras": "Подключение камеры",
    }[path] ?? "Мониторинг";
  useEffect(() => {
    document.title =
      (identity ? section : identity === null ? "Вход" : "Загрузка") +
      " | Bacuri VMS";
  }, [section, identity]);
  if (sessionFailed)
    return (
      <main className="ws-login ws-panel">
        <h1>Площадка недоступна</h1>
        <p role="status">
          Не удалось проверить сеанс. Проверьте соединение с площадкой.
        </p>
        <Button
          variant="primary"
          onClick={() => {
            setSessionFailed(false);
            setSessionAttempt((n) => n + 1);
          }}
        >
          Повторить подключение
        </Button>
      </main>
    );
  if (identity === undefined)
    return (
      <main className="ws-startup" role="status">
        Загрузка сеанса…
      </main>
    );
  if (!identity)
    return (
      <Login
        services={services}
        onLogin={(user) => {
          setMessage("");
          setIdentity(user);
        }}
        message={message}
      />
    );
  const site = params.has("site")
    ? identity.site_roles.find((s) => s.site_id === params.get("site"))
    : identity.site_roles[0];
  const context = new URLSearchParams(params);
  if (site) context.set("site", site.site_id);
  const query = context.size ? "?" + context.toString() : "";
  const auditQuery = new URLSearchParams(context);
  auditQuery.delete("alert_id");
  return (
    <div className="ws-shell">
      <a className="ws-skip" href="#workspace">
        К содержимому
      </a>
      <header className="ws-bar">
        <span className="ws-brand">
          <img alt="" src={mark} />
          Bacuri VMS
        </span>
        <nav aria-label="Рабочее место">
          <NavLink to={"/monitor" + query}>
            <svg aria-hidden="true">
              <use href={icons + "#i-monitor-play"} />
            </svg>
            <span>Мониторинг</span>
          </NavLink>
          <NavLink
            className="ws-audit-link"
            aria-label="Журнал тревог"
            to={"/audit?" + auditQuery.toString()}
          >
            <svg aria-hidden="true">
              <use href={icons + "#i-bell-ring"} />
            </svg>
            <span>
              Журнал<span className="ws-nav-detail"> тревог</span>
            </span>
          </NavLink>
          <NavLink to={"/maps" + query} aria-label="Карты и планы">
            <svg aria-hidden="true">
              <use href={icons + "#i-map"} />
            </svg>
            <span>
              Карты<span className="ws-nav-detail"> и планы</span>
            </span>
          </NavLink>
          {site?.role === "administrator" && (
            <NavLink to={"/access" + query}>
              <svg aria-hidden="true">
                <use href={icons + "#i-shield-check"} />
              </svg>
              <span>Доступ</span>
            </NavLink>
          )}
          {site?.role === "administrator" && (
            <NavLink to={"/cameras" + query}>
              <svg aria-hidden="true">
                <use href={icons + "#i-cctv"} />
              </svg>
              <span>Камеры</span>
            </NavLink>
          )}
        </nav>
        <div className="ws-session">
          {site && (
            <span className="ws-role">
              {site.role === "administrator" ? "Администратор" : "Оператор"}
            </span>
          )}
          {identity.site_roles.length > 1 && (
            <label className="ws-site-select">
              Площадка{" "}
              <select
                value={site?.site_id ?? ""}
                onChange={(e) => void setParams({ site: e.target.value })}
              >
                {!site && (
                  <option value="" disabled>
                    Выберите площадку
                  </option>
                )}
                {identity.site_roles.map((s) => (
                  <option key={s.site_id}>{s.site_id}</option>
                ))}
              </select>
            </label>
          )}
          <div className="ws-session-actions">
            <ThemeSwitch {...theme} />
            <Button
              className="ws-logout"
              title="Выйти"
              aria-label="Выйти"
              onClick={() => {
                void services
                  .logout()
                  .then(() => {
                    setMessage("");
                    setIdentity(null);
                    const channel = new BroadcastChannel("bacuri-session");
                    channel.postMessage("logout");
                    channel.close();
                  })
                  .catch(() => setMessage("Не удалось завершить сеанс"));
              }}
            >
              <svg aria-hidden="true">
                <use href={icons + "#i-log-out"} />
              </svg>
            </Button>
          </div>
        </div>
      </header>
      <main id="workspace" tabIndex={-1}>
        {message && <p role="status">{message}</p>}
        {site && path === "/access" ? (
          site.role === "administrator" ? (
            <Suspense fallback={<p role="status">Загрузка прав</p>}>
              <Access
                key={site.site_id}
                site={site.site_id}
                session={identity}
                expire={expired}
              />
            </Suspense>
          ) : (
            <p role="status">Недостаточно прав</p>
          )
        ) : site ? (
          <Site
            key={site.site_id}
            services={services}
            site={site.site_id}
            userId={identity.user_id}
            admin={site.role === "administrator"}
            cameras={cameras}
            maps={path === "/maps"}
            audit={path === "/audit"}
            onExpired={expired}
          />
        ) : (
          <div className="ws-panel ws-pad">
            <h1>
              {identity.site_roles.length
                ? "Площадка недоступна"
                : "Нет доступных площадок"}
            </h1>
            {identity.site_roles[0] && (
              <Button
                onClick={() =>
                  void setParams({ site: identity.site_roles[0]!.site_id })
                }
              >
                Открыть доступную площадку
              </Button>
            )}
          </div>
        )}
      </main>
    </div>
  );
}
export function WorkspaceRouter({
  services,
  initial,
}: {
  services: Services;
  initial?: string;
}) {
  const [router] = useState(() => {
    const routes: RouteObject[] = [
      {
        path: "/",
        loader: ({ request }) =>
          replace("/monitor" + new URL(request.url).search),
      },
      ...["/monitor", "/cameras", "/maps", "/audit", "/access"].map((path) => ({
        path,
        element: <Workspace services={services} />,
      })),
    ];
    return initial
      ? createMemoryRouter(routes, { initialEntries: [initial] })
      : createBrowserRouter(routes);
  });
  useEffect(() => () => router.dispose(), [router]);
  return <RouterProvider router={router} />;
}
