import { test, expect, checkAccessibility } from "./browser.mjs";
async function open(page, story) {
  await page.goto("/iframe.html?viewMode=story&id=workspace--" + story);
  await expect(page.locator("main h1")).toBeVisible();
}
test("Installer binds both streams and can confirm only a decoded archive frame", async ({
  page,
}) => {
  await open(page, "offline");
  await page.getByLabel("Название", { exact: true }).fill("Ворота");
  await page
    .getByLabel("Основной поток", { exact: true })
    .fill("rtsps://camera.test/main");
  await page
    .getByLabel("Дополнительный поток", { exact: true })
    .fill("rtsps://camera.test/sub");
  await page.getByRole("button", { name: "Подключить камеру" }).click();
  await page.getByRole("button", { name: "Повторить проверку" }).click();
  await expect(
    page.getByRole("button", { name: "Создать тестовую запись" }),
  ).toBeFocused();
  await page
    .getByRole("button", { name: "Создать тестовую запись" })
    .press("Enter");
  await expect(
    page.getByRole("button", { name: "Открыть запись" }),
  ).toBeFocused();
  await expect(
    page.getByRole("button", { name: "Подтвердить запись" }),
  ).toHaveCount(0);
  await page.getByRole("button", { name: "Открыть запись" }).press("Enter");
  await expect(
    page.getByRole("button", { name: "Подтвердить запись" }),
  ).toBeFocused();
  expect(
    await page
      .locator("video")
      .evaluate((v) => v.readyState >= 2 && v.videoWidth > 0),
  ).toBe(true);
  await page.getByRole("button", { name: "Подтвердить запись" }).press("Enter");
  await expect(page.getByText("Запись подтверждена")).toBeVisible();
  await page.getByLabel("Название", { exact: true }).fill("Другая точка");
  await expect(page.getByText("Запись подтверждена")).toHaveCount(0);
});
test("Operator opens alert, sees the gap, returns focus and handles an empty feed", async ({
  page,
}) => {
  await open(page, "monitor");
  await page.getByRole("button", { name: /Потеря видеопотока/ }).press("Enter");
  await expect(page.locator(".ws-overlay")).toContainText("Пропуск записи");
  await expect(page.locator("video")).toBeHidden();
  await page
    .getByRole("button", { name: "К выбранной тревоге" })
    .press("Enter");
  await expect(
    page.getByRole("button", { name: /Потеря видеопотока/ }),
  ).toBeFocused();
  await page.getByRole("link", { name: "Камеры" }).click();
  await expect(
    page.getByRole("heading", { name: "Подключение камеры" }),
  ).toBeVisible();
  await page.getByRole("link", { name: "Мониторинг" }).click();
  await expect(
    page.getByRole("button", { name: /Потеря видеопотока/ }),
  ).toBeFocused();
  await page.getByRole("button", { name: /Поток восстановлен/ }).click();
  await expect(page.locator(".ws-overlay")).toContainText(
    "добавляется в каталог",
  );
  await page.getByRole("button", { name: /Движение у ворот/ }).click();
  await expect(page.getByText("Кадр записи получен")).toBeVisible();
  await expect(page.locator(".ws-player h2")).toBeFocused();
  await open(page, "empty-feed");
  await expect(page.getByText("Тревог пока нет")).toBeVisible();
});
async function appearance(page, selectors) {
  return page.evaluate(([heading, panel, nav, grid]) => {
    const style = (selector) =>
      getComputedStyle(document.querySelector(selector));
    return {
      font: style(heading).fontFamily,
      titleSize: style(heading).fontSize,
      ink: style(heading).color,
      canvas: style("body").backgroundColor,
      panel: style(panel).backgroundColor,
      selected: style(nav).backgroundColor,
      columns: style(grid).gridTemplateColumns.split(" ").length,
    };
  }, selectors);
}
for (const route of ["monitor", "installer"])
  test("Full workspace screen " + route, async ({ page }, info) => {
    await open(page, route);
    if (route === "monitor")
      await expect(page.getByText("Эфир подключён")).toBeVisible();
    await page.evaluate(() => document.fonts.ready);
    const reference = await page.context().newPage();
    await reference.goto("http://127.0.0.1:6007/");
    if (route === "installer") await reference.locator("#show-install").click();
    const referenceSelectors = [
      route === "monitor" ? "#monitor-heading" : "#install-screen h1",
      route === "monitor" ? ".monitor-panel" : ".install-grid .panel",
      '.nav-item[aria-current="page"]',
      route === "monitor" ? ".monitor-grid" : ".install-grid",
    ];
    const appearanceReport = [];
    const a11y = await checkAccessibility(page, ".ws-shell");
    await info.attach("axe-result", {
      body: JSON.stringify(a11y),
      contentType: "application/json",
    });
    await info.attach("accessibility-tree", {
      body: await page.locator(".ws-shell").ariaSnapshot(),
      contentType: "text/plain",
    });
    for (const width of [1440, 768, 390, 320]) {
      await page.setViewportSize({ width, height: 900 });
      await reference.setViewportSize({ width, height: 900 });
      const expected = await appearance(reference, referenceSelectors);
      const actual = await appearance(page, [
        "main h1",
        ".ws-panel",
        'nav [aria-current="page"]',
        route === "monitor" ? ".ws-monitor" : ".ws-install",
      ]);
      expect(actual).toEqual(expected);
      appearanceReport.push({ width, actual, expected });
      expect(
        await page.evaluate(() => document.documentElement.scrollWidth),
      ).toBeLessThanOrEqual(width);
      if (route === "monitor") {
        await page.bringToFront();
        await page.locator("video").evaluate(async (video) => {
          video.pause();
          if (video.currentTime !== 1) {
            const settled = new Promise((resolve) =>
              video.addEventListener("seeked", resolve, { once: true }),
            );
            video.currentTime = 1;
            await settled;
          }
        });
        await expect
          .poll(() => page.locator("video").evaluate((v) => v.currentTime))
          .toBe(1);
      }
      const filename = route + "-" + width + ".png";
      const capture = {
        fullPage: true,
        animations: "disabled",
        style:
          "video::-webkit-media-controls {display:none} video {pointer-events:none}",
      };
      await expect(page).toHaveScreenshot(filename, capture);
      const shot = info.outputPath(filename);
      await page.screenshot({ ...capture, path: shot });
      await info.attach(filename, { path: shot, contentType: "image/png" });
    }
    await info.attach("prototype-comparison", {
      body: JSON.stringify(appearanceReport),
      contentType: "application/json",
    });
    await reference.close();
  });
