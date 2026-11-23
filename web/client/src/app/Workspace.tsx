import { lazy, Suspense, useEffect, useMemo, useRef, useState } from "react";
import type { FormEvent } from "react";
import {
  NavLink,
  BrowserRouter,
  MemoryRouter,
  Navigate,
  useRoutes,
  useLocation,
  useSearchParams,
  type RouteObject,
} from "react-router";
import type { Feed, Point, Services, Session } from "../contracts/workspace";
import { HttpFailure } from "../adapters/api";
import { SubsectionSlot } from "./SubsectionMenu";
import { cameraSetup, Installer } from "./installer";
import "../ui-kit/media-panels.css";
import { ThemeSwitch, useWorkspaceTheme } from "./WorkspaceTheme";
import { Button } from "../ui-kit/FormControls";
import icons from "../../../../design/bacuri/icons/sprite.svg";
import mark from "../../../../design/bacuri/brand/mark.svg";
import "./workspace.css";
const Audit = lazy(() =>
  import("./AlertWork").then((m) => ({ default: m.AlertAudit })),
);
const Access = lazy(() => import("./Access"));
const Monitor = lazy(() =>
  import("./monitor").then((m) => ({ default: m.Monitor })),
);
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
        <a className="bk-button" href="/api/v1/session/oidc/start">
          Войти через Keycloak
        </a>
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
  const visibleFeed = useMemo(() => {
    const allowed = new Set(points?.map((point) => point.id));
    return {
      ...feed,
      items: feed.items.filter((item) => allowed.has(item.point_id)),
    };
  }, [feed, points]);
  const notice = failed
    ? "Нет связи или доступа к площадке. Восстанавливаем подключение…"
    : "Загрузка площадки…";
  if (cameras && admin)
    return (
      <Installer
        services={services}
        site={site}
        points={points}
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
    <Suspense fallback={<p role="status">Загрузка мониторинга…</p>}>
      <Monitor
        admin={admin}
        services={services}
        site={site}
        points={points}
        feed={visibleFeed}
      />
    </Suspense>
  );
}
const sidebarKey = "bacuri.vms.sidebar.collapsed";
const sections = [
  { path: "/monitor", label: "Мониторинг", icon: "monitor-play" },
  {
    path: "/audit",
    label: "Журнал",
    detail: " действий",
    icon: "clipboard-list",
  },
  { path: "/maps", label: "Карты", detail: " и планы", icon: "map" },
  { path: "/access", label: "Доступ", icon: "shield-check", admin: true },
  { path: "/cameras", label: "Камеры", icon: "cctv", admin: true },
];
function Workspace({ services }: { services: Services }) {
  const theme = useWorkspaceTheme();
  const [submenuSlot, setSubmenuSlot] = useState<HTMLDivElement | null>(null);
  const [collapsed, setCollapsed] = useState(() => {
    try {
      return localStorage.getItem(sidebarKey) === "true";
    } catch {
      return false;
    }
  });
  function toggleSidebar() {
    setCollapsed(!collapsed);
    try {
      localStorage.setItem(sidebarKey, String(!collapsed));
    } catch {
      // Keep the selection for this session when browser storage is unavailable.
    }
  }
  const toggleLabel = collapsed ? "Развернуть меню" : "Свернуть меню";
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
      "/audit": "Журнал действий",
      "/access": "Доступ и пользователи",
      "/cameras": {
        manual: "Подключение камеры",
        onvif: "Поиск ONVIF и замена камеры",
        batch: "Массовое подключение",
        recording: "Запись и хранение",
        catalog: "Каталог подключённых устройств",
      }[cameraSetup(params.get("setup")) ?? "catalog"],
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
  context.delete("setup");
  if (site) context.set("site", site.site_id);
  const query = context.size ? "?" + context.toString() : "";
  const auditQuery = new URLSearchParams(context);
  auditQuery.delete("alert_id");
  return (
    <div className="ws-shell" data-collapsed={collapsed || undefined}>
      <a className="ws-skip" href="#workspace">
        К содержимому
      </a>
      <header className="ws-bar">
        <div className="ws-navigation">
          <span className="ws-brand">
            <img alt="Bacuri VMS" src={mark} />
            <span aria-hidden="true">Bacuri VMS</span>
          </span>
          <nav id="workspace-navigation" aria-label="Рабочее место">
            {sections
              .filter(
                (section) => !section.admin || site?.role === "administrator",
              )
              .map(({ path: sectionPath, label, detail, icon }) => {
                const name = label + (detail ?? "");
                return (
                  <div className="ws-nav-section" key={sectionPath}>
                    <NavLink
                      to={
                        sectionPath +
                        (sectionPath === "/audit"
                          ? "?" + auditQuery.toString()
                          : query)
                      }
                      aria-label={name}
                      title={name}
                    >
                      <svg aria-hidden="true">
                        <use href={icons + "#i-" + icon} />
                      </svg>
                      <span>
                        {label}
                        {detail && (
                          <span className="ws-nav-detail">{detail}</span>
                        )}
                      </span>
                    </NavLink>
                    {sectionPath === path && (
                      <div className="ws-submenu-slot" ref={setSubmenuSlot} />
                    )}
                  </div>
                );
              })}
          </nav>
          <button
            type="button"
            className="ws-sidebar-toggle"
            onClick={toggleSidebar}
            title={toggleLabel}
            aria-label={toggleLabel}
            aria-expanded={!collapsed}
            aria-controls="workspace-navigation"
          >
            <svg
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              strokeWidth="1.75"
              strokeLinecap="round"
              strokeLinejoin="round"
              aria-hidden="true"
            >
              <rect x="3" y="4" width="18" height="16" rx="2" />
              <path d="M8 4v16" />
              <path className="ws-sidebar-chevron" d="m16 9-3 3 3 3" />
            </svg>
            <span>{toggleLabel}</span>
          </button>
        </div>
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
      <SubsectionSlot.Provider value={submenuSlot}>
        <main id="workspace" tabIndex={-1}>
          {site && <h1 className="ws-route-name">{section}</h1>}
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
      </SubsectionSlot.Provider>
    </div>
  );
}
function MonitorRedirect() {
  const { search } = useLocation();
  return <Navigate to={{ pathname: "/monitor", search }} replace />;
}

function WorkspaceRoutes({ services }: { services: Services }) {
  const routes: RouteObject[] = [
    { path: "/", element: <MonitorRedirect /> },
    ...["/monitor", "/cameras", "/maps", "/audit", "/access"].map((path) => ({
      path,
      element: <Workspace services={services} />,
    })),
  ];
  return useRoutes(routes);
}

export function WorkspaceRouter({
  services,
  initial,
}: {
  services: Services;
  initial?: string;
}) {
  const content = <WorkspaceRoutes services={services} />;
  return initial ? (
    <MemoryRouter initialEntries={[initial]}>{content}</MemoryRouter>
  ) : (
    <BrowserRouter>{content}</BrowserRouter>
  );
}
