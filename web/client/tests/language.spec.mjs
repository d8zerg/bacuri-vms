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

test("Display preferences survive sign-out and sign-in and reset when site storage is cleared", async ({
  page,
}) => {
  await page.emulateMedia({ colorScheme: "light" });
  await page.goto(monitor + "&globals=language:en");
  const root = page.locator("html");
  const canvas = page.locator("body");
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  await page.getByRole("radio", { name: "Dark theme", exact: true }).check();
  await page.getByRole("radio", { name: "Русский", exact: true }).check();
  await page.keyboard.press("Escape");
  await page.getByRole("button", { name: "Выйти", exact: true }).click();
  await expect(
    page.getByRole("heading", { name: "Вход", exact: true }),
  ).toBeVisible();
  await expect(canvas).toHaveCSS("background-color", "rgb(30, 30, 30)");

  await page.getByLabel("Имя пользователя").fill("operator.demo");
  await page.getByLabel("Пароль", { exact: true }).fill("example");
  await page.getByRole("button", { name: "Войти", exact: true }).click();
  await expect(
    page.getByRole("navigation", { name: "Рабочее место" }),
  ).toBeVisible();
  await expect(canvas).toHaveCSS("background-color", "rgb(30, 30, 30)");

  const otherTab = await page.context().newPage();
  await otherTab.goto("/index.json");
  await otherTab.evaluate(() => localStorage.clear());
  await expect(root).toHaveAttribute("lang", "en");
  await expect(canvas).toHaveCSS("background-color", "rgb(247, 244, 237)");
  await page.getByRole("button", { name: "Sign out", exact: true }).click();
  await expect(
    page.getByRole("heading", { name: "Sign in", exact: true }),
  ).toBeVisible();
  await page.emulateMedia({ colorScheme: "dark" });
  await expect(canvas).toHaveCSS("background-color", "rgb(30, 30, 30)");
  expect(
    await otherTab.evaluate(() => localStorage.getItem("bacuri.vms.language")),
  ).toBeNull();
  await otherTab.close();
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
