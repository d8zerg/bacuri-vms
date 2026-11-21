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
test("Administrator selects discovered profiles and replaces an existing binding", async ({
  page,
}) => {
  await open(page, "installer");
  await page
    .getByRole("button", { name: "Поиск ONVIF и замена камеры" })
    .click();
  await page.getByLabel("Точка наблюдения").selectOption("previous");
  await page.getByRole("button", { name: "Найти камеры ONVIF" }).click();
  await expect(page.getByLabel("Обнаруженная камера")).toBeVisible();
  await page.getByLabel("Пользователь камеры").fill("viewer");
  await page.getByRole("button", { name: "Получить профили" }).click();
  await expect(
    page.getByText("Камера отклонила имя или пароль."),
  ).toBeVisible();
  await page.getByLabel("Пароль камеры").fill("demo");
  await page.getByRole("button", { name: "Получить профили" }).click();
  await expect(page.getByText(/stand-A/)).toBeVisible();
  await page.getByLabel("Профиль эфира").selectOption("rtsps://camera.test/1");
  await expect(
    page.getByRole("button", { name: "Использовать профили" }),
  ).toBeDisabled();
  await page.getByLabel("Профиль записи").selectOption("rtsps://camera.test/2");
  await checkAccessibility(page, ".ws-shell");
  await page.getByRole("button", { name: "Использовать профили" }).click();
  await expect(page.getByLabel("Основной поток", { exact: true })).toHaveValue(
    "rtsps://camera.test/2",
  );
  await expect(page.getByLabel("Название", { exact: true })).toHaveValue(
    "Ворота",
  );
  await page.getByRole("button", { name: "Подключить камеру" }).click();
  await page.getByRole("button", { name: "Открыть предыдущую запись" }).click();
  await expect(
    page.getByRole("heading", { name: "Предыдущая запись" }),
  ).toBeVisible();
  await expect(
    page.getByRole("button", { name: "Подтвердить запись" }),
  ).toHaveCount(0);
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

for (const [story, state] of [
  ["plan", "Первый этаж"],
  ["empty-maps", "Карта региона не установлена"],
  ["maps-unavailable", "Не удалось загрузить карты и планы"],
]) {
  test("Map screen " + story, async ({ page }) => {
    await open(page, story);
    await expect(page.locator("main")).toContainText(state);
    if (story === "plan")
      await expect(page.locator(".ws-plan img")).toHaveJSProperty(
        "naturalWidth",
        640,
      );
    await page.evaluate(() => document.fonts.ready);
    await checkAccessibility(page, ".ws-shell");
    await expect(page.locator("main")).toMatchAriaSnapshot({
      name: story + ".aria.yml",
    });
    for (const width of [1440, 768, 390, 320]) {
      await page.setViewportSize({ width, height: 900 });
      expect(
        await page.evaluate(() => document.documentElement.scrollWidth),
      ).toBeLessThanOrEqual(width);
      await expect(page).toHaveScreenshot(story + "-" + width + ".png", {
        fullPage: true,
        animations: "disabled",
      });
    }
  });
}

test("Administrator uploads a plan and reopens a positioned point", async ({
  page,
}) => {
  await open(page, "plan");
  const x = page.getByRole("spinbutton", { name: "X", exact: true });
  await x.fill("0.42");
  await page.getByRole("button", { name: "Сохранить положение" }).click();
  await expect(page.getByText("Положение сохранено")).toBeVisible();
  await page.getByLabel("Масштаб плана").selectOption("2");
  await page.getByRole("link", { name: "Эфир", exact: true }).click();
  await expect(page.getByRole("heading", { name: "Мониторинг" })).toBeVisible();
  await page.getByRole("link", { name: "Карты и планы" }).click();
  await expect(page.locator("#map-point-dock")).toBeFocused();
  await expect(x).toHaveValue("0.42");
  await expect(page.getByLabel("Масштаб плана")).toHaveValue("2");
  await page.getByText("Добавить план", { exact: true }).click();
  await page.getByLabel("Название плана").fill("Второй этаж");
  await page.getByLabel("Изображение PNG").setInputFiles("stories/floor.png");
  await page.getByRole("button", { name: "Загрузить план" }).click();
  await expect(
    page.getByRole("heading", { name: "Второй этаж" }),
  ).toBeVisible();
  await page.locator(".ws-plan img").click({ position: { x: 75, y: 45 } });
  await page.getByRole("button", { name: "Сохранить положение" }).click();
  await expect(page.getByText("Положение сохранено")).toBeVisible();
  await page.getByLabel("Карта или план").selectOption("");
  await expect(page.getByText("Карта региона не установлена")).toBeVisible();
});

test("Operator closes an alert, searches its history and controls delegated work", async ({
  page,
}) => {
  await open(page, "monitor");
  await page.getByRole("button", { name: /Движение у ворот/ }).click();
  const panel = page.locator(".ws-alert-work");
  await panel.getByRole("button", { name: "Взять в работу" }).click();
  await expect(panel).toContainText("Ответственный: Дежурный");
  const note = panel.getByLabel("Результат или основание закрытия");
  await note.fill("Запись проверена");
  await panel.getByRole("button", { name: "Записать результат" }).click();
  await expect(
    panel.getByRole("button", { name: "Закрыть тревогу" }),
  ).toBeDisabled();
  await note.fill("Проверка завершена");
  await panel.getByRole("button", { name: "Закрыть тревогу" }).click();
  await expect(panel.locator(".ws-history li")).toHaveCount(3);
  await panel.locator("summary").click();
  await panel.getByLabel("Имя сервиса").fill("Видеоанализ");
  await panel.getByRole("button", { name: "Выдать поручение" }).click();
  await expect(panel.getByLabel("Токен для передачи сервису")).toHaveValue(
    "test-credential",
  );
  await panel.getByRole("button", { name: "Отозвать" }).click();
  await expect(panel.getByText("Поручение отозвано")).toBeVisible();
  await checkAccessibility(page, ".ws-alert-work");
  await panel
    .getByRole("link", { name: "Открыть журнал этой тревоги" })
    .click();
  await expect(
    page.getByRole("heading", { name: "Журнал тревог" }),
  ).toBeVisible();
  const query = page.getByLabel("Результат или основание");
  await query.fill("Другое основание");
  await page.getByRole("button", { name: "Найти" }).click();
  await expect(page.getByText("Записи не найдены")).toBeVisible();
  await query.fill("Запись проверена");
  await page.getByRole("button", { name: "Найти" }).click();
  await expect(page.locator(".ws-history li")).toHaveCount(1);
  await checkAccessibility(page, ".ws-alert-work");
  await page
    .getByRole("link", { name: "Открыть тревогу", exact: true })
    .click();
  await expect(panel.locator("strong").first()).toHaveText("Закрыта");
});
