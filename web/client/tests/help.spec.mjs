import { randomUUID } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { createRequire } from "node:module";
import { pathToFileURL } from "node:url";
import { test as base, expect } from "playwright/test";
import { test as workspaceTest } from "./browser.mjs";
import { coverageReport } from "./coverage.mjs";

const origin = "https://help.test";
const client = "/results/client";
const require = createRequire(import.meta.url);
const test = base.extend({
  // Serve the release HTML and assets with the site's public-page restrictions.
  page: async ({ page }, use, info) => {
    const unexpected = [];
    const errors = [];
    const measure = info.project.name === "chromium-light";
    if (measure)
      await page.coverage.startJSCoverage({ resetOnNavigation: false });
    page.on("pageerror", (error) => errors.push(error.message));
    page.on("websocket", (socket) => unexpected.push(socket.url()));
    await page.context().route("**/*", async (route) => {
      const request = route.request();
      const url = new URL(request.url());
      if (request.method() === "GET" && url.origin === origin && !url.search) {
        if (url.pathname === "/help")
          return route.fulfill({
            path: client + "/help.html",
            headers: {
              "Content-Type": "text/html; charset=utf-8",
              "Content-Security-Policy":
                "default-src 'none'; script-src 'self'; style-src 'self'; font-src 'self'; img-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'",
              "Referrer-Policy": "no-referrer",
            },
          });
        if (/^\/assets\/[\w.-]+$/.test(url.pathname))
          return route.fulfill({ path: client + url.pathname });
      }
      unexpected.push(request.method() + " " + request.url());
      return route.abort();
    });
    await use(page);
    if (measure) {
      const scripts = (await page.coverage.stopJSCoverage()).filter((entry) =>
        entry.url.startsWith(origin + "/assets/"),
      );
      mkdirSync("/results/chrome", { recursive: true });
      writeFileSync(
        `/results/chrome/help-${randomUUID()}.json`,
        JSON.stringify(
          scripts.map(({ url, functions }) => ({ url, functions })),
        ),
      );
      const assets = scripts.flatMap((entry) => {
        const path = client + new URL(entry.url).pathname;
        if (!existsSync(path + ".map")) return [];
        return {
          ...entry,
          url: pathToFileURL(path).href,
          sourceMap: JSON.parse(readFileSync(path + ".map", "utf8")),
        };
      });
      expect(assets.length).toBeGreaterThan(0);
      await coverageReport().add(assets);
    }
    expect(errors).toEqual([]);
    expect(unexpected).toEqual([]);
    expect(await page.context().cookies()).toEqual([]);
  },
});

function articleLink(page, id) {
  return page.locator(`a[href*="article=${id}"]`).first();
}

async function checkA11y(page) {
  // DevTools evaluates axe in the test page without relaxing its script policy.
  await page.evaluate(
    readFileSync(require.resolve("axe-core/axe.min.js"), "utf8"),
  );
  const violations = await page.evaluate(async () => {
    const audit = await window.axe.run(document, {
      runOnly: ["wcag2a", "wcag2aa", "wcag21a", "wcag21aa", "wcag22aa"],
    });
    return audit.violations.map(({ id, nodes }) => ({
      id,
      targets: nodes.map(({ target }) => target),
    }));
  });
  expect(violations).toEqual([]);
}

