import assert from "node:assert/strict";
import { test, expect } from "playwright/test";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);

const url = "http://127.0.0.1:8080";
const fixtures = {
  main: "rtsp://127.0.0.1:8554/main",
  sub: "rtsp://127.0.0.1:8554/sub",
};
const timings = { timeout: 8000 };

async function text(page, selector, expected) {
  await page.waitForFunction(
    ([selector, text]) => document.querySelector(selector)?.textContent.includes(text),
    [selector, expected],
    timings,
  );
}

async function focused(page, id) {
  await page.waitForFunction((expected) => document.activeElement?.id === expected, id, timings);
}

async function videoWidth(page, id, width) {
  await page.waitForFunction(
    ([id, width]) => {
      const video = document.getElementById(id);
      return video.readyState >= 2 && video.videoWidth === width;
    },
    [id, width],
    timings,
  );
}

async function checkAccessibility(page, name) {
  if (!(await page.evaluate(() => Boolean(window.axe)))) {
    await page.addScriptTag({ path: require.resolve("axe-core/axe.min.js") });
  }
  const violations = await page.evaluate(async () => {
    const result = await window.axe.run({ runOnly: ["wcag2a", "wcag2aa", "wcag21a", "wcag21aa", "wcag22aa"] });
    return result.violations.map(({ id, nodes }) => ({ id, targets: nodes.map((node) => node.target) }));
  });
  assert.deepEqual(violations, [], name);
  console.log(name + ": automated accessibility checks passed");
}

test("camera setup, archive states and keyboard navigation", async ({ page }) => {
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  page.on("response", (response) => {
    if (response.status() >= 400) errors.push(`${response.status()} ${response.url()}`);
    assert.ok(response.url().startsWith(url), `Unexpected request ${response.url()}`);
  });

  const response = await page.goto(url, { waitUntil: "domcontentloaded" });
  assert.equal(response.status(), 200);
  await text(page, "#monitor-heading", "Мониторинг");
  await videoWidth(page, "monitor-video", 640);
  assert.equal(await page.locator(".alert-card").count(), 4);
  await checkAccessibility(page, "monitor");
  await page.keyboard.press("Tab");
  assert.equal(await page.evaluate(() => document.activeElement?.getAttribute("href")), "#main-content");
  await page.keyboard.press("Enter");
  await focused(page, "main-content");

  await page.locator("#open-alert").focus();
  await page.keyboard.press("Enter");
  await focused(page, "camera-heading");
  await videoWidth(page, "monitor-video", 1280);
  await text(page, "#play-state", "Запись подтверждена");
  await page.locator("#timeline-cursor").focus();
  await page.keyboard.press("Home");
  assert.equal(await page.locator("#timeline-cursor").inputValue(), "0");
  await page.keyboard.press("ArrowRight");
  assert.equal(await page.locator("#timeline-cursor").inputValue(), "1");
  await page.locator("#back-to-alert").focus();
  await page.keyboard.press("Enter");
  assert.equal(await page.evaluate(() => document.activeElement?.querySelector(".alert-name")?.textContent), "Движение у ворот");
  await videoWidth(page, "monitor-video", 640);

  await page.getByRole("button", { name: /Потеря видеопотока/ }).click();
  await page.locator("#open-alert").click();
  await text(page, "#media-message-title", "В этом интервале нет записи");
  assert.ok(await page.locator("#play-toggle").isDisabled());
  assert.equal(await page.locator("#timeline-cursor").inputValue(), "132");

  await page.getByRole("button", { name: /Смещение часов камеры/ }).click();
  await text(page, "#play-state", "Время оценочное ±8 с");
  await videoWidth(page, "monitor-video", 1280);
  await page.getByRole("button", { name: /Поток восстановлен/ }).click();
  await text(page, "#media-message-title", "Запись добавляется в каталог");
  await page.locator("#media-retry").click();
  await text(page, "#play-state", "Запись подтверждена");
  assert.equal(await page.locator("#pending-segment").getAttribute("class"), "recorded");

  await page.getByRole("button", { name: /Движение у ворот/ }).click();
  await page.waitForFunction(() => !document.getElementById("monitor-video").paused, undefined, timings);
  await page.locator("#play-toggle").click();
  const seconds = await page.locator("#timeline-cursor").inputValue();
  await page.locator("#network-condition").selectOption("offline");
  await text(page, "#media-message-title", "Связь потеряна");
  await page.locator("#media-retry").click();
  await text(page, "#play-state", "Запись подтверждена");
  assert.equal(await page.locator("#timeline-cursor").inputValue(), seconds);
  assert.equal(await page.getByRole("button", { name: /Движение у ворот/ }).getAttribute("aria-pressed"), "true");

  await page.locator("#show-install").focus();
  await page.keyboard.press("Enter");
  await focused(page, "install-heading");
  await text(page, "#current-role", "Администратор");
  await page.locator("#check-camera").focus();
  await page.keyboard.press("Enter");
  await focused(page, "main-url");
  assert.equal(await page.locator("#main-url").getAttribute("aria-invalid"), "true");
  await text(page, "#form-message", "Введите RTSP-адрес");
  await page.locator("#fill-fixture").click();
  await focused(page, "check-camera");

  await page.locator("#network-condition").selectOption("offline");
  await page.locator("#check-camera").click();
  await text(page, "#camera-connection", "Нет связи");
  assert.equal(await page.locator("#main-url").inputValue(), fixtures.main);
  assert.equal(await page.locator("#sub-url").inputValue(), fixtures.sub);
  assert.ok(await page.locator("#record-button").isDisabled());
  await page.locator("#network-condition").selectOption("online");
  await page.locator("#check-camera").focus();
  await page.keyboard.press("Enter");
  await focused(page, "record-button");
  await text(page, "#sub-state", "640 × 360");
  await page.keyboard.press("Enter");
  await focused(page, "view-recording");
  await text(page, "#record-state", "Запись сохранена");
  await page.keyboard.press("Enter");
  await focused(page, "verify-recording");
  await videoWidth(page, "install-video", 1280);
  await page.keyboard.press("Enter");
  await focused(page, "install-feedback");
  await text(page, "#playback-state", "Подтверждено");
  assert.ok(await page.locator("#verify-recording").isDisabled());

  await checkAccessibility(page, "installer");
  for (const width of [768, 390, 320]) {
    await page.setViewportSize({ width, height: 844 });
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
    await page.locator("#show-monitor").click();
    await focused(page, "monitor-heading");
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
    await page.locator("#show-install").click();
  }
  await page.setViewportSize({ width: 390, height: 844 });
  await page.locator("#show-monitor").click();
  await page.locator("#show-live").click();
  await videoWidth(page, "monitor-video", 640);
  assert.deepEqual(errors, []);
});

