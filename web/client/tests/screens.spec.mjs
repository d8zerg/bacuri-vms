import { test, expect, checkAccessibility } from "./browser.mjs";
async function open(page, story) {
  await page.goto("/iframe.html?viewMode=story&id=workspace--" + story);
  await expect(page.locator("main h1")).toBeVisible();
}

test("Workspace theme follows the system, supports keyboard selection and survives reload and logout", async ({
  page,
}) => {
  await page.setViewportSize({ width: 320, height: 900 });
  await page.emulateMedia({ colorScheme: "light" });
  await open(page, "monitor");
  const theme = page.getByRole("radiogroup", { name: "Тема оформления" });
  const system = theme.getByRole("radio", { name: "Системная тема" });
  const dark = theme.getByRole("radio", { name: "Тёмная тема" });
  const light = theme.getByRole("radio", { name: "Светлая тема" });
  const logout = page.getByRole("button", { name: "Выйти" });
  const canvas = page.locator("body");
  const lightColor = "rgb(247, 244, 237)";
  const darkColor = "rgb(30, 30, 30)";
  await expect(system).toBeChecked();
  await expect(canvas).toHaveCSS("background-color", lightColor);
  await page.emulateMedia({ colorScheme: "dark" });
  await expect(canvas).toHaveCSS("background-color", darkColor);
  await system.press("ArrowRight");
  await expect(dark).toBeChecked();
  await page.emulateMedia({ colorScheme: "light" });
  await expect(canvas).toHaveCSS("background-color", darkColor);
  await page.reload();
  await expect(dark).toBeChecked();
  await expect(canvas).toHaveCSS("background-color", darkColor);
  await dark.press("ArrowRight");
  await expect(light).toBeChecked();
  await page.emulateMedia({ colorScheme: "dark" });
  await expect(canvas).toHaveCSS("background-color", lightColor);
  const otherTab = await page.context().newPage();
  await open(otherTab, "monitor");
  await expect(
    otherTab.getByRole("radio", { name: "Светлая тема" }),
  ).toBeChecked();
  await system.check();
  await expect(
    otherTab.getByRole("radio", { name: "Системная тема" }),
  ).toBeChecked();
  await otherTab.close();
  await expect(canvas).toHaveCSS("background-color", darkColor);
  await light.check();
  await page.getByRole("link", { name: "Журнал тревог" }).click();
  await expect(light).toBeChecked();
  await expect(logout).toHaveAttribute("title", "Выйти");
  await expect(logout).toHaveText("");
  await logout.click();
  await expect(
    page.getByRole("heading", { name: "Вход в Bacuri VMS" }),
  ).toBeVisible();
  await expect(canvas).toHaveCSS("background-color", lightColor);
});