test("Visitors find a recording fault by symptom and error code in both languages", async ({
  page,
}) => {
  await page.goto(origin + "/help#lang=en");
  await expect(page.locator("html")).toHaveAttribute("lang", "en");
  const search = page.getByRole("searchbox");
  await search.fill("live video but no recording");
  const result = articleLink(page, "recording-missing");
  await expect(result).toBeVisible();
  await search.fill("камера в сети, запись отсутствует");
  await expect(result).toContainText(
    "Live video works but the recording is missing",
  );
  await result.focus();
  await page.keyboard.press("Enter");
  await expect(page).toHaveURL(/article=recording-missing/);
  const articleTitle = page.getByRole("heading", { level: 1 });
  const english = await articleTitle.innerText();
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  await page.getByRole("radio", { name: "Русский" }).check();
  await page.keyboard.press("Escape");
  await expect(page.locator("html")).toHaveAttribute("lang", "ru");
  await expect(page).toHaveURL(/article=recording-missing/);
  await expect(articleTitle).not.toHaveText(english);
  await search.fill("камера в сети, запись отсутствует");
  await expect(result).toBeVisible();
  await search.fill("live video but no recording");
  await expect(result).toContainText("Камера в сети, запись отсутствует");
  await search.fill("camera_ptz_busy");
  await expect(articleLink(page, "ptz-busy")).toBeVisible();
  for (const [message, id] of [
    ["Another operator controls the camera.", "ptz-busy"],
    ["Камерой управляет другой оператор.", "ptz-busy"],
    ["The selected interval is a recording gap.", "recording-missing"],
    ["В выбранном интервале пропуск записи.", "recording-missing"],
  ]) {
    await search.fill(message);
    await expect(
      page.getByRole("main").getByRole("list").getByRole("link").first(),
    ).toHaveAttribute("href", new RegExp("article=" + id + "$"));
  }
  await search.fill("no-such-public-code");
  await expect(page.locator('a[href*="article=ptz-busy"]')).toHaveCount(0);
  await expect(search).toBeFocused();
});

test("Public article links open directly, keep the theme and fit a narrow display", async ({
  page,
}) => {
  await page.setViewportSize({ width: 320, height: 844 });
  await page.addInitScript(() => {
    if (!sessionStorage.getItem("seeded")) {
      localStorage.setItem("bacuri.vms.language", "ru");
      localStorage.setItem("bacuri.vms.theme", "dark");
      localStorage.setItem("bacuri.vms.help.language", "ru");
      localStorage.setItem("bacuri.vms.help.theme", "dark");
      sessionStorage.setItem("seeded", "1");
    }
  });
  await page.goto(
    origin + "/help#lang=en&theme=light&article=recording-missing",
  );
  const heading = page.getByRole("heading", { level: 1 });
  await expect(heading).toBeVisible();
  await expect(page.locator("html")).toHaveAttribute("lang", "en");
  await expect(page.locator("html")).toHaveAttribute(
    "data-workspace-theme",
    "light",
  );
  const english = await heading.innerText();
  const preferences = page.getByRole("button", {
    name: "Settings",
    exact: true,
  });
  await preferences.click();
  await expect(page.getByRole("dialog", { name: "Settings" })).toBeVisible();
  const theme = page.getByRole("radiogroup", { name: /theme/i });
  const dark = theme.getByRole("radio", { name: /dark/i });
  const light = theme.getByRole("radio", { name: /light/i });
  const language = page.getByRole("radiogroup", { name: /language/i });
  await expect(language.getByRole("radio", { name: "English" })).toBeChecked();
  await expect(language.getByRole("radio", { name: "Русский" })).toBeVisible();
  await expect(theme.getByRole("radio", { name: /system/i })).toBeVisible();
  await expect(light).toBeChecked();
  await light.press("ArrowLeft");
  await expect(dark).toBeChecked();
  await checkA11y(page);
  await expect(page).toHaveURL(/article=recording-missing/);
  await page.reload();
  await preferences.click();
  await expect(dark).toBeChecked();
  await page.keyboard.press("Escape");
  await expect(heading).toHaveText(english);
  await checkA11y(page);
  const images = page.locator("article img");
  await expect(images).toHaveCount(1);
  for (const image of await images.all()) {
    await expect(image).toHaveJSProperty("complete", true);
    expect(
      await image.evaluate((element) => element.naturalWidth),
    ).toBeGreaterThan(0);
  }
  expect(
    await page.evaluate(() => document.documentElement.scrollWidth),
  ).toBeLessThanOrEqual(320);
  await page.goto(origin + "/help#lang=ru&code=camera_ptz_busy");
  await expect(page.locator("html")).toHaveAttribute("lang", "ru");
  await expect(heading).not.toHaveText(english);
  const byCode = await heading.innerText();
  await page.goto(origin + "/help#lang=ru&article=ptz-busy");
  await expect(heading).toHaveText(byCode);
  await page.goto(origin + "/help#lang=en&code=unknown_code");
  await expect(page.getByRole("searchbox")).toBeVisible();
  await expect(page.getByRole("status")).toContainText("no matching article");
  await expect(heading).not.toHaveText(byCode);
});

