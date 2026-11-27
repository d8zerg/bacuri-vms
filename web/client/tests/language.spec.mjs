import { expect, test } from "./browser.mjs";

const monitor = "/iframe.html?viewMode=story&id=workspace--monitor";

test("story language follows the toolbar global", async ({ page }) => {
  await page.goto(monitor + "&globals=language:en");
  await expect(page.locator("html")).toHaveAttribute("lang", "en");
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  await expect(
    page.getByRole("radiogroup", { name: "Interface language" }),
  ).toBeVisible();
  await page.keyboard.press("Escape");
  await expect(
    page.getByRole("link", { name: "Monitoring", exact: true }),
  ).toBeVisible();
});

test("operator switches the interface language", async ({ page }) => {
  await page.goto(monitor);
  await page.getByRole("button", { name: "Настройки", exact: true }).click();
  const switcher = page.getByRole("radiogroup", { name: "Язык интерфейса" });
  await switcher.getByRole("radio", { name: "English" }).check();
  await expect(page.locator("html")).toHaveAttribute("lang", "en");
  const english = page.getByRole("radiogroup", { name: "Interface language" });
  await expect(english.getByRole("radio", { name: "English" })).toBeChecked();
  await page.keyboard.press("Escape");
  await expect(
    page.getByRole("link", { name: "Monitoring", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  await english.getByRole("radio", { name: "Русский" }).check();
  await page.keyboard.press("Escape");
  await expect(page.locator("html")).toHaveAttribute("lang", "ru");
  await expect(
    page.getByRole("link", { name: "Мониторинг", exact: true }),
  ).toBeVisible();
});

for (const story of ["installer", "plan"])
  test("English workspace screen " + story, async ({ page }) => {
    await page.goto(
      "/iframe.html?viewMode=story&globals=language:en&id=workspace--" + story,
    );
    await expect(
      page.getByRole("main").getByRole("heading", { level: 1 }),
    ).toBeAttached();
    await expect(page.locator("html")).toHaveAttribute("lang", "en");
    // The screen content is loaded lazily, so the capture waits for it.
    if (story === "installer")
      await expect(
        page.getByRole("heading", { name: "Camera settings" }),
      ).toBeVisible();
    else
      await expect(page.locator(".ws-plan img")).toHaveJSProperty(
        "naturalWidth",
        640,
      );
    await expect(page.getByText("Loading…")).toHaveCount(0);
    await page.evaluate(() => document.fonts.ready);
    for (const width of [1440, 390]) {
      await page.setViewportSize({ width, height: 900 });
      // Longer English labels stay inside the page.
      expect(
        await page.evaluate(() => document.documentElement.scrollWidth),
      ).toBeLessThanOrEqual(width);
      await expect(page).toHaveScreenshot(
        "en-" + story + "-" + width + ".png",
        { fullPage: true, animations: "disabled" },
      );
    }
  });