test("Workspace navigation keeps icons level and labels inside touch targets", async ({
  page,
}) => {
  await open(page, "monitor");
  await page.evaluate(() => document.fonts.ready);
  const nav = page.getByRole("navigation", { name: "Рабочее место" });
  const links = nav.getByRole("link");
  await expect(links).toHaveCount(5);
  for (const width of [
    320, 360, 390, 448, 449, 600, 752, 768, 1041, 1080, 1280, 1281, 1440,
  ]) {
    await page.setViewportSize({ width, height: 900 });
    const brand = await page.locator(".ws-brand").boundingBox();
    const switcher = await page
      .getByRole("radiogroup", { name: "Тема оформления" })
      .boundingBox();
    const logout = await page
      .getByRole("button", { name: "Выйти" })
      .boundingBox();
    expect(switcher.x).toBeGreaterThanOrEqual(brand.x + brand.width);
    expect(logout.x).toBeGreaterThan(switcher.x + switcher.width);
    expect(logout.x + logout.width).toBeLessThanOrEqual(width);
    const menu = await nav.boundingBox();
    if (
      menu.y < switcher.y + switcher.height &&
      switcher.y < menu.y + menu.height
    ) {
      expect(switcher.x).toBeGreaterThan(menu.x + menu.width);
    }
    expect(
      Math.abs(switcher.y + switcher.height / 2 - (brand.y + brand.height / 2)),
    ).toBeLessThan(1);
    expect(Math.abs(logout.y - switcher.y)).toBeLessThan(1);
    await expect(
      nav.getByRole("link", { name: "Журнал тревог", exact: true }),
    ).toHaveText(width <= 448 ? "Журнал" : "Журнал тревог", {
      useInnerText: true,
    });
    await expect(
      nav.getByRole("link", { name: "Карты и планы", exact: true }),
    ).toHaveText(width <= 448 ? "Карты" : "Карты и планы", {
      useInnerText: true,
    });
    expect(
      await nav.evaluate((el) => el.scrollWidth - el.clientWidth),
    ).toBeLessThanOrEqual(1);
    const items = await links.evaluateAll((nodes) =>
      nodes.map((link) => {
        const box = link.getBoundingClientRect();
        const icon = link.querySelector("svg").getBoundingClientRect();
        const label = link.querySelector("span").getBoundingClientRect();
        return {
          iconY: icon.top,
          labelY: label.top,
          width: box.width,
          height: box.height,
          inset: Math.min(
            label.left - box.left,
            box.right - label.right,
            box.bottom - label.bottom,
          ),
        };
      }),
    );
    for (const item of items) {
      expect(Math.abs(item.iconY - items[0].iconY)).toBeLessThan(1);
      expect(Math.abs(item.labelY - items[0].labelY)).toBeLessThan(1);
      expect(item.inset).toBeGreaterThanOrEqual(3);
      if (width <= 752) {
        expect(Math.min(item.width, item.height)).toBeGreaterThanOrEqual(44);
      }
    }
  }
});
test("The site selector fits below the header controls on a narrow screen", async ({
  page,
}) => {
  await page.setViewportSize({ width: 320, height: 900 });
  await page.goto(
    "/iframe.html?viewMode=story&id=workspace--monitor&args=condition:multiple-sites",
  );
  const selector = page.getByRole("combobox", { name: "Площадка" });
  await expect(selector).toBeVisible();
  await expect(selector.locator("option")).toHaveCount(2);
  const field = await selector.boundingBox();
  const controls = await page
    .getByRole("button", { name: "Выйти" })
    .boundingBox();
  const menu = await page
    .getByRole("navigation", { name: "Рабочее место" })
    .boundingBox();
  expect(field.y).toBeGreaterThanOrEqual(controls.y + controls.height);
  expect(menu.y).toBeGreaterThanOrEqual(field.y + field.height);
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(
    320,
  );
});
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
    const colors =
      info.project.use.colorScheme === "dark"
        ? {
            ink: "rgb(212, 212, 212)",
            canvas: "rgb(30, 30, 30)",
            panel: "rgb(37, 37, 38)",
            selected: "rgb(55, 52, 40)",
          }
        : {
            ink: "rgb(32, 32, 32)",
            canvas: "rgb(247, 244, 237)",
            panel: "rgb(255, 253, 248)",
            selected: "rgb(238, 233, 218)",
          };
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
      const actual = await appearance(page, [
        "main h1",
        ".ws-panel",
        'nav [aria-current="page"]',
        route === "monitor" ? ".ws-monitor" : ".ws-install",
      ]);
      expect(actual).toMatchObject({
        ...colors,
        font: "Inter, system-ui, -apple-system, sans-serif",
        columns: width >= 768 ? 2 : 1,
      });
      expect(parseFloat(actual.titleSize)).toBeCloseTo(
        width === 1440 ? 36 : 26.4,
        1,
      );
      appearanceReport.push({ width, ...actual });
      expect(
        await page.evaluate(() => document.documentElement.scrollWidth),
      ).toBeLessThanOrEqual(width);
      if (route === "monitor") {
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
    await info.attach("workspace-appearance", {
      body: JSON.stringify(appearanceReport),
      contentType: "application/json",
    });
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

test("Review an event on the plan, compare live video, then resume the same event", async ({
  page,
}) => {
  await open(page, "plan");
  const panel = page.getByRole("region", { name: "События камеры" });
  await expect(panel.getByRole("button", { name: /Проверить/ })).toHaveCount(3);
  await expect(panel.locator("video")).toHaveCount(0);
  await panel
    .getByRole("button", { name: /Потеря видеопотока/ })
    .press("Enter");
  await expect(panel.locator(".ws-overlay")).toContainText(
    "Пропуск записи. Нет связи с камерой",
  );
  await expect(panel.locator("video")).toBeHidden();
  await panel.getByRole("button", { name: "Эфир", exact: true }).click();
  await expect(panel.getByText("Эфир подключён")).toBeVisible();
  await panel.getByRole("button", { name: "Архив", exact: true }).click();
  await expect(panel.locator("video")).toBeHidden();
  await panel.getByRole("button", { name: /Показать ещё/ }).click();
  await panel.getByRole("button", { name: /Движение у ворот/ }).press("Enter");
  await expect(panel.getByText("Кадр записи получен")).toBeVisible();
  await expect(panel.locator("video")).toHaveCount(1);
  await checkAccessibility(page, ".ws-shell");
  await page.getByLabel("Масштаб плана").selectOption("2");
  await panel
    .getByRole("link", { name: "Открыть тревогу в мониторинге" })
    .click();
  await expect(page.locator(".ws-alert-work")).toBeVisible();
  await page.getByRole("link", { name: "Карты и планы" }).click();
  const row = panel.getByRole("button", { name: /Движение у ворот/ });
  await expect(row).toBeFocused();
  await expect(row).toHaveAttribute("aria-expanded", "true");
  await expect(page.getByLabel("Масштаб плана")).toHaveValue("2");
  await row.press("Enter");
  await expect(panel.locator("video")).toHaveCount(0);
  await page.setViewportSize({ width: 390, height: 844 });
  await page.locator(".ws-plan .ws-pin").click();
  await expect(
    page.getByRole("heading", { name: "Погрузочные ворота 02" }),
  ).toBeFocused();
});

test("Administrator calibrates the camera sector on a non-square floor plan", async ({
  page,
}) => {
  await open(page, "plan");
  await page.getByText("Настроить сектор обзора", { exact: true }).click();
  await page.getByRole("button", { name: "Указать ориентир на карте" }).click();
  const plan = page.locator(".ws-plan img"),
    box = await plan.boundingBox();
  await plan.click({ position: { x: box.width * 0.65, y: box.height * 0.52 } });
  await expect(page.getByLabel("Направление от верха плана, °")).toHaveValue(
    /^(89|90)/,
  );
  await page.getByLabel("Горизонтальный угол обзора, °").fill("45");
  await page.getByRole("button", { name: "Сохранить направление" }).click();
  await expect(page.getByText("Направление сохранено")).toBeVisible();
  await expect(page.locator(".ws-pin")).toHaveAttribute(
    "title",
    /угол обзора 45°/,
  );
  await checkAccessibility(page, ".ws-shell");
});

test("Administrator calibrates a PTZ camera reporting normalized positions", async ({
  page,
}) => {
  await open(page, "ptz-plan");
  await page.getByText("Настроить сектор обзора", { exact: true }).click();
  await expect(page.getByText(/Показание pan/)).toContainText("0.15");
  await page.getByLabel("Направление от верха плана, °").fill("210");
  await page
    .getByLabel("Градусов поворота по часовой стрелке на единицу pan")
    .fill("-180");
  await page.getByLabel("Угол обзора при максимальном зуме, °").fill("15");
  await page.getByRole("button", { name: "Сохранить направление" }).click();
  await expect(page.locator(".ws-sector")).toHaveAttribute(
    "data-state",
    "live",
  );
  await checkAccessibility(page, ".ws-shell");
});

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
  const archiveTime = await page
    .locator(".ws-transport time")
    .getAttribute("datetime");
  await page.getByRole("button", { name: "Эфир", exact: true }).click();
  await expect(note).toHaveValue("Запись проверена");
  await page.getByRole("button", { name: "Архив", exact: true }).click();
  await expect(page.locator(".ws-transport time")).toHaveAttribute(
    "datetime",
    archiveTime,
  );
  await expect(note).toHaveValue("Запись проверена");
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
  await page.getByRole("button", { name: "Сбросить" }).click();
  await expect(query).toHaveValue("");
  await page.getByRole("button", { name: "Следующая страница" }).click();
  await expect(page.locator(".ws-history li")).toHaveCount(1);
  await page.getByRole("button", { name: "Предыдущая страница" }).click();
  await expect(page.locator(".ws-history li")).toHaveCount(2);
  await query.fill("Запись проверена");
  await page.getByRole("button", { name: "Найти" }).click();
  await expect(page.locator(".ws-history li")).toHaveCount(1);
  await checkAccessibility(page, ".ws-alert-work");
  await page
    .getByRole("link", { name: "Открыть тревогу", exact: true })
    .click();
  await expect(panel.locator("strong").first()).toHaveText("Закрыта");
});