test("Role navigation narrows the catalog and browser Back restores it", async ({
  page,
}) => {
  await page.goto(origin + "/help");
  await expect(
    page.getByRole("button", { name: "Settings", exact: true }),
  ).toBeVisible();
  await expect(page.locator("html")).toHaveAttribute("lang", "en");
  await expect(page.locator("html")).not.toHaveAttribute(
    "data-workspace-theme",
  );
  await checkA11y(page);
  await page.locator('nav a[href*="section=operator"]').first().click();
  await expect(articleLink(page, "start-monitoring")).toBeVisible();
  await expect(articleLink(page, "connect-camera")).toHaveCount(0);
  await page.locator('nav a[href*="section=administrator"]').first().click();
  await expect(articleLink(page, "connect-camera")).toBeVisible();
  await expect(articleLink(page, "start-monitoring")).toHaveCount(0);
  await articleLink(page, "connect-camera").click();
  await expect(page).toHaveURL(/article=connect-camera/);
  expect(
    Object.fromEntries(new URLSearchParams(new URL(page.url()).hash.slice(1))),
  ).toEqual({
    lang: "en",
    theme: "system",
    article: "connect-camera",
  });
  await page.goBack();
  await expect(page).toHaveURL(/section=administrator/);
  await expect(articleLink(page, "connect-camera")).toBeVisible();
});

test("A desktop sign-in error opens its public recovery guidance with the desktop preferences", async ({
  page,
}) => {
  for (const lang of ["en", "ru"]) {
    const theme = lang === "en" ? "light" : "dark";
    await page.goto(
      origin +
        "/help#" +
        new URLSearchParams({ lang, theme, code: "desktop_code_invalid" }),
    );
    const paper = page.getByRole("article");
    await expect(
      paper.locator("code").filter({ hasText: /^desktop_code_invalid$/ }),
    ).toBeVisible();
    const selected = paper.locator(".help-selected-error");
    await expect(selected).toContainText(
      lang === "en" ? /desktop application/i : /настольн/i,
    );
    await expect(page.locator("html")).toHaveAttribute("lang", lang);
    await expect(page.locator("html")).toHaveAttribute(
      "data-workspace-theme",
      theme,
    );
    const title = await page.getByRole("heading", { level: 1 }).innerText();
    await page.goto(
      origin +
        "/help#" +
        new URLSearchParams({ lang, theme, article: "sign-in" }),
    );
    await expect(page.getByRole("heading", { level: 1 })).toHaveText(title);
    await page.goto(
      origin +
        "/help#" +
        new URLSearchParams({ lang, theme, article: "connection-lost" }),
    );
    await expect(paper.locator(".help-diagnosis dt").first()).toBeVisible();
    await expect(page.locator("html")).toHaveAttribute(
      "data-workspace-theme",
      theme,
    );
  }
});

