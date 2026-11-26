import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useSyncExternalStore,
  type ReactNode,
} from "react";
import type { MessageKey } from "./ru";
import { messages } from "./messages";

export type Language = "ru" | "en";
export const languages = ["ru", "en"] as const satisfies readonly Language[];
export type { MessageKey };

const loaders = {
  ru: async () => (await import("./ru")).ru,
  en: async () => (await import("./en")).en,
};
async function loadLanguage(language: Language) {
  Object.assign(messages[language], await loaders[language]());
}
const locales: Record<Language, string> = { ru: "ru-RU", en: "en-GB" };
const storageKey = "bacuri.vms.language";

function saved(): Language | null {
  try {
    const value = localStorage.getItem(storageKey);
    if (value === "ru" || value === "en") return value;
  } catch {
    // Browser storage may be unavailable.
  }
  return null;
}

// The saved choice, otherwise the first supported language of the browser, otherwise English.
export function preferredLanguage(): Language {
  const choice = saved();
  if (choice) return choice;
  for (const tag of navigator.languages ?? [navigator.language]) {
    const primary = tag.toLowerCase().split("-")[0];
    if (primary === "ru" || primary === "en") return primary;
  }
  return "en";
}

let current: Language = preferredLanguage();
await loadLanguage(current);
let requested = current;
const listeners = new Set<() => void>();

function subscribe(listener: () => void) {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

export function currentLanguage(): Language {
  return current;
}

export async function selectLanguage(language: Language) {
  requested = language;
  if (language === current) return;
  await loadLanguage(language);
  if (requested !== language) return;
  current = language;
  try {
    localStorage.setItem(storageKey, language);
  } catch {
    // The selected language still applies in this tab.
  }
  listeners.forEach((listener) => listener());
}

// BCP 47 locale of the current language for Intl formatters.
export function locale(language: Language = current): string {
  return locales[language];
}

export type Params = Record<string, string | number>;

// Translates a key in the current language. A plural message selects its form by the count parameter.
export function t(key: MessageKey, params: Params = {}): string {
  return translate(current, key, params);
}

export function translate(
  language: Language,
  key: MessageKey,
  params: Params = {},
): string {
  const message = messages[language][key];
  if (message === undefined)
    throw new Error(`Missing translation for ${language}: ${key}`);
  const text =
    typeof message === "string"
      ? message
      : (message[
          new Intl.PluralRules(locale(language)).select(
            Number(params.count ?? 0),
          ) as keyof typeof message
        ] ?? message.other);
  return text.replace(/\{(\w+)\}/g, (match, name: string) =>
    name in params ? String(params[name]) : match,
  );
}

const formats = new Map<string, Intl.DateTimeFormat>();
// A date and time formatter of the current language, cached per options.
export function dateTimeFormat(
  options: Intl.DateTimeFormatOptions,
): Intl.DateTimeFormat {
  const key = current + JSON.stringify(options);
  let format = formats.get(key);
  if (!format) {
    format = new Intl.DateTimeFormat(locale(), options);
    formats.set(key, format);
  }
  return format;
}

// Short weekday names of the current language from Monday to Sunday.
export function shortWeekdays(): string[] {
  const format = dateTimeFormat({ weekday: "short", timeZone: "UTC" });
  // 1 January 2024 is a Monday.
  return Array.from({ length: 7 }, (_, n) => {
    const name = format.format(Date.UTC(2024, 0, 1 + n));
    return name.charAt(0).toLocaleUpperCase(locale()) + name.slice(1);
  });
}

export function formatNumber(
  value: number,
  options?: Intl.NumberFormatOptions,
): string {
  return value.toLocaleString(locale(), options);
}

const LanguageContext = createContext<Language>(current);

// Components that translate text read the language through this provider, so they render again when
// the user switches it.
export function LanguageProvider({ children }: { children: ReactNode }) {
  const language = useSyncExternalStore(subscribe, currentLanguage);
  useEffect(() => {
    document.documentElement.lang = language;
  }, [language]);
  useEffect(() => {
    function sync(event: StorageEvent) {
      if (event.key === storageKey || event.key === null) {
        const choice = saved();
        if (choice) void selectLanguage(choice);
      }
    }
    window.addEventListener("storage", sync);
    return () => window.removeEventListener("storage", sync);
  }, []);
  return (
    <LanguageContext.Provider value={language}>
      {children}
    </LanguageContext.Provider>
  );
}

// The translation function of the current language for a component.
// The function is stable for one language, so effects that use it run again only when the language changes.
export function useT(): typeof t {
  const language = useContext(LanguageContext);
  return useCallback<typeof t>(
    (key, params) => translate(language, key, params),
    [language],
  );
}

export function useLanguage(): Language {
  return useContext(LanguageContext);
}