test("Administrator selects online regions, pauses a download and updates installed coverage from a file", async ({
  page,
}) => {
  await open(page, "map-management");
  const area = page.getByRole("region", { name: "Управление регионами" });
  await area.getByRole("button", { name: "Установить регионы" }).click();
  const catalog = area.getByLabel("HTTPS-каталог");
  await catalog.fill("https://maps.test/unavailable.json");
  await area.getByRole("button", { name: "Показать каталог" }).click();
  await expect(area.getByRole("status")).toContainText("Каталог недоступен");
  await catalog.fill("https://maps.test/catalog.json");
  await area.getByRole("button", { name: "Показать каталог" }).click();
  const south = area.getByRole("checkbox", { name: /Юг/ });
  await south.check();
  await expect(area.getByRole("checkbox", { name: /Север/ })).toHaveCount(0);
  await area.getByRole("button", { name: "Установить выбранные" }).click();
  await expect(
    area.getByRole("list", { name: "Состояние регионов" }),
  ).toContainText("Ожидание связи");
  await area.getByRole("button", { name: "Приостановить загрузку" }).click();
  await expect(area.getByText(/На паузе/)).toBeVisible();
  await area.getByRole("button", { name: "Продолжить загрузку" }).click();
  await expect(area.getByText(/Скачивание/)).toBeVisible();
  await area.getByRole("button", { name: "Обновить установленные" }).click();
  await area.getByLabel("Обновить из файла").setInputFiles({
    name: "regions.zip",
    mimeType: "application/zip",
    buffer: Buffer.alloc(1200000),
  });
  await expect(
    area.getByText(/За новыми границами останутся камеры: 1/),
  ).toBeVisible();
  await expect(area.getByRole("checkbox", { name: /Юг/ })).toHaveCount(0);
  await area.getByRole("checkbox", { name: "Выбрать все" }).check();
  await checkAccessibility(page, ".ws-map-packages");
  await area.getByRole("button", { name: "Обновить выбранные" }).click();
  await expect(area.getByText(/Север · Установлен/)).toBeVisible();
  await area.getByRole("button", { name: "Закрыть" }).click();
  await expect(catalog).toHaveCount(0);
});