test("A recording gap opens illustrated recovery instructions and the related configuration tasks", async ({
  page,
}, info) => {
  for (const [lang, width] of [
    ["en", 1440],
    ["ru", 320],
  ]) {
    await page.setViewportSize({ width, height: 900 });
    await page.goto(
      origin +
        "/help#" +
        new URLSearchParams({ lang, theme: "dark", code: "recording_gap" }),
    );
    const article = page.getByRole("article");
    const before = lang === "en" ? "Before you start" : "Перед началом";
    const diagnosis =
      lang === "en" ? "If something goes wrong" : "Если возникла проблема";
    const alphabet = lang === "en" ? /[A-Za-z]/ : /[А-Яа-яЁё]/;
    await expect(
      article.getByRole("heading", { name: before, exact: true }),
    ).toBeVisible();
    await expect(
      article.getByRole("heading", { name: diagnosis, exact: true }),
    ).toBeVisible();
    const jumps = article.getByRole("navigation", {
      name: lang === "en" ? "In this guide" : "В этой статье",
    });
    const diagnose = jumps.getByRole("button", { name: diagnosis });
    await diagnose.focus();
    await page.keyboard.press("Enter");
    await expect(
      article.getByRole("heading", { name: diagnosis }),
    ).toBeFocused();
    await expect(page).toHaveURL(/code=recording_gap$/);
    const figure = article.locator("figure");
    await expect(figure.locator("figcaption p")).toContainText(alphabet);
    const entries = await figure.locator("figcaption ol li").allTextContents();
    expect(entries.length).toBeGreaterThan(1);
    for (const line of entries) expect(line).toMatch(alphabet);
    const image = figure.locator("img");
    expect(new URL(await image.getAttribute("src"), page.url()).origin).toBe(
      origin,
    );
    await expect
      .poll(() => image.evaluate((el) => el.naturalWidth))
      .toBeGreaterThan(0);
    const box = await image.boundingBox();
    expect(box.x).toBeGreaterThanOrEqual(0);
    expect(box.x + box.width).toBeLessThanOrEqual(width);
    expect(
      await page.evaluate(() => document.documentElement.scrollWidth),
    ).toBeLessThanOrEqual(width);
    if (info.project.name === "chromium-light") {
      const filename = "recording-gap-" + lang + "-" + width + ".png";
      const shot = info.outputPath(filename);
      await page.screenshot({
        path: shot,
        fullPage: true,
        animations: "disabled",
      });
      await info.attach(filename, { path: shot, contentType: "image/png" });
    }
    const related = page.getByRole("complementary");
    const policy = related.locator('a[href*="article=recording-policy"]');
    await expect(
      related.locator('a[href*="article=connect-camera"]'),
    ).toBeVisible();
    await policy.focus();
    await page.keyboard.press("Enter");
    await expect(page).toHaveURL(/article=recording-policy/);
    await expect(page.getByRole("heading", { level: 1 })).toBeFocused();
    expect(
      Object.fromEntries(
        new URLSearchParams(new URL(page.url()).hash.slice(1)),
      ),
    ).toEqual({ lang, theme: "dark", article: "recording-policy" });
  }
});