test("full operator and installer screens", async ({ page }, testInfo) => {
  await page.goto(url);
  await text(page, "#current-role", "Администратор");
  await expect(page.locator("#camera-heading")).toHaveCSS("font-family", /^Inter\b/);
  const loadedFonts = await page.evaluate(async () => {
    await document.fonts.ready;
    return [...document.fonts].filter(face => face.family === "Inter" && face.status === "loaded").length;
  });
  assert.equal(loadedFonts, 2, "Both bundled Inter subsets render the camera screen");
  const dark = testInfo.project.use.colorScheme === "dark";
  await expect(page.locator("body")).toHaveCSS("background-color", dark ? "rgb(30, 30, 30)" : "rgb(247, 244, 237)");
  await expect(page.locator(".monitor-panel")).toHaveCSS("background-color", dark ? "rgb(37, 37, 38)" : "rgb(255, 253, 248)");
  await expect(page.locator("#camera-heading")).toHaveCSS("color", dark ? "rgb(212, 212, 212)" : "rgb(32, 32, 32)");
  await expect(page.locator("#camera-profile")).toHaveCSS("color", dark ? "rgb(166, 166, 166)" : "rgb(87, 87, 87)");
  await expect(page.locator("#open-alert")).toHaveCSS("background-color", dark ? "rgb(80, 74, 55)" : "rgb(225, 216, 187)");
  const selectedBackground = dark ? "rgb(55, 52, 40)" : "rgb(238, 233, 218)";
  await expect(page.locator('.nav-item[aria-current="page"]')).toHaveCSS("background-color", selectedBackground);
  await expect(page.locator('.alert-card[aria-pressed="true"]')).toHaveCSS("background-color", selectedBackground);
  await expect(page.locator(".timeline-track .recorded").first()).toHaveCSS("background-color", dark ? "rgb(155, 141, 111)" : "rgb(139, 128, 98)");
  await expect(page.locator(".media-clock").first()).toHaveCSS("color", "rgb(255, 249, 236)");
  await videoWidth(page, "monitor-video", 640);
  // Freeze only the live fixture, so the clock and video frame are repeatable.
  await page.locator("#monitor-video").evaluate(async (video) => {
    video.pause();
    const settled = new Promise((resolve) => video.addEventListener("seeked", resolve, { once: true }));
    video.currentTime = 1;
    await settled;
  });
  for (const screen of ["monitor", "installer"]) {
    if (screen === "installer") await page.locator("#show-install").click();
    for (const width of [1440, 768, 390, 320]) {
      await page.setViewportSize({ width, height: 900 });
      const name = `${screen}-${width}.png`;
      await expect(page).toHaveScreenshot(name, { fullPage: true });
      await page.screenshot({ path: testInfo.outputPath(name), fullPage: true });
    }
  }
});
