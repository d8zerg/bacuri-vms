import assert from "node:assert/strict";
import { chromium, expect, firefox } from "playwright/test";

const origin = "https://workspace.test";
const client = "/results/client";
const colors = { light: "rgb(247, 244, 237)", dark: "rgb(30, 30, 30)" };

async function checkStartup(
  browser,
  system,
  { saved, signedIn, denyStorage } = {},
) {
  // The checks name the Russian interface, which the client selects from the browser language.
  const page = await browser.newPage({ colorScheme: system, locale: "ru-RU" });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  let scripts, session;
  try {
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
        await session.promise;
        if (signedIn)
          return route.fulfill({
            json: { user_id: "viewer", csrf_token: "fixture", site_roles: [] },
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
        ? page.getByRole("navigation", { name: "Рабочее место" })
        : page.getByRole("heading", { name: "Вход в Bacuri VMS" });
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
    }
    assert.deepEqual(errors, []);
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
      });
    }
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