test("Every public error maps to a bilingual article and all illustrations load locally", async ({
  page,
}, info) => {
  test.slow();
  const publicCodes = JSON.parse(
    readFileSync("../../build/contracts/help-codes.json", "utf8"),
  );
  const messages = JSON.parse(
    readFileSync("../../build/contracts/help-messages.json", "utf8"),
  );
  expect(Object.keys(messages).sort()).toEqual(publicCodes.sort());
  const requiredTasks = [
    "start-monitoring",
    "camera-on-map",
    "connect-camera",
    "review-alarm",
    "recording-missing",
    "archive-playback",
    "compare-recordings",
    "export-fragment",
    "verify-export",
    "privacy-masks",
    "ptz-control",
    "recording-policy",
  ];
  const catalog = new Map();
  const heading = page.getByRole("heading", { level: 1 });
  for (const lang of ["en", "ru"]) {
    const ids = new Set();
    for (const section of ["operator", "administrator", "troubleshooting"]) {
      await page.goto(
        origin + "/help#" + new URLSearchParams({ lang, section }),
      );
      await page.locator("nav.help-tabs").waitFor();
      const links = await page
        .locator('main ul a[href*="article="]')
        .evaluateAll((nodes) =>
          nodes.map((node) => ({
            href: node.href,
            title: node.querySelector(".help-item-title").textContent,
          })),
        );
      expect(links.length).toBeGreaterThan(0);
      for (const { href, title } of links) {
        const url = new URL(href);
        expect(url.origin + url.pathname).toBe(origin + "/help");
        const id = new URLSearchParams(url.hash.slice(1)).get("article");
        expect(ids.has(id)).toBe(false);
        ids.add(id);
        await page.goto(href);
        await expect(heading).toHaveText(title);
        const paper = page.locator("article");
        const beforeHeading = paper.getByRole("heading", {
          name: lang === "en" ? "Before you start" : "Перед началом",
          exact: true,
        });
        const before = await beforeHeading
          .locator("xpath=following-sibling::*[1]")
          .innerText();
        const copy = await paper.evaluate((node) => ({
          title: document.querySelector("h1").textContent.trim(),
          summary: node.querySelector("p").textContent.trim(),
          steps: [...node.querySelectorAll(".help-steps > li")].map((step) =>
            step.textContent.trim(),
          ),
          result: node.querySelector(".help-outcome p").textContent.trim(),
          codes: [...node.querySelectorAll(".help-error-list code")].map(
            (el) => el.textContent,
          ),
          errorText: [...node.querySelectorAll(".help-error-list dd")].map(
            (el) => el.textContent,
          ),
          diagnosis: [...node.querySelectorAll(".help-diagnosis > div")].map(
            (branch) => [
              branch.querySelector("dt").textContent,
              branch.querySelector("dd").textContent,
            ],
          ),
          caption: node.querySelector("figcaption p")?.textContent.trim(),
          legend: [...node.querySelectorAll("figcaption ol li")].map((entry) =>
            entry.textContent.trim(),
          ),
        }));
        const related = await page
          .getByRole("complementary")
          .getByRole("link")
          .evaluateAll((links) => links.map((link) => link.href));
        const relatedIds = related.map((href) => {
          const params = new URLSearchParams(new URL(href).hash.slice(1));
          expect(params.get("lang")).toBe(lang);
          return params.get("article");
        });
        expect(relatedIds.length).toBeGreaterThan(0);
        expect(new Set(relatedIds).size).toBe(relatedIds.length);
        expect(relatedIds).not.toContain(id);
        const alphabet = lang === "en" ? /[A-Za-z]/ : /[А-Яа-яЁё]/;
        for (const text of [
          copy.title,
          copy.summary,
          before,
          copy.result,
          ...copy.steps,
        ])
          expect(text).toMatch(alphabet);
        expect(copy.steps.length).toBeGreaterThan(0);
        if (section === "troubleshooting")
          expect(copy.diagnosis.length).toBeGreaterThan(0);
        for (const [symptom, action] of copy.diagnosis) {
          expect(symptom).toMatch(alphabet);
          expect(action).toMatch(alphabet);
          expect(symptom).not.toBe(action);
        }
        expect(copy.errorText).toEqual(
          copy.codes.map((code) => messages[code]?.[lang]),
        );
        if (lang === "en")
          catalog.set(id, {
            codes: copy.codes,
            title: copy.title,
            caption: copy.caption,
            legend: copy.legend,
            related: relatedIds,
          });
        else {
          expect(copy.codes).toEqual(catalog.get(id)?.codes);
          expect(relatedIds).toEqual(catalog.get(id)?.related);
          expect(copy.title).not.toBe(catalog.get(id)?.title);
        }
        const pictures = paper.locator("figure img");

        expect(copy.caption).toMatch(alphabet);
        expect(copy.legend).toHaveLength(3);
        for (const line of copy.legend) expect(line).toMatch(alphabet);
        if (lang === "ru")
          expect(copy.legend).not.toEqual(catalog.get(id)?.legend);
        if (lang === "ru")
          expect(copy.caption).not.toBe(catalog.get(id)?.caption);
        const image = new URL(await pictures.getAttribute("src"), url);
        expect(image.origin).toBe(origin);
        expect(image.pathname).toMatch(/^\/assets\/[\w.-]+\.svg$/);
        const markers = await paper.evaluate(
          (_, svg) => {
            const drawing = new DOMParser().parseFromString(
              svg,
              "image/svg+xml",
            );
            return [...drawing.querySelectorAll("[data-key]")].map((marker) => [
              marker.getAttribute("data-key"),
              marker.querySelector("text")?.textContent,
            ]);
          },
          readFileSync(client + image.pathname, "utf8"),
        );
        expect(markers).toEqual(
          copy.legend.map((_, i) => [String(i + 1), String(i + 1)]),
        );
        await expect
          .poll(() => pictures.evaluate((image) => image.naturalWidth))
          .toBeGreaterThan(0);
      }
    }
    expect([...ids].sort()).toEqual([...catalog.keys()].sort());
  }
  for (const id of requiredTasks) expect(catalog.has(id)).toBe(true);
  for (const article of catalog.values())
    for (const id of article.related) expect(catalog.has(id)).toBe(true);
  const mappedCodes = [...catalog.values()].flatMap((article) => article.codes);
  expect(mappedCodes.sort()).toEqual(publicCodes.sort());
  for (const { codes, title } of catalog.values()) {
    for (const code of codes) {
      await page.goto(
        origin + "/help#" + new URLSearchParams({ lang: "en", code }),
      );
      await expect(heading).toHaveText(title);
    }
  }
  if (info.project.name === "chromium-light") {
    await page.goto(origin + "/help#lang=en");
    const search = page.getByRole("searchbox");
    for (const [id, { codes }] of catalog) {
      for (const code of codes) {
        for (const text of Object.values(messages[code])) {
          await search.fill(text);
          await expect(articleLink(page, id), code + " " + text).toBeVisible();
        }
      }
    }
  }
});

