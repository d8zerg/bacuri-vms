import { lazy, Suspense, useEffect, useMemo, useState } from "react";
import {
  NavLink,
  BrowserRouter,
  MemoryRouter,
  Navigate,
  useRoutes,
  useLocation,
  useNavigate,
  useSearchParams,
  type RouteObject,
} from "react-router";
import type { Feed, Point, Services, Session } from "../contracts/workspace";
import { HttpFailure } from "../adapters/api";
import { takeDesktopSignIn } from "../workflows/desktopSignIn";
import { ToolbarDrawer } from "../ui-kit/ToolbarDrawer";
import { WorkspaceAccount } from "./WorkspaceAccount";
import {
  HelpLink,
  HelpTheme,
  ScreenHelp,
  screenHelpTopic,
  type HelpTopic,
} from "./HelpLink";
import { SubsectionSlot, useSidebarLayout } from "./SubsectionMenu";
import { cameraSetup } from "./cameraSetup";
import { SignInHeader } from "./SignInHeader";
import "../ui-kit/media-panels.css";
import {
  WorkspacePreferences,
  SidebarSettings,
  useWorkspaceTheme,
} from "./WorkspaceTheme";
import { LanguageProvider, useT, type MessageKey } from "../i18n";
import { Button } from "../ui-kit/FormControls";
import icons from "../../../../design/bacuri/icons/sprite.svg";
import mark from "../../../../design/bacuri/brand/mark.svg";
import "./workspace.css";
const Login = lazy(() => import("./Login"));
const Installer = lazy(() =>
  import("./installer").then((m) => ({ default: m.Installer })),
);
const Audit = lazy(() =>
  import("./AlertWork").then((m) => ({ default: m.AlertAudit })),
);
const Access = lazy(() => import("./Access"));
const DesktopSignIn = lazy(() =>
  import("./DesktopSignIn").then((m) => ({ default: m.DesktopSignIn })),
);
const Monitor = lazy(() =>
  import("./monitor").then((m) => ({ default: m.Monitor })),
);
const Investigations = lazy(() => import("./Investigations"));
const Maps = lazy(() => import("./Maps"));
const waiting: Feed = { items: [], status: "loading" };
function Site({
  services,
  site,
  admin,
  userId,
  cameras,
  investigations,
  maps,
  audit,
  onExpired,
}: {
  services: Services;
  site: string;
  admin: boolean;
  userId: string;
  cameras: boolean;
  investigations: boolean;
  maps: boolean;
  audit: boolean;
  onExpired: () => void;
}) {
  const t = useT();
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
  const notice = failed ? t("site.reconnecting") : t("site.loading");
  if (cameras && admin)
    return (
      <Suspense fallback={<p role="status">{t("loading.generic")}</p>}>
        <Installer
          services={services}
          site={site}
          points={points}
          connectionNotice={notice}
        />
      </Suspense>
    );
  if (!points) return <p role="status">{notice}</p>;
  if (investigations)
    return (
      <Suspense fallback={<p role="status">{t("loading.materials")}</p>}>
        <Investigations
          key={points[0]?.access_revision ?? ""}
          services={services}
          site={site}
          points={points}
          user={userId}
        />
      </Suspense>
    );
  if (audit)
    return (
      <Suspense fallback={<p role="status">{t("loading.journal")}</p>}>
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
      <Suspense fallback={<p role="status">{t("loading.maps")}</p>}>
        <Maps
          services={services}
          site={site}
          admin={admin}
          points={points}
          feed={feed}
        />
      </Suspense>
    );
  if (cameras) return <p role="status">{t("access.cameraDenied")}</p>;
  return (
    <Suspense fallback={<p role="status">{t("loading.monitoring")}</p>}>
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
  {
    path: "/monitor",
    label: "nav.monitoring",
    short: "nav.monitoring.short",
    icon: "monitor-play",
  },
  {
    path: "/investigations",
    label: "nav.investigations",
    short: "nav.investigations.short",
    icon: "search",
  },
  {
    path: "/audit",
    label: "nav.journal",
    short: "nav.journal.short",
    icon: "clipboard-list",
  },
  { path: "/maps", label: "nav.maps", short: "nav.maps.short", icon: "map" },
  { path: "/access", label: "nav.access", icon: "shield-check", admin: true },
  { path: "/cameras", label: "nav.cameras", icon: "cctv", admin: true },
] satisfies {
  path: string;
  label: MessageKey;
  short?: MessageKey;
  icon: string;
  admin?: boolean;
}[];
function Workspace({
  services,
  theme,
}: {
  services: Services;
  theme: ReturnType<typeof useWorkspaceTheme>;
}) {
  const t = useT();
  const sidebar = useSidebarLayout();
  const [subsectionHelp, setSubsectionHelp] = useState<HelpTopic>();
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
  const toggleLabel = collapsed ? t("nav.expand") : t("nav.collapse");
  const [identity, setIdentity] = useState<Session | null | undefined>(
    undefined,
  );
  const [message, setMessage] = useState<MessageKey | null>(null);
  const [sessionFailed, setSessionFailed] = useState(false);
  const [sessionAttempt, setSessionAttempt] = useState(0);
  useEffect(() => {
    const channel = new BroadcastChannel("bacuri-session");
    channel.onmessage = () => setSessionAttempt((n) => n + 1);
    return () => channel.close();
  }, []);
  const [params, setParams] = useSearchParams();
  const location = useLocation();
  const navigate = useNavigate();
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
    setMessage("session.expired");
    setIdentity(null);
  });
  // After a corporate sign-in started from a desktop sign-in, return to its confirmation.
  useEffect(() => {
    if (!identity) return;
    const pending = takeDesktopSignIn();
    if (pending) void navigate("/desktop-sign-in" + pending, { replace: true });
  }, [identity, navigate]);
  const section =
    {
      "/desktop-sign-in": t("section.signIn"),
      "/maps": t("section.maps"),
      "/investigations": t("section.investigations"),
      "/audit": t("section.journal"),
      "/access": t("section.access"),
      "/cameras": t(
        `section.cameras.${cameraSetup(params.get("setup")) ?? "catalog"}`,
      ),
    }[path] ?? t("section.monitoring");
  useEffect(() => {
    document.title =
      (identity
        ? section
        : identity === null
          ? t("section.signIn")
          : t("loading.generic")) + " | Bacuri VMS";
  }, [section, identity, t]);
  if (sessionFailed)
    return (
      <main className="ws-login ws-panel">
        <SignInHeader article="connection-lost" />
        <h1>{t("site.unavailable")}</h1>
        <p role="status">{t("site.sessionCheckFailed")}</p>
        <Button
          variant="primary"
          onClick={() => {
            setSessionFailed(false);
            setSessionAttempt((n) => n + 1);
          }}
        >
          {t("site.retry")}
        </Button>
      </main>
    );
  if (identity === undefined)
    return (
      <main className="ws-startup" role="status">
        {t("loading.session")}
      </main>
    );
  if (!identity)
    return (
      <Suspense
        fallback={
          <main className="ws-startup" role="status">
            {t("loading.session")}
          </main>
        }
      >
        <Login
          services={services}
          onLogin={(user) => {
            setMessage(null);
            setIdentity(user);
          }}
          message={message}
        />
      </Suspense>
    );
  if (path === "/desktop-sign-in")
    return (
      <Suspense
        fallback={
          <main className="ws-startup" role="status">
            {t("loading.generic")}
          </main>
        }
      >
        <DesktopSignIn services={services} identity={identity} />
      </Suspense>
    );
  const site = params.has("site")
    ? identity.site_roles.find((s) => s.site_id === params.get("site"))
    : identity.site_roles[0];
  const context = new URLSearchParams(params);
  context.delete("setup");
  if (site) context.set("site", site.site_id);
  const query = context.size ? "?" + context.toString() : "";
  const helpTopic = site
    ? (subsectionHelp ?? screenHelpTopic(path, params))
    : "access-privacy";
  const auditQuery = new URLSearchParams(context);
  auditQuery.delete("alert_id");
  return (
    <div className="ws-shell" data-collapsed={collapsed || undefined}>
      <a className="ws-skip" href="#workspace">
        {t("nav.skip")}
      </a>
      <header className="ws-bar">
        <div className="ws-navigation">
          <span className="ws-brand">
            <img alt="Bacuri VMS" src={mark} />
            <span aria-hidden="true">Bacuri VMS</span>
          </span>
          <nav id="workspace-navigation" aria-label={t("nav.workspace")}>
            {sections
              .filter(
                (section) => !section.admin || site?.role === "administrator",
              )
              .map(({ path: sectionPath, label, short, icon }) => {
                const name = t(label);
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
                        {short ? (
                          <>
                            <span className="ws-nav-wide">{name}</span>
                            <span className="ws-nav-short">{t(short)}</span>
                          </>
                        ) : (
                          name
                        )}
                      </span>
                    </NavLink>
                    {sectionPath === path && (
                      <div className="ws-submenu-slot" ref={setSubmenuSlot} />
                    )}
                  </div>
                );
              })}
            {sidebar && (
              <div className="ws-sidebar-actions">
                <SidebarSettings {...theme} />
                <HelpLink sidebar article={helpTopic} />
              </div>
            )}
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
          {identity.site_roles.length > 1 && (
            <label className="ws-site-select">
              {t("site.label")}{" "}
              <select
                value={site?.site_id ?? ""}
                onChange={(e) => void setParams({ site: e.target.value })}
              >
                {!site && (
                  <option value="" disabled>
                    {t("site.choose")}
                  </option>
                )}
                {identity.site_roles.map((s) => (
                  <option key={s.site_id}>{s.site_id}</option>
                ))}
              </select>
            </label>
          )}

          <div className="ws-session-actions">
            <WorkspaceAccount
              username={identity.username}
              helpTopic={helpTopic}
              logout={() => {
                void services
                  .logout()
                  .then(() => {
                    setMessage(null);
                    setIdentity(null);
                    const channel = new BroadcastChannel("bacuri-session");
                    channel.postMessage("logout");
                    channel.close();
                  })
                  .catch(() => setMessage("session.logoutFailed"));
              }}
            >
              {!sidebar && (
                <>
                  <div className="ws-header-help">
                    <HelpLink article={helpTopic} iconOnly />
                  </div>
                  <ToolbarDrawer
                    title={t("nav.settings")}
                    closeLabel={t("nav.closeSettings")}
                  >
                    <WorkspacePreferences {...theme} />
                  </ToolbarDrawer>
                </>
              )}
            </WorkspaceAccount>
          </div>
        </div>
      </header>
      <ScreenHelp.Provider value={setSubsectionHelp}>
        <SubsectionSlot.Provider value={submenuSlot}>
          <main id="workspace" tabIndex={-1}>
            {site && <h1 className="ws-route-name">{section}</h1>}
            {message && <p role="status">{t(message)}</p>}
            {site && path === "/access" ? (
              site.role === "administrator" ? (
                <Suspense fallback={<p role="status">{t("loading.access")}</p>}>
                  <Access
                    key={site.site_id}
                    site={site.site_id}
                    session={identity}
                    expire={expired}
                  />
                </Suspense>
              ) : (
                <p role="status">{t("access.denied")}</p>
              )
            ) : site ? (
              <Site
                key={site.site_id}
                services={services}
                site={site.site_id}
                userId={identity.user_id}
                admin={site.role === "administrator"}
                cameras={cameras}
                investigations={path === "/investigations"}
                maps={path === "/maps"}
                audit={path === "/audit"}
                onExpired={expired}
              />
            ) : (
              <div className="ws-panel ws-pad">
                <h1>
                  {identity.site_roles.length
                    ? t("site.unavailable")
                    : t("site.none")}
                </h1>
                {identity.site_roles[0] && (
                  <Button
                    onClick={() =>
                      void setParams({ site: identity.site_roles[0]!.site_id })
                    }
                  >
                    {t("site.openAvailable")}
                  </Button>
                )}
              </div>
            )}
          </main>
        </SubsectionSlot.Provider>
      </ScreenHelp.Provider>
    </div>
  );
}
function MonitorRedirect() {
  const { search } = useLocation();
  return <Navigate to={{ pathname: "/monitor", search }} replace />;
}

function WorkspaceRoutes({ services }: { services: Services }) {
  const theme = useWorkspaceTheme();
  const routes: RouteObject[] = [
    { path: "/", element: <MonitorRedirect /> },
    ...[
      "/monitor",
      "/cameras",
      "/maps",
      "/audit",
      "/access",
      "/investigations",
      "/desktop-sign-in",
    ].map((path) => ({
      path,
      element: <Workspace services={services} theme={theme} />,
    })),
  ];
  const page = useRoutes(routes);
  return <HelpTheme.Provider value={theme.theme}>{page}</HelpTheme.Provider>;
}

export function WorkspaceRouter({
  services,
  initial,
}: {
  services: Services;
  initial?: string;
}) {
  const content = <WorkspaceRoutes services={services} />;
  return (
    <LanguageProvider>
      {initial ? (
        <MemoryRouter initialEntries={[initial]}>{content}</MemoryRouter>
      ) : (
        <BrowserRouter>{content}</BrowserRouter>
      )}
    </LanguageProvider>
  );
}
