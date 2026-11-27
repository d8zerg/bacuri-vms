import assert from "node:assert/strict";
import { mkdirSync, writeFileSync } from "node:fs";
import { randomUUID } from "node:crypto";
import { chromium, expect, firefox } from "playwright/test";

const origin = "https://workspace.test";
const client = "/results/client";
const colors = { light: "rgb(247, 244, 237)", dark: "rgb(30, 30, 30)" };

async function checkStartup(
  browser,
  system,
  { saved, signedIn, denyStorage, language = "en" } = {},
) {
  // A Russian browser with no saved choice must still open the sign-in screen in English.
  const page = await browser.newPage({ colorScheme: system, locale: "ru-RU" });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  let scripts, session;
  try {
    if (browser.browserType() === chromium)
      await page.coverage.startJSCoverage({ resetOnNavigation: false });
    await page.route(origin + "/**", async (route) => {
      const { pathname } = new URL(route.request().url());
      if (pathname === "/seed")
        return route.fulfill({
          body: "<!doctype html>",
          contentType: "text/html",
        });
      if (pathname.startsWith("/assets/")) {
        if (pathname.endsWith(".js") && pathname !== "/assets/initial-theme.js")
          await scripts.promise;
        return route.fulfill({ path: client + pathname });
      }
      if (pathname === "/api/v1/session") {
        if (route.request().method() === "POST")
          return route.fulfill({
            status: 401,
            json: { code: "invalid_credentials" },
          });
        await session.promise;
        if (signedIn)
          return route.fulfill({
            json: {
              username: "viewer",
              user_id: "viewer",
              csrf_token: "fixture",
              site_roles: [],
            },
          });
        return route.fulfill({ status: 401, json: { code: "unauthorized" } });
      }
      if (pathname.startsWith("/api/"))
        return route.fulfill({ json: { oidc: false } });
      return route.fulfill({
        path: client + "/index.html",
        headers: { "Content-Security-Policy": "default-src 'self'" },
      });
    });
    await page.goto(origin + "/seed");
    if (language === "ru")
      await page.evaluate(() =>
        localStorage.setItem("bacuri.vms.language", "ru"),
      );
    if (saved)
      await page.evaluate(
        (value) => localStorage.setItem("bacuri.vms.theme", value),
        saved,
      );
    if (denyStorage)
      await page.addInitScript(() => {
        Object.defineProperty(window, "localStorage", {
          get() {
            throw new DOMException("Blocked", "SecurityError");
          },
        });
      });
    await page.addInitScript(() => {
      window.startupColors = [];
      function sample() {
        if (document.body) {
          const color = getComputedStyle(
            document.documentElement,
          ).backgroundColor;
          if (window.startupColors.at(-1) !== color)
            window.startupColors.push(color);
        }
        requestAnimationFrame(sample);
      }
      requestAnimationFrame(sample);
    });
    const expected = colors[saved] ?? colors[system];
    const root = page.locator("html");
    const status = page.locator("body > .ws-startup");
    for (const reload of [false, true]) {
      scripts = Promise.withResolvers();
      session = Promise.withResolvers();
      if (reload) await page.reload({ waitUntil: "commit" });
      else await page.goto(origin + "/monitor", { waitUntil: "commit" });
      await expect(status).toHaveCSS("opacity", "1");
      await expect(root).toHaveAttribute("lang", language);
      await expect(status.locator("span:visible")).toHaveText(
        language === "en" ? "Loading the session…" : "Загрузка сеанса…",
      );
      await expect(page.locator("#root")).toBeEmpty();
      await expect(root).toHaveCSS("background-color", expected);
      await expect(page.locator("body")).toHaveCSS(
        "background-color",
        expected,
      );
      await expect
        .poll(() =>
          page.evaluate(
            () => performance.getEntriesByName("first-contentful-paint").length,
          ),
        )
        .toBe(1);
      const initialBounds = await status.boundingBox();
      const initialFrame =
        browser.browserType() === chromium
          ? await page.screenshot({ timeout: 5000 })
          : null;
      scripts.resolve();
      await expect(page.locator("#root > .ws-startup")).toBeAttached();
      await expect(status).toBeVisible();
      assert.deepEqual(await status.boundingBox(), initialBounds);
      if (initialFrame)
        assert.ok(
          initialFrame.equals(await page.screenshot({ timeout: 5000 })),
          "The frame changed while waiting for the session",
        );
      session.resolve();
      const destination = signedIn
        ? page.getByRole("navigation", {
            name: language === "en" ? "Workspace" : "Рабочее место",
          })
        : page.getByRole("heading", {
            name: language === "en" ? "Sign in" : "Вход",
            exact: true,
          });
      await expect(destination).toBeVisible();
      await expect(status).toBeHidden();
      await expect(root).toHaveCSS("background-color", expected);
      await expect(page.locator("body")).toHaveCSS(
        "background-color",
        expected,
      );
      assert.deepEqual(await page.evaluate(() => window.startupColors), [
        expected,
      ]);
      if (
        !reload &&
        !signedIn &&
        !denyStorage &&
        !saved &&
        system === "light"
      ) {
        const toggle = page.getByRole("button", { name: /\((RU|EN)\)$/ });
        const submit = page.locator("button[type=submit]");
        const notice = page.locator("#login-notice");
        await expect(toggle).toHaveText("RU");
        await submit.click();
        await expect(notice).toHaveText("Enter your user name.");
        await expect(page.getByLabel("User name")).toBeFocused();
        await toggle.click();
        await expect(toggle).toHaveText("EN");
        await expect(notice).toHaveText("Введите имя пользователя.");
        await page.getByLabel("Имя пользователя").fill("viewer");
        await submit.click();
        await expect(notice).toHaveText("Введите пароль.");
        await page.getByLabel("Пароль", { exact: true }).fill("example");
        await submit.click();
        await expect(notice).toHaveText("Проверьте имя пользователя и пароль.");
        await toggle.click();
        await expect(notice).toHaveText("Check the user name and password.");
        await expect(page.getByLabel("Password", { exact: true })).toHaveValue(
          "example",
        );
        await page.context().route(origin + "/help", (route) => {
          return route.fulfill({
            contentType: "text/html",
            body: "<title>Help</title>",
          });
        });
        const [help] = await Promise.all([
          page.context().waitForEvent("page"),
          page.getByRole("button", { name: "Help", exact: true }).click(),
        ]);
        await expect(help).toHaveURL(
          origin + "/help#lang=en&theme=system&code=invalid_credentials",
        );
        await help.close();
        await toggle.click();
        await expect(root).toHaveAttribute("lang", "ru");
        language = "ru";
      }
    }
    assert.deepEqual(errors, []);
    if (browser.browserType() === chromium) {
      const scripts = (await page.coverage.stopJSCoverage())
        .filter((script) => script.url.startsWith(origin + "/assets/"))
        .map(({ url, functions }) => ({ url, functions }));
      assert.ok(scripts.some(({ url }) => url.endsWith("/initial-theme.js")));
      mkdirSync("/results/chrome", { recursive: true });
      writeFileSync(
        `/results/chrome/${randomUUID()}.json`,
        JSON.stringify(scripts),
      );
    }
  } finally {
    scripts?.resolve();
    session?.resolve();
    await page.close();
  }
}

for (const engine of [chromium, firefox]) {
  const browser = await engine.launch();
  try {
    for (const system of ["light", "dark"]) {
      await checkStartup(browser, system);
      await checkStartup(browser, system, {
        saved: system === "light" ? "dark" : "light",
        signedIn: true,
        language: "ru",
      });
    }
    await checkStartup(browser, "dark", { signedIn: true });
    await checkStartup(browser, "dark", { saved: "unknown" });
    await checkStartup(browser, "light", { denyStorage: true });
    console.log(
      engine.name(),
      "first navigation and reload passed with delayed scripts and session",
    );
  } finally {
    await browser.close();
  }
}
