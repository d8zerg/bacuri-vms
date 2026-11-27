import { useEffect, useLayoutEffect, useId, useState } from "react";
import { selectLanguage, useLanguage, useT } from "../i18n";
import { SettingsIcon } from "../ui-kit/ToolbarDrawer";
import { DisplaySwitches, type Theme } from "../ui-kit/DisplaySwitches";

const storageKey = "bacuri.vms.theme";

function savedTheme(): Theme {
  try {
    const value = localStorage.getItem(storageKey);
    if (value === "light" || value === "dark") return value;
  } catch {
    // Browser storage may be unavailable.
  }
  return "system";
}

export function useWorkspaceTheme() {
  const [theme, setTheme] = useState(savedTheme);
  useLayoutEffect(() => {
    const root = document.documentElement;
    if (theme !== "system") root.dataset.workspaceTheme = theme;
    else delete root.dataset.workspaceTheme;
    return () => {
      delete root.dataset.workspaceTheme;
    };
  }, [theme]);
  useEffect(() => {
    function sync(event: StorageEvent) {
      if (event.key === storageKey || event.key === null)
        setTheme(savedTheme());
    }
    window.addEventListener("storage", sync);
    return () => window.removeEventListener("storage", sync);
  }, []);
  function selectTheme(value: Theme) {
    setTheme(value);
    try {
      localStorage.setItem(storageKey, value);
    } catch {
      // The selected theme still applies in this tab.
    }
  }
  return { theme, selectTheme };
}

export function WorkspacePreferences({
  theme,
  selectTheme,
}: ReturnType<typeof useWorkspaceTheme>) {
  const t = useT();
  const language = useLanguage();
  const [pending, setPending] = useState<typeof language | null>(null);
  function choose(value: typeof language) {
    setPending(value);
    void selectLanguage(value).finally(() => {
      setPending((requested) => (value === requested ? null : requested));
    });
  }
  return (
    <DisplaySwitches
      language={pending ?? language}
      theme={theme}
      setLanguage={choose}
      setTheme={selectTheme}
      labels={{
        language: t("language.label"),
        theme: t("theme.label"),
        ru: t("language.ru"),
        en: t("language.en"),
        system: t("theme.system"),
        dark: t("theme.dark"),
        light: t("theme.light"),
      }}
    />
  );
}

export function SidebarSettings(props: ReturnType<typeof useWorkspaceTheme>) {
  const t = useT();
  const id = useId();
  return (
    <>
      <button
        type="button"
        className="ws-sidebar-action"
        popoverTarget={id}
        aria-haspopup="dialog"
        aria-label={t("nav.settings")}
        title={t("nav.settings")}
      >
        <SettingsIcon />
        <span>{t("nav.settings")}</span>
      </button>
      <div
        id={id}
        className="ws-preferences-popover"
        role="dialog"
        aria-label={t("nav.settings")}
        popover="auto"
      >
        <WorkspacePreferences {...props} />
      </div>
    </>
  );
}