workspaceTest(
  "Login and workspace help links preserve the initiating screen and disclose only public identifiers",
  async ({ page }) => {
    const story =
      "/iframe.html?viewMode=story&globals=language:en&id=workspace--";
    const login = await page.goto(story + "desktop-sign-in-login");
    expect(login.ok()).toBe(true);
    const requests = [];
    await page.context().route("**/help", (route) => {
      requests.push(route.request().headers());
      return route.fulfill({
        contentType: "text/html",
        body: "<title>Help</title>",
      });
    });
    const help = page.getByRole("button", { name: "Help", exact: true });
    await expect(help).toHaveText("");
    const [loginHelp] = await Promise.all([
      page.context().waitForEvent("page"),
      help.click(),
    ]);
    await loginHelp.waitForLoadState();
    const loginLink = new URL(loginHelp.url());
    expect(await loginHelp.evaluate(() => window.opener === null)).toBe(true);
    await loginHelp.close();
    expect(loginLink.pathname).toBe("/help");
    expect(loginLink.search).toBe("");
    expect(
      Object.fromEntries(new URLSearchParams(loginLink.hash.slice(1))),
    ).toEqual({
      lang: "en",
      theme: "system",
      article: "sign-in",
    });
    await page.setViewportSize({ width: 1440, height: 900 });
    await page.goto(story + "plan");
    const prefs = page
      .locator(".ws-navigation")
      .getByRole("button", { name: "Settings", exact: true });
    await prefs.click();
    await page.getByRole("radio", { name: "Dark theme", exact: true }).check();
    await page.keyboard.press("Escape");
    const topic = page
      .locator(".ws-navigation")
      .getByRole("link", { name: "Help", exact: true });
    await expect(topic).toHaveAttribute("target", "_blank");
    await expect(
      page
        .getByRole("main")
        .getByRole("link", { name: "Help for this screen" }),
    ).toHaveCount(0);
    const source = page.url();
    const [popup] = await Promise.all([
      page.waitForEvent("popup"),
      topic.click(),
    ]);
    await popup.waitForLoadState();
    const destination = new URL(popup.url());
    expect(destination.origin).toBe(new URL(source).origin);
    expect(destination.pathname).toBe("/help");
    expect(destination.search).toBe("");
    expect(
      Object.fromEntries(new URLSearchParams(destination.hash.slice(1))),
    ).toEqual({
      lang: "en",
      theme: "dark",
      article: "camera-on-map",
    });
    expect(requests).toHaveLength(2);
    for (const headers of requests) expect(headers.referer).toBeUndefined();
    expect(await popup.evaluate(() => window.opener === null)).toBe(true);
    await popup.close();
    await expect(page).toHaveURL(source);
  },
);

