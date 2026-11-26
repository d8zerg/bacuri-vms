const storage = "bacuri.preview.language";
const russian = window.bacuriRussian || {};
let language = (() => {
  const requested = new URLSearchParams(location.search).get("lang");
  if (requested === "en" || requested === "ru") return requested;
  try {
    const saved = localStorage.getItem(storage);
    if (saved === "en" || saved === "ru") return saved;
  } catch {
    // Storage is unavailable for local files in some browsers.
  }
  return (navigator.language || "").toLowerCase().startsWith("ru") ? "ru" : "en";
})();

const text = (value) =>
  language === "ru" && Object.hasOwn(russian, value) ? russian[value] : value;

// The page is written in English. Each text node and label keeps its English source,
// so switching back and forth does not depend on the shown text.
const sources = new WeakMap();
const labelled = ["aria-label", "title", "alt"];
function localize() {
  document.documentElement.lang = language;
  const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
  while (walker.nextNode()) {
    const node = walker.currentNode;
    if (!sources.has(node)) sources.set(node, node.nodeValue);
    const source = sources.get(node);
    const trimmed = source.trim();
    node.nodeValue = trimmed ? source.replace(trimmed, text(trimmed)) : source;
  }
  for (const element of document.body.querySelectorAll("*")) {
    for (const name of labelled) {
      const key = `data-source-${name}`;
      if (!element.hasAttribute(name) && !element.hasAttribute(key)) continue;
      if (!element.hasAttribute(key)) element.setAttribute(key, element.getAttribute(name));
      element.setAttribute(name, text(element.getAttribute(key)));
    }
  }
  if (!document.documentElement.dataset.sourceTitle)
    document.documentElement.dataset.sourceTitle = document.title;
  document.title = text(document.documentElement.dataset.sourceTitle);
  for (const button of document.querySelectorAll("[data-language]"))
    button.setAttribute("aria-pressed", String(button.dataset.language === language));
}

for (const button of document.querySelectorAll("[data-language]")) {
  button.addEventListener("click", () => {
    language = button.dataset.language;
    try {
      localStorage.setItem(storage, language);
    } catch {
      // The choice is kept for this page only.
    }
    localize();
  });
}

const screens = document.querySelectorAll(".screen");
const modes = document.querySelectorAll("[data-mode]");
for (const button of modes) {
  button.addEventListener("click", () => {
    const mode = button.dataset.mode;
    for (const option of modes)
      option.setAttribute("aria-pressed", String(option === button));
    for (const screen of screens)
      screen.hidden = mode !== "both" && screen.dataset.theme !== mode;
    document
      .querySelector(".compare")
      .classList.toggle("single", mode !== "both");
  });
}

function setText(element, value) {
  const node = element.firstChild;
  sources.set(node, value);
  node.nodeValue = text(value);
}

for (const screen of screens) {
  const button = screen.querySelector(".cta");
  button.addEventListener("click", () => {
    const archive = screen.dataset.view !== "archive";
    screen.dataset.view = archive ? "archive" : "live";
    setText(screen.querySelector(".kind"), archive ? "Archive" : "Live");
    screen
      .querySelector(".ol use")
      .setAttribute("href", archive ? "#i-archive" : "#i-radio");
    screen
      .querySelector(".track")
      .style.setProperty("--cursor", archive ? "55%" : "calc(100% - 2px)");
    screen.querySelector(".or").textContent = archive ? "14:23:18" : "14:26:00";
    setText(
      screen.querySelector(".context"),
      archive ? "Alarm frame · UTC+3" : "Archive · UTC+3",
    );
    setText(
      button.querySelector("span"),
      archive ? "Back to live" : "Open archive",
    );
    button
      .querySelector("use")
      .setAttribute("href", archive ? "#i-radio" : "#i-archive");
  });
}

localize();
