import { useId } from "react";
import icons from "../../../../design/bacuri/icons/sprite.svg";
import type { Language } from "../i18n";
import "./display-switches.css";

const themes = [
  ["system", "monitor"],
  ["dark", "moon"],
  ["light", "sun"],
] as const;
export type Theme = (typeof themes)[number][0];

export function DisplaySwitches({
  language,
  theme,
  labels,
  setLanguage,
  setTheme,
}: {
  language: Language;
  theme: Theme;
  labels: Record<Language | Theme | "language" | "theme", string>;
  setLanguage: (value: Language) => void;
  setTheme: (value: Theme) => void;
}) {
  const id = useId();
  return (
    <>
      <div className="bk-settings-group">
        <strong>{labels.language}</strong>
        <div
          className="bk-display-switch"
          role="radiogroup"
          aria-label={labels.language}
        >
          {(["ru", "en"] as const).map((value) => (
            <label key={value} title={labels[value]} lang={value}>
              <input
                type="radio"
                name={id + "-language"}
                value={value}
                aria-label={labels[value]}
                checked={value === language}
                onChange={() => setLanguage(value)}
              />
              <span aria-hidden="true">{value.toUpperCase()}</span>
            </label>
          ))}
        </div>
      </div>
      <div className="bk-settings-group">
        <strong>{labels.theme}</strong>
        <div
          className="bk-display-switch"
          role="radiogroup"
          aria-label={labels.theme}
        >
          {themes.map(([value, icon]) => (
            <label key={value} title={labels[value]}>
              <input
                type="radio"
                name={id + "-theme"}
                value={value}
                aria-label={labels[value]}
                checked={value === theme}
                onChange={() => setTheme(value)}
              />
              <svg aria-hidden="true">
                <use href={icons + "#i-" + icon} />
              </svg>
            </label>
          ))}
        </div>
      </div>
    </>
  );
}