workspaceTest.describe("Workspace account and help on a touch screen", () => {
  workspaceTest.use({ hasTouch: true });
  workspaceTest(
    "Narrow controls expose the account and Help on demand",
    async ({ page }) => {
      await page.setViewportSize({ width: 390, height: 844 });
      await page.goto(
        "/iframe.html?viewMode=story&globals=language:en&id=workspace--monitor",
      );
      const banner = page.getByRole("banner");
      const account = banner.getByRole("button", {
        name: "operator.demo",
        exact: true,
      });
      const settings = page.getByRole("button", {
        name: "Settings",
        exact: true,
      });
      const help = banner.getByRole("link", { name: "Help", exact: true });
      await expect(help).toBeHidden();
      await expect(
        banner
          .getByText("operator.demo", { exact: true })
          .filter({ visible: true }),
      ).toHaveCount(0);
      const accountBox = await account.boundingBox();
      const settingsBox = await settings.boundingBox();
      expect(accountBox.x + accountBox.width).toBeLessThanOrEqual(
        settingsBox.x,
      );
      await account.tap();
      const userDrawer = page.getByRole("dialog", {
        name: "operator.demo",
        exact: true,
      });
      await expect(userDrawer).toBeVisible();
      await expect(
        userDrawer.getByText("operator.demo", { exact: true }),
      ).toBeVisible();
      const userHelp = userDrawer.getByRole("link", {
        name: "Help",
        exact: true,
      });
      await expect(userHelp).toBeVisible();
      const publicPath = new URL(
        await userHelp.getAttribute("href"),
        page.url(),
      );
      expect(publicPath.pathname).toBe("/help");
      expect(publicPath.search).toBe("");
      expect(
        Object.fromEntries(new URLSearchParams(publicPath.hash.slice(1))),
      ).toEqual({
        lang: "en",
        theme: "system",
        article: "start-monitoring",
      });
      await expect(
        userDrawer.getByRole("button", { name: "Sign out", exact: true }),
      ).toBeVisible();
      await page.keyboard.press("Escape");
      await expect(userDrawer).toBeHidden();
      await expect(account).toBeFocused();
      await settings.tap();
      const drawer = page.getByRole("dialog", { name: "Settings" });
      await expect(drawer).toBeVisible();
      await expect(
        drawer.getByRole("link", { name: "Help", exact: true }),
      ).toHaveCount(0);
      await expect(
        drawer.getByRole("button", { name: "Sign out", exact: true }),
      ).toHaveCount(0);
      await expect(
        drawer.getByRole("radiogroup", { name: "Interface language" }),
      ).toBeVisible();
      await expect(
        drawer.getByRole("radiogroup", { name: "Color theme" }),
      ).toBeVisible();
      await page.keyboard.press("Escape");
      await expect(drawer).toBeHidden();
      await expect(settings).toBeFocused();
      await account.tap();
      await expect(userDrawer).toBeVisible();
      await page.setViewportSize({ width: 1440, height: 900 });
      await expect(userDrawer).toBeVisible();
      await page.keyboard.press("Escape");
      await expect(userDrawer).toBeHidden();
      await settings.click();
      await expect(drawer).toBeVisible();
    },
  );
});

