import { createRequire } from "node:module";
import { test, expect } from "playwright/test";
import { coverageReport } from "./coverage.mjs";
const require = createRequire(import.meta.url);
const open = async (page, name) => {
  await page.goto("/iframe.html?viewMode=story&id=archive-timeline--" + name);
  await expect(
    page.getByRole("heading", { name: "Шкала архива" }),
  ).toBeVisible();
};
const slider = (page) => page.getByRole("slider", { name: "Позиция в архиве" });

test.beforeEach(async ({ page }, info) => {
  if (info.project.name === "chromium-light")
    await page.coverage.startJSCoverage({ resetOnNavigation: false });
});
test.afterEach(async ({ page }, info) => {
  if (info.project.name === "chromium-light") {
    const entries = await page.coverage.stopJSCoverage();
    await coverageReport().add(
      entries.filter((entry) =>
        entry.url.startsWith("http://127.0.0.1:6006/assets/"),
      ),
    );
  }
});
test("Catalog theme overrides the system setting and exposes Russian content", async ({
  page,
}, info) => {
  const theme = info.project.use.colorScheme === "light" ? "dark" : "light";
  await page.goto(
    "/iframe.html?viewMode=story&id=archive-timeline--gap&globals=theme:" +
      theme,
  );
  await expect(slider(page)).toBeVisible();
  await expect(page.locator("html")).toHaveAttribute("lang", "ru");
  await expect(page.locator(".bt-timeline")).toHaveCSS("color-scheme", theme);
  await expect(page.locator(".bt-timeline")).toHaveCSS(
    "background-color",
    theme === "dark" ? "rgb(37, 37, 38)" : "rgb(255, 253, 248)",
  );
});

test("Keyboard selects UTC within the half-open interval and retains focus", async ({
  page,
}) => {
  await open(page, "gap");
  const input = slider(page);
  await page.getByRole("button", { name: "Обновить шкалу" }).focus();
  await page.keyboard.press("Tab");
  await expect(input).toBeFocused();
  await expect(input).toHaveAccessibleDescription(/14:21:55 - 14:22:40 UTC/);
  await expect(input).toHaveAttribute(
    "aria-valuetext",
    /14:22:12 UTC. Пропуск записи. Нет связи с камерой/,
  );
  await input.press("Home");
  await expect(input).toHaveAttribute("aria-valuetext", /14:20:00 UTC. Запись/);
  await input.press("ArrowRight");
  await expect(input).toHaveAttribute("aria-valuetext", /14:20:01 UTC. Запись/);
  await input.press("ArrowLeft");
  await input.press("ArrowLeft");
  await expect(input).toHaveAttribute("aria-valuetext", /14:20:00 UTC/);
  await input.press("End");
  await input.press("ArrowRight");
  await expect(input).toHaveAttribute(
    "aria-valuetext",
    /14:25:59,999 UTC. Нет сведений/,
  );
  await expect(input).toBeFocused();
  await expect(input).toHaveCSS("outline-style", "solid");
  await open(page, "gap");
  for (let second = 132; second < 160; second++)
    await slider(page).press("ArrowRight");
  await expect(slider(page)).toHaveAttribute(
    "aria-valuetext",
    /14:22:40 UTC. Запись/,
  );
  await slider(page).press("ArrowLeft");
  await expect(slider(page)).toHaveAttribute(
    "aria-valuetext",
    /14:22:39 UTC. Пропуск записи/,
  );
});
test("Coverage evidence, accessible names and reflow at narrow widths", async ({
  page,
}, info) => {
  await page.setViewportSize({ width: 320, height: 800 });
  for (const [story, label] of Object.entries({
    recorded: "Запись",
    estimated: "Оценочное время ±8 с",
    pending: "Каталогизация",
    unavailable: "Запись недоступна. Том недоступен",
    unknown: "Нет сведений",
    midnight: "Оценочное время. Погрешность неизвестна",
  })) {
    await open(page, story);
    await expect(slider(page)).toHaveAttribute(
      "aria-valuetext",
      new RegExp(label.replaceAll(".", "\\.")),
    );
  }
  await slider(page).press("End");
  await expect(slider(page)).toHaveAttribute("aria-valuetext", /19 сент/);
  await open(page, "gap");
  await page.locator("html").evaluate((el) => {
    el.style.fontSize = "200%";
  });
  await expect(slider(page)).toBeVisible();
  expect(
    await page.evaluate(() => document.documentElement.scrollWidth),
  ).toBeLessThanOrEqual(320);
  await page.addScriptTag({ path: require.resolve("axe-core/axe.min.js") });
  const violations = await page.evaluate(async () =>
    (
      await window.axe.run(document.querySelector(".bt-timeline"), {
        runOnly: ["wcag2a", "wcag2aa", "wcag21a", "wcag21aa", "wcag22aa"],
      })
    ).violations.map((v) => ({
      id: v.id,
      nodes: v.nodes.map((n) => n.target),
    })),
  );
  expect(violations).toEqual([]);
  await info.attach("timeline-320", {
    body: await page.locator(".bt-timeline").screenshot(),
    contentType: "image/png",
  });
});
test("Pointer selects the interval under the cursor and refresh preserves the moment", async ({
  page,
}) => {
  await open(page, "recorded");
  const input = slider(page);
  const box = await input.boundingBox();
  await input.click({ position: { x: box.width * 0.37, y: box.height / 2 } });
  await expect(input).toHaveAttribute("aria-valuetext", /Пропуск записи/);
  const bands = page.locator(".bt-bands");
  await expect(bands.locator('[data-kind="recorded"]').first()).toHaveCSS(
    "background-image",
    "none",
  );
  for (const [kind, pattern] of Object.entries({
    gap: "135deg",
    estimated: "90deg",
    pending: "radial-gradient",
    unavailable: "0deg",
  })) {
    await expect(bands.locator('[data-kind="' + kind + '"]')).toHaveCSS(
      "background-image",
      new RegExp(pattern),
    );
  }
  await open(page, "pending");
  await expect(input).toHaveAttribute(
    "aria-valuetext",
    /14:25:01 UTC. Каталогизация/,
  );
  await page.getByRole("button", { name: "Обновить шкалу" }).click();
  await expect(input).toHaveAttribute("aria-valuetext", /14:25:01 UTC. Запись/);
});

test("Loading, empty results and recovery retain distinct actions and focus", async ({
  page,
}) => {
  await open(page, "loading");
  await expect(page.getByRole("status")).toHaveText("Загрузка архива…");
  await expect(
    page.getByRole("button", { name: "Обновить шкалу" }),
  ).toBeDisabled();
  await expect(slider(page)).toHaveCount(0);
  await open(page, "empty");
  await expect(page.getByRole("status")).toHaveText(/нет данных архива/);
  await expect(slider(page)).toHaveCount(0);
  await open(page, "recovery");
  await expect(page.getByRole("status")).toHaveText(
    /Связь с площадкой потеряна/,
  );
  const button = page.getByRole("button", { name: "Обновить шкалу" });
  await button.focus();
  await button.press("Enter");
  await expect(slider(page)).toHaveAttribute(
    "aria-valuetext",
    /14:21:22 UTC. Запись/,
  );
  await expect(button).toBeFocused();
});
