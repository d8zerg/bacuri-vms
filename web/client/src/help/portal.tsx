import errorMessages from "../../../../build/contracts/help-messages.json";
import { helpVisuals, workstationSettings } from "../i18n/help-visuals";
import { relatedTopics } from "../i18n/help-related";
import { DisplaySwitches } from "../ui-kit/DisplaySwitches";
import { ToolbarDrawer } from "../ui-kit/ToolbarDrawer";
import { labels, languageNames, normalizeHelpText } from "../i18n/help";
import { useEffect, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import { articles, type Article } from "../i18n/help-articles";
import mark from "../../../../design/bacuri/brand/mark.svg";
import sprite from "../../../../design/bacuri/icons/sprite.svg";
import "./portal.css";

type Language = "en" | "ru";
type Section = Article["section"];
const sections: Section[] = ["operator", "administrator", "troubleshooting"];
const icons = {
  operator: "monitor-play",
  administrator: "settings-2",
  troubleshooting: "search",
};
const common = [
  "recording-missing",
  "connect-camera",
  "review-alarm",
  "investigate-export",
];
const languageKey = "bacuri.vms.help.language";
const themeKey = "bacuri.vms.help.theme";
function saved(key: string) {
  try {
    return localStorage.getItem(key);
  } catch {
    return null;
  }
}
function remember(key: string, value: string) {
  try {
    localStorage.setItem(key, value);
  } catch {
    /* The selection still applies in this tab. */
  }
}
function locale(value: string | null): Language | undefined {
  return value === "en" || value === "ru" ? value : undefined;
}
function address() {
  const params = new URLSearchParams(location.hash.slice(1));
  const language =
    locale(params.get("lang")) ??
    locale(saved(languageKey)) ??
    locale(saved("bacuri.vms.language")) ??
    "en";
  const theme =
    [params.get("theme"), saved(themeKey), saved("bacuri.vms.theme")].find(
      (value) => value === "dark" || value === "light" || value === "system",
    ) ?? "system";
  const id = params.get("article");
  const code = params.get("code");
  const article = id
    ? articles.find((a) => a.id === id)
    : code
      ? articles.find((a) => a.codes.includes(code))
      : undefined;
  const section = sections.find((s) => s === params.get("section"));
  return {
    language,
    theme,
    article,
    code: article && code && article.codes.includes(code) ? code : undefined,
    section,
    unknown: !!(id || code) && !article,
  };
}
function fragment(language: Language, theme: string, item?: Article | Section) {
  const params = new URLSearchParams({ lang: language, theme });
  if (typeof item === "string") params.set("section", item);
  else if (item) params.set("article", item.id);
  return "#" + params.toString();
}
const publicMessages: Readonly<Record<string, Record<Language, string>>> =
  errorMessages;
const byId = new Map(articles.map((article) => [article.id, article]));
const searchIndex = articles.map((article) => {
  const editions = [article.en, article.ru];
  const messages = article.codes.flatMap((code) =>
    Object.values(publicMessages[code] ?? {}).map(normalizeHelpText),
  );
  const title = normalizeHelpText(editions.map((copy) => copy.title).join(" "));
  const tags = normalizeHelpText(
    editions.flatMap((copy) => copy.keywords).join(" "),
  );
  const text = editions.flatMap((copy) => [
    copy.summary,
    copy.before,
    ...copy.steps,
    copy.result,
    ...(copy.diagnosis ?? []).flatMap(({ symptom, action }) => [
      symptom,
      action,
    ]),
  ]);
  const body = normalizeHelpText(
    [...text, ...article.codes, ...messages].join(" "),
  );
  return { article, title, tags, body, messages };
});
function findArticles(query: string) {
  const q = normalizeHelpText(query);
  const words = q.split(/[^\p{L}\p{N}_]+/u).filter(Boolean);
  if (!words.length) return [];
  return searchIndex
    .map(({ article, title, tags, body, messages }) => {
      let score = 0;
      for (const word of words) {
        if (title.includes(word)) score += 5;
        else if (tags.includes(word)) score += 3;
        else if (body.includes(word)) score += 1;
        else return { article, score: 0 };
      }
      if (article.codes.includes(q) || messages.includes(q)) score += 100;
      if (title.includes(q) || tags.includes(q)) score += 5;
      return { article, score };
    })
    .filter((hit) => hit.score > 0)
    .sort((a, b) => b.score - a.score)
    .map((hit) => hit.article);
}
function Icon({ name }: { name: string }) {
  return (
    <svg aria-hidden="true" width="21" height="21">
      <use href={sprite + "#i-" + name} />
    </svg>
  );
}
function Portal() {
  const [view, setView] = useState(address);
  const [query, setQuery] = useState("");
  const search = useRef<HTMLInputElement>(null);
  const heading = useRef<HTMLHeadingElement>(null);

  const { language, theme, article, section, unknown } = view;
  const t = labels[language];
  const visual = article && helpVisuals[article.id];
  const searching = !!query.trim();
  const title = searching
    ? t.results
    : (article?.[language].title ?? (section ? t[section] : t.help));
  useEffect(() => {
    function navigate() {
      const next = address();
      setView(next);
      setQuery("");
      requestAnimationFrame(() => {
        if (next.unknown) search.current?.focus();
        else heading.current?.focus();
      });
    }
    if (address().unknown) search.current?.focus();
    window.addEventListener("hashchange", navigate);
    return () => window.removeEventListener("hashchange", navigate);
  }, []);
  useEffect(() => {
    document.documentElement.lang = language;
    document.title = title + " | Bacuri VMS";
  }, [language, title]);
  useEffect(() => {
    if (theme === "system")
      delete document.documentElement.dataset.workspaceTheme;
    else document.documentElement.dataset.workspaceTheme = theme;
  }, [theme]);
  function changeLanguage(value: Language) {
    if (value === language) return;
    remember(languageKey, value);
    replacePreferences(value, theme);
  }
  function replacePreferences(lang: Language, nextTheme: string) {
    const hash = view.code
      ? "#" +
        new URLSearchParams({
          lang,
          theme: nextTheme,
          code: view.code,
        }).toString()
      : fragment(lang, nextTheme, article ?? section);
    history.replaceState(null, "", hash);
    setView(address());
  }
  function resetSearch() {
    setQuery("");
    search.current?.focus();
  }
  function articleList(items: Article[]) {
    return (
      <ul className="help-list">
        {items.map((a) => (
          <li key={a.id}>
            <a href={fragment(language, theme, a)} onClick={() => setQuery("")}>
              <span className="help-item-title">{a[language].title}</span>
              <span className="help-summary">{a[language].summary}</span>
            </a>
          </li>
        ))}
      </ul>
    );
  }
  const matches = searching ? findArticles(query) : [];
  const activeSection = article?.section ?? section;
  return (
    <>
      <a
        className="help-skip"
        href="#help-content"
        onClick={(event) => {
          event.preventDefault();
          heading.current?.focus();
        }}
      >
        {t.skip}
      </a>
      <header className="help-bar">
        <div className="help-wrap help-bar-inner">
          <a
            className="help-brand"
            href={fragment(language, theme)}
            onClick={() => setQuery("")}
          >
            <img src={mark} alt="" />
            Bacuri VMS
          </a>
          <span className="help-product">{t.help}</span>
          <div className="help-preferences">
            <ToolbarDrawer title={t.settings} closeLabel={t.closeSettings}>
              <DisplaySwitches
                language={language}
                theme={theme}
                labels={{ ...t, ...languageNames }}
                setLanguage={changeLanguage}
                setTheme={(value) => {
                  remember(themeKey, value);
                  replacePreferences(language, value);
                }}
              />
            </ToolbarDrawer>
          </div>
        </div>
      </header>
      <main className="help-wrap" id="help-content">
        {(activeSection || searching || unknown) && (
          <nav className="help-crumb" aria-label={t.breadcrumb}>
            <a href={fragment(language, theme)} onClick={() => setQuery("")}>
              {t.home}
            </a>
            {activeSection && (
              <>
                <span aria-hidden="true">/</span>
                <a href={fragment(language, theme, activeSection)}>
                  {t[activeSection]}
                </a>
              </>
            )}
          </nav>
        )}
        <div className="help-intro">
          <h1 ref={heading} tabIndex={-1}>
            {title}
          </h1>
          <div role="search" className="help-search">
            <Icon name="search" />
            <input
              type="search"
              ref={search}
              aria-label={t.search}
              placeholder={t.placeholder}
              value={query}
              maxLength={200}
              onChange={(e) => setQuery(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === "Escape") resetSearch();
              }}
            />
            {query && (
              <button onClick={resetSearch} type="button" aria-label={t.clear}>
                ×
              </button>
            )}
          </div>
          {unknown && !searching && <p role="status">{t.unknown}</p>}
        </div>
        {searching ? (
          <section aria-label={t.results}>
            <p className="help-count" role="status">
              {t.results} · {matches.length}
            </p>
            {matches.length ? (
              articleList(matches)
            ) : (
              <div className="help-empty">
                <h2>{t.none}</h2>
                <p>{t.try}</p>
                <button type="button" onClick={resetSearch}>
                  {t.clear}
                </button>
              </div>
            )}
          </section>
        ) : article ? (
          <div className="help-reading">
            <article className="help-paper">
              <p className="help-lede">{article[language].summary}</p>
              {view.code && (
                <p className="help-selected-error">
                  {publicMessages[view.code]?.[language]}
                </p>
              )}
              <nav className="help-jumps" aria-label={t.contents}>
                {[
                  ["help-steps", t.steps],
                  ...(article[language].diagnosis?.length
                    ? [["help-diagnosis", t.diagnosis]]
                    : []),
                  ["help-result", t.result],
                ].map(([id, name]) => (
                  <button
                    key={id}
                    type="button"
                    onClick={() => document.getElementById(id!)?.focus()}
                  >
                    {name}
                  </button>
                ))}
              </nav>
              <div className="help-prerequisite">
                <h2>{t.before}</h2>
                <p>{article[language].before}</p>
              </div>
              {visual && (
                <figure className="help-visual">
                  <img src={visual.src} alt={visual[language].caption} />
                  <figcaption>
                    <p>{visual[language].caption}</p>
                    <ol>
                      {visual[language].keys.map((key) => (
                        <li key={key}>{key}</li>
                      ))}
                    </ol>
                  </figcaption>
                </figure>
              )}
              {article.id === "start-monitoring" && (
                <figure className="help-visual">
                  <img
                    src={workstationSettings[language].src}
                    alt={workstationSettings[language].alt}
                    loading="lazy"
                  />
                  <figcaption>
                    {workstationSettings[language].caption}
                  </figcaption>
                </figure>
              )}
              <h2 id="help-steps" tabIndex={-1}>
                {t.steps}
              </h2>
              <ol className="help-steps">
                {article[language].steps.map((step) => (
                  <li key={step}>{step}</li>
                ))}
              </ol>
              {article[language].diagnosis && (
                <section>
                  <h2 id="help-diagnosis" tabIndex={-1}>
                    {t.diagnosis}
                  </h2>
                  <dl className="help-diagnosis">
                    {article[language].diagnosis.map(({ symptom, action }) => (
                      <div key={symptom}>
                        <dt>{symptom}</dt>
                        <dd>{action}</dd>
                      </div>
                    ))}
                  </dl>
                </section>
              )}
              <div className="help-outcome">
                <h2 id="help-result" tabIndex={-1}>
                  {t.result}
                </h2>
                <p>{article[language].result}</p>
              </div>
              {article.codes.length > 0 && (
                <div>
                  <h2>{t.codes}</h2>
                  <dl className="help-error-list">
                    {article.codes.map((code) => (
                      <div
                        key={code}
                        data-current={code === view.code || undefined}
                      >
                        <dt>
                          <code>{code}</code>
                        </dt>
                        <dd>{publicMessages[code]?.[language]}</dd>
                      </div>
                    ))}
                  </dl>
                </div>
              )}
            </article>
            <aside>
              <h2>{t.related}</h2>
              {articleList(
                (relatedTopics[article.id] ?? [])
                  .map((id) => byId.get(id))
                  .filter((item): item is Article => !!item),
              )}
            </aside>
          </div>
        ) : section ? (
          <>
            <nav className="help-tabs" aria-label={t.topics}>
              {sections.map((s) => (
                <a
                  key={s}
                  aria-current={s === section ? "page" : undefined}
                  href={fragment(language, theme, s)}
                >
                  {t[s]}
                </a>
              ))}
            </nav>
            {articleList(articles.filter((a) => a.section === section))}
          </>
        ) : (
          <>
            <nav aria-label={t.topics}>
              <h2>{t.topics}</h2>
              <div className="help-topics">
                {sections.map((s, i) => (
                  <a
                    key={s}
                    className="help-topic"
                    href={fragment(language, theme, s)}
                  >
                    <span className="help-icon">
                      <Icon name={icons[s]} />
                    </span>
                    <strong>{t[s]}</strong>
                    <span className="help-summary">{t.scopes[i]}</span>
                  </a>
                ))}
              </div>
            </nav>
            <section>
              <h2>{t.common}</h2>
              {articleList(
                common.flatMap((id) => articles.filter((a) => a.id === id)),
              )}
            </section>
          </>
        )}
      </main>
      <footer className="help-wrap">
        Bacuri VMS <span>·</span> {t.version}{" "}
        {
          document.querySelector<HTMLMetaElement>('meta[name="bacuri-version"]')
            ?.content
        }
      </footer>
    </>
  );
}
createRoot(document.getElementById("help")!).render(<Portal />);
