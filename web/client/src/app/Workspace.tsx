import { lazy, Suspense, useEffect, useState } from "react";
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
import { Button } from "../ui-kit/FormControls";
import icons from "../../../../design/bacuri/icons/sprite.svg";
import mark from "../../../../design/bacuri/brand/mark.svg";
import "./workspace.css";
const Audit = lazy(() =>
  import("./AlertWork").then((m) => ({ default: m.AlertAudit })),
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
      setNotice("Вход не выполнен. Проверьте данные и связь с площадкой.");
    } finally {
      setBusy(false);
    }
  }
  return (
    <main className="ws-login ws-panel ws-pad">
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
              name="enrollment"
              autoComplete="off"
              required
            />
          </label>
        )}
        <Button type="submit" variant="primary" disabled={busy}>
          Войти
        </Button>
      </form>
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
        <Audit services={services} site={site} points={points} />
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
  const [identity, setIdentity] = useState<Session | null | undefined>(
    undefined,
  );
  const [message, setMessage] = useState("");
  const [params, setParams] = useSearchParams();
  const location = useLocation();
  const cameras = location.pathname.replace(/\/$/, "") === "/cameras";
  useEffect(() => {
    const ctl = new AbortController();
    void services
      .session(ctl.signal)
      .then((user) => {
        if (!ctl.signal.aborted) setIdentity(user);
      })
      .catch(() => {
        if (!ctl.signal.aborted) setIdentity(null);
      });
    return () => ctl.abort();
  }, [services]);
  const [expired] = useState(() => () => {
    setMessage("Сеанс или права изменились. Войдите снова.");
    setIdentity(null);
  });
  if (identity === undefined)
    return (
      <main className="ws-pad" role="status">
        Загрузка сеанса…
      </main>
    );
  if (!identity)
    return (
      <Login services={services} onLogin={setIdentity} message={message} />
    );
  const site = params.has("site")
    ? identity.site_roles.find((s) => s.site_id === params.get("site"))
    : identity.site_roles[0];
  const context = new URLSearchParams(params);
  if (site) context.set("site", site.site_id);
  const query = context.size ? "?" + context.toString() : "";
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
        {identity.site_roles.length > 1 && (
          <label>
            Площадка{" "}
            <select
              value={site?.site_id}
              onChange={(e) => void setParams({ site: e.target.value })}
            >
              {identity.site_roles.map((s) => (
                <option key={s.site_id}>{s.site_id}</option>
              ))}
            </select>
          </label>
        )}
        <Button
          onClick={() => {
            void services
              .logout()
              .then(() => {
                setMessage("");
                setIdentity(null);
              })
              .catch(() => setMessage("Не удалось завершить сеанс"));
          }}
        >
          Выйти
        </Button>
      </header>
      <nav aria-label="Рабочее место">
        <NavLink
          to={"/monitor" + query}
          aria-current={location.pathname === "/monitor" ? "page" : false}
        >
          <svg aria-hidden="true">
            <use href={icons + "#i-monitor-play"} />
          </svg>
          Мониторинг
        </NavLink>
        <NavLink to={"/maps" + query}>
          <svg aria-hidden="true">
            <use href={icons + "#i-map"} />
          </svg>
          Карты и планы
        </NavLink>
        {site?.role === "administrator" && (
          <NavLink to={"/cameras" + query}>
            <svg aria-hidden="true">
              <use href={icons + "#i-cctv"} />
            </svg>
            Камеры
          </NavLink>
        )}
      </nav>
      <main id="workspace" tabIndex={-1}>
        {message && <p role="status">{message}</p>}
        {site ? (
          <Site
            key={site.site_id}
            services={services}
            site={site.site_id}
            userId={identity.user_id}
            admin={site.role === "administrator"}
            cameras={cameras}
            maps={location.pathname === "/maps"}
            audit={location.pathname === "/audit"}
            onExpired={expired}
          />
        ) : (
          <p>Нет доступных площадок</p>
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
      ...["/monitor", "/cameras", "/maps", "/audit"].map((path) => ({
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