workspaceTest(
  "Desktop utilities stay above Collapse in the sidebar and move to the header on a tablet",
  async ({ page }) => {
    await page.setViewportSize({ width: 1440, height: 900 });
    await page.goto(
      "/iframe.html?viewMode=story&globals=language:en&id=workspace--monitor",
    );
    const sidebar = page.locator(".ws-navigation");
    const settings = sidebar.getByRole("button", {
      name: "Settings",
      exact: true,
    });
    const help = sidebar.getByRole("link", { name: "Help", exact: true });
    const session = page.locator(".ws-session");
    const popover = page.getByRole("dialog", { name: "Settings", exact: true });
    const username = session
      .getByText("operator.demo", { exact: true })
      .filter({ visible: true });
    const logout = session.getByRole("button", {
      name: "Sign out",
      exact: true,
    });
    await expect(logout).toBeVisible();
    await expect(logout).toHaveText("");
    const nameBox = await username.boundingBox();
    const logoutBox = await logout.boundingBox();
    expect(logoutBox.x).toBeGreaterThanOrEqual(nameBox.x + nameBox.width);
    for (const label of ["Collapse menu", "Expand menu"]) {
      const toggle = sidebar.getByRole("button", { name: label });
      const [prefBox, helpBox, toggleBox] = await Promise.all(
        [settings, help, toggle].map((el) => el.boundingBox()),
      );
      expect(prefBox.y + prefBox.height).toBeLessThanOrEqual(helpBox.y);
      expect(helpBox.y + helpBox.height).toBeLessThanOrEqual(toggleBox.y);
      await settings.press("Enter");
      await expect(popover).toBeVisible();
      await expect(popover).not.toHaveAttribute("aria-modal", "true");
      expect(await popover.evaluate((el) => el.matches(":modal"))).toBe(false);
      await expect(
        popover.getByRole("radiogroup", { name: "Interface language" }),
      ).toBeVisible();
      await expect(
        popover.getByRole("radiogroup", { name: "Color theme" }),
      ).toBeVisible();
      if (label === "Collapse menu") {
        await page.keyboard.press("Escape");
        await expect(settings).toBeFocused();
      } else {
        await username.click();
      }
      await expect(popover).toBeHidden();
      await toggle.click();
    }
    await page.setViewportSize({ width: 768, height: 900 });
    await expect(help).toBeHidden();
    await expect(settings).toBeHidden();
    const tabletSettings = session.getByRole("button", {
      name: "Settings",
      exact: true,
    });
    const tabletHelp = session.getByRole("link", { name: "Help", exact: true });
    const boxes = await Promise.all(
      [tabletHelp, tabletSettings, username, logout].map((control) =>
        control.boundingBox(),
      ),
    );
    for (let i = 1; i < boxes.length; i++) {
      expect(boxes[i].x).toBeGreaterThanOrEqual(
        boxes[i - 1].x + boxes[i - 1].width,
      );
      expect(
        Math.abs(
          boxes[i].y +
            boxes[i].height / 2 -
            boxes[i - 1].y -
            boxes[i - 1].height / 2,
        ),
      ).toBeLessThan(2);
    }
  },
);

workspaceTest(
  "Camera and archive help follows the selected subsection in the same session",
  async ({ page }) => {
    await page.setViewportSize({ width: 1440, height: 900 });
    await page.goto(
      "/iframe.html?viewMode=story&globals=language:en&id=workspace--catalog",
    );
    const help = page
      .locator(".ws-navigation")
      .getByRole("link", { name: "Help", exact: true });
    await page
      .context()
      .route("**/help", (route) =>
        route.fulfill({ path: client + "/help.html" }),
      );
    await page.context().route("**/assets/*", (route) => {
      if (route.request().frame().page() === page) return route.fallback();
      return route.fulfill({
        path: client + new URL(route.request().url()).pathname,
      });
    });
    const topics = [
      ["Recording and storage", "recording-policy"],
      ["Video rules", "video-rules"],
      ["Batch connection", "apply-camera-batch"],
      ["Archive", "archive-playback"],
      ["Activity journal", "activity-journal"],
    ];
    for (const [label, id] of topics) {
      if (id === "apply-camera-batch")
        await page
          .getByRole("button", { name: "Connection", exact: true })
          .click();
      if (id === "archive-playback")
        await page
          .getByRole("link", { name: "Monitoring", exact: true })
          .click();
      const isSection = id === "activity-journal";
      const subsection = page.getByRole(isSection ? "link" : "button", {
        name: label,
        exact: true,
      });
      await subsection.click();
      const [popup] = await Promise.all([
        page.waitForEvent("popup"),
        help.click(),
      ]);
      const url = new URL(popup.url());
      expect(url.pathname).toBe("/help");
      expect(url.search).toBe("");
      expect(
        Object.fromEntries(new URLSearchParams(url.hash.slice(1))),
      ).toEqual({ lang: "en", theme: "system", article: id });
      await expect(
        popup.getByRole("article").locator(".help-steps"),
      ).toBeVisible();
      await popup.close();
      await expect(subsection).toHaveAttribute(
        isSection ? "aria-current" : "aria-pressed",
        isSection ? "page" : "true",
      );
    }
  },
);