test("Administrator references a floor offline and resolves an existing camera position", async ({
  page,
}) => {
  await open(page, "plan");
  await page.getByRole("button", { name: "Привязать план к карте" }).click();
  await page.getByRole("button", { name: "Предпросмотр" }).click();
  await expect(
    page.getByRole("status").filter({ hasText: "Укажите положение" }),
  ).toBeVisible();
  await page.getByLabel("Способ привязки").selectOption("width");
  await page.getByLabel("X на плане").fill("0.5");
  await page.getByLabel("Y на плане").fill("0.2");
  await page.getByLabel("Широта").fill("43.73");
  await page.getByLabel("Долгота").fill("7.4");
  await page.getByLabel("Ширина изображения, м").fill("40");
  await page.getByRole("button", { name: "Восток" }).click();
  await expect(page.getByLabel("Азимут верха")).toHaveValue("90");
  await page.getByLabel("Здание для группировки").fill("Корпус");
  await page.getByRole("button", { name: "Предпросмотр" }).click();
  const save = page.getByRole("button", { name: "Сохранить привязку" });
  await expect(save).toBeDisabled();
  await page
    .getByRole("combobox", { name: /расхождение/ })
    .selectOption("keep");
  await checkAccessibility(page, ".ws-shell");
  await page.setViewportSize({ width: 320, height: 900 });
  expect(
    await page.evaluate(() => document.documentElement.scrollWidth),
  ).toBeLessThanOrEqual(320);
  await save.click();
  await page
    .getByRole("button", { name: "Настроить привязку к карте" })
    .click();
  await expect(page.getByLabel("Ширина изображения, м")).toHaveValue("40");
  await page.getByLabel("Способ привязки").selectOption("unlink");
  await page.getByRole("button", { name: "Предпросмотр" }).click();
  await page.getByRole("button", { name: "Снять привязку" }).click();
  await expect(
    page.getByRole("button", { name: "Привязать план к карте" }),
  ).toBeVisible();
});

