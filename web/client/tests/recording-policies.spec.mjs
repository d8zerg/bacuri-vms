import { test, expect, checkAccessibility } from "./browser.mjs";

test("Camera overview, group membership and individual settings stay in their selected scope", async ({
  page,
}) => {
  await page.goto(
    "/iframe.html?id=workspace--recording-policies&viewMode=story",
  );
  const panel = page.getByRole("region", { name: "Политики записи" });
  const tab = (name) => panel.getByRole("button", { name, exact: true });
  const summary = panel.getByRole("region", {
    name: "Настройки записи камеры «Погрузочные ворота 02»",
  });
  const editor = panel.locator("form");
  const source = summary.getByRole("button", {
    name: "Открыть политику группы «Погрузочные ворота»",
  });
  await tab("Камеры").click();
  await expect(tab("Камеры")).toHaveAttribute("aria-pressed", "true");
  await expect(editor).toHaveCount(0);
  await expect(summary).toContainText("08:00 - 18:00");
  await source.click();
  await expect(tab("Группы")).toHaveAttribute("aria-pressed", "true");
  await tab("Группы").click();
  await expect(editor.getByLabel("Приоритет группы")).toHaveValue("50");
  const members = panel.locator(".rp-members");
  await expect(members).toContainText("Политика действует для 1 из 1");
  await members.locator("summary").click();
  await members
    .getByRole("button", { name: /Погрузочные ворота 02.*По политике группы/ })
    .click();
  await tab("Настроить отдельно").click();
  const saturday = tab("Сб");
  await expect(saturday).toHaveAttribute("aria-pressed", "false");
  await saturday.focus();
  await page.keyboard.press("Space");
  await tab("Отмена").click();
  await expect(source).toBeVisible();
  await tab("Настроить отдельно").click();
  await expect(saturday).toHaveAttribute("aria-pressed", "false");
  await saturday.click();
  await editor.getByLabel("До события, с").fill("8");
  await tab("Сохранить для камеры").click();
  await expect(summary).toContainText("Индивидуальная политика");
  await expect(summary).toContainText("8 с до события");
  await page.setViewportSize({ width: 320, height: 820 });
  await checkAccessibility(page, ".rp-workspace");
  expect(
    await page.evaluate(() => document.documentElement.scrollWidth),
  ).toBeLessThanOrEqual(320);
  await tab("Группы").click();
  await expect(members).toContainText("Политика действует для 0 из 1");
  await expect(editor.getByLabel("До события, с")).toHaveValue("5");
  await editor.getByLabel("До события, с").fill("7");
  await tab("Сохранить для группы").click();
  await expect(editor.getByLabel("До события, с")).toHaveValue("7");
  await members.locator("summary").click();
  await checkAccessibility(page, ".rp-workspace");
  expect(
    await page.evaluate(() => document.documentElement.scrollWidth),
  ).toBeLessThanOrEqual(320);
  await members
    .getByRole("button", {
      name: /Погрузочные ворота 02.*Индивидуальная политика/,
    })
    .click();
  await expect(summary).toContainText("8 с до события");
  await tab("Изменить настройки камеры").click();
  await tab("Вернуть наследование").click();
  await tab("Подтвердить сброс").click();
  await expect(source).toBeVisible();
  await expect(summary).toContainText("7 с до события");
  await tab("Том").click();
  await tab("Том").click();
  await expect(panel.getByRole("meter")).toBeVisible();
  await editor.getByLabel("Резерв свободного места, %").fill("25");
  await tab("Сохранить").click();
  await tab("Камеры").click();
  await tab("Том").click();
  await expect(editor.getByLabel("Резерв свободного места, %")).toHaveValue(
    "25",
  );
});
