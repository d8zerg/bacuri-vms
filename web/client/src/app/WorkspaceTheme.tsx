import { useEffect, useLayoutEffect, useState } from "react";
import icons from "../../../../design/bacuri/icons/sprite.svg";

const choices = [
  { value: "system", label: "Системная тема", icon: "monitor" },
  { value: "dark", label: "Тёмная тема", icon: "moon" },
  { value: "light", label: "Светлая тема", icon: "sun" },
] as const;
type Theme = (typeof choices)[number]["value"];
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
      if (value === "system") localStorage.removeItem(storageKey);
      else localStorage.setItem(storageKey, value);
    } catch {
      // The selected theme still applies in this tab.
    }
  }
  return { theme, selectTheme };
}

export function ThemeSwitch({
  theme,
  selectTheme,
}: ReturnType<typeof useWorkspaceTheme>) {
  return (
    <div
      className="ws-theme-switch"
      role="radiogroup"
      aria-label="Тема оформления"
    >
      {choices.map(({ value, label, icon }) => (
        <label key={value} title={label}>
          <input
            type="radio"
            name="workspace-theme"
            value={value}
            aria-label={label}
            checked={value === theme}
            onChange={() => selectTheme(value)}
          />
          <svg aria-hidden="true">
            <use href={icons + "#i-" + icon} />
          </svg>
        </label>
      ))}
    </div>
  );
}