test("Switching building floors isolates cameras and opens their source plan", async ({
  page,
}, info) => {
  await open(page, "linked-floors");
  const floors = page.getByLabel("План на геокарте");
  await expect(floors).toHaveValue("");
  if (info.project.name.startsWith("chromium")) {
    await page.getByRole("button", { name: /Корпус · Новых тревог/ }).click();
    await expect(page.locator(".maplibregl-marker .ws-sector")).toHaveCount(1);
  } else await floors.selectOption("floor");
  await expect(floors).toHaveValue("floor");
  await page.getByLabel("Непрозрачность плана").fill("0.3");
  await floors.selectOption("upper");
  await expect(page.locator(".maplibregl-marker .ws-sector")).toHaveCount(0);
  await floors.selectOption("floor");
  await page.locator(".ws-map-list button").click();
  await page
    .getByRole("button", { name: "Открыть план · Первый этаж" })
    .click();
  await page
    .getByRole("button", { name: "Настроить привязку к карте" })
    .click();
  await page.getByLabel("Способ привязки").selectOption("pair");
  const image = page.locator(".ws-reference .ws-plan img");
  await image.click({ position: { x: 20, y: 40 } });
  await page.getByText("Координаты ориентира B", { exact: true }).click();
  await page.getByLabel("X на плане").fill("0.75");
  await page.getByLabel("Y на плане").fill("0.85");
  await page.getByLabel("Широта").fill("43.7301");
  await page.getByLabel("Долгота").fill("7.4002");
  await page.getByRole("button", { name: "Предпросмотр" }).click();
  await expect(
    page.getByRole("heading", { name: "Проверка привязки" }),
  ).toBeVisible();
});

test("Enrollment moves focus to the requested code and preserves the destination", async ({
  page,
}) => {
  await open(page, "login");
  await page.setViewportSize({ width: 320, height: 680 });
  await checkAccessibility(page, ".ws-login");
  await page.getByLabel("Имя пользователя").fill("installer");
  await page.getByLabel("Пароль", { exact: true }).fill("example");
  await page.getByRole("button", { name: "Войти" }).click();
  const code = page.getByLabel("Код первичной привязки");
  await expect(code).toBeFocused();
  await code.fill("example");
  await code.press("Enter");
  await expect(page.getByRole("heading", { name: "Мониторинг" })).toBeVisible();
});

test("Operator filters the feed and returns to the same visible event on a phone", async ({
  page,
}) => {
  await page.setViewportSize({ width: 320, height: 640 });
  await open(page, "monitor");
  await page.getByRole("link", { name: "К тревогам" }).press("Enter");
  const heading = page.getByRole("heading", { name: "Тревоги", exact: true });
  await expect(heading).toBeFocused();
  const filter = page.getByLabel("Состояние тревог в ленте");
  await filter.selectOption("closed");
  await expect(page.getByText("В этом состоянии тревог нет.")).toBeVisible();
  await filter.selectOption("new");
  const feed = page.getByRole("complementary", { name: "Тревоги" });
  const times = await feed
    .locator("time")
    .evaluateAll((items) => items.map((item) => item.dateTime));
  expect(times).toHaveLength(4);
  expect(times).toEqual([...times].sort().reverse());
  const event = feed.getByRole("button", { name: /Движение у ворот/ });
  await event.press("Enter");
  await expect(page.locator(".ws-player h2")).toBeFocused();
  await page.getByRole("button", { name: "Архив", exact: true }).press("Enter");
  await expect(
    page.getByRole("heading", { name: "Обработка тревоги" }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Эфир", exact: true }).click();
  await expect(page.locator(".ws-player")).toContainText("Эфир подключён");
  await expect(
    page.getByRole("heading", { name: "Обработка тревоги" }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Архив", exact: true }).click();
  await expect(page.locator(".ws-player")).toContainText("Кадр записи получен");
  await page
    .getByRole("button", { name: "К выбранной тревоге" })
    .press("Enter");
  await expect(event).toBeFocused();
  await expect(event).toBeInViewport();
  await expect(filter).toHaveValue("new");
  await expect(
    page.getByRole("button", { name: "Повторить", exact: true }),
  ).toHaveCount(0);
  await page.getByRole("link", { name: "Журнал тревог", exact: true }).click();
  await expect(
    page.getByRole("link", { name: "Журнал тревог", exact: true }),
  ).toHaveAttribute("aria-current", "page");
  await expect(page).toHaveTitle("Журнал тревог | Bacuri VMS");
});

test("Session lookup can recover from an outage without entering credentials again", async ({
  page,
}) => {
  await open(page, "session-offline");
  await expect(
    page.getByRole("heading", { name: "Площадка недоступна" }),
  ).toBeVisible();
  await expect(page.getByLabel("Пароль")).toHaveCount(0);
  await page
    .getByRole("button", { name: "Повторить подключение" })
    .press("Enter");
  await expect(page.getByRole("heading", { name: "Мониторинг" })).toBeVisible();
});
