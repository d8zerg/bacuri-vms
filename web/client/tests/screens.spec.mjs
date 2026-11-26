import { test, expect, checkAccessibility } from "./browser.mjs";
import ids from "../stories/fixture-ids.json" with { type: "json" };
async function open(page, story) {
  await page.goto("/iframe.html?viewMode=story&id=workspace--" + story);
  await expect(
    page.getByRole("main").getByRole("heading", { level: 1 }),
  ).toBeAttached();
}

async function expectRouteName(page, name) {
  await expect(page).toHaveTitle(name + " | Bacuri VMS");
  const heading = page
    .getByRole("main")
    .getByRole("heading", { name, level: 1, exact: true });
  await expect(heading).toHaveCSS("position", "absolute");
  await expect(heading).toHaveCSS("clip-path", "inset(50%)");
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
  await page.getByRole("link", { name: "Журнал действий" }).click();
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
  await expect(links).toHaveCount(6);
  await expect(nav.getByRole("link", { name: "Расследования" })).toBeVisible();
  for (const width of [
    320, 360, 390, 449, 480, 481, 600, 752, 753, 768, 1041, 1199, 1200, 1280,
    1440,
  ]) {
    await page.setViewportSize({ width, height: 900 });
    await expect(page.getByRole("button", { name: "К тревогам" })).toHaveCount(
      width <= 752 ? 1 : 0,
    );
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
    if (width < 1200)
      expect(
        Math.abs(
          switcher.y + switcher.height / 2 - (brand.y + brand.height / 2),
        ),
      ).toBeLessThan(1);
    expect(Math.abs(logout.y - switcher.y)).toBeLessThan(1);
    await expect(
      nav.getByRole("link", { name: "Журнал действий", exact: true }),
    ).toHaveText(width <= 480 ? "Журнал" : "Журнал действий", {
      useInnerText: true,
    });
    await expect(
      nav.getByRole("link", { name: "Карты и планы", exact: true }),
    ).toHaveText(width <= 480 ? "Карты" : "Карты и планы", {
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
          x: box.left,
          y: box.top,
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
      if (width < 1200) {
        expect(Math.abs(item.iconY - items[0].iconY)).toBeLessThan(1);
        expect(Math.abs(item.labelY - items[0].labelY)).toBeLessThan(1);
      } else {
        expect(item.x).toBe(items[0].x);
        const index = items.indexOf(item);
        if (index > 0)
          expect(item.y).toBeGreaterThanOrEqual(
            items[index - 1].y + items[index - 1].height,
          );
      }
      expect(item.inset).toBeGreaterThanOrEqual(3);
      if (width <= 752) {
        expect(Math.min(item.width, item.height)).toBeGreaterThanOrEqual(44);
      }
    }
  }
});
test("Archive search preserves the bookmark when a case is saved and reopened", async ({
  page,
}) => {
  await open(page, "investigations");
  await page.locator(".inv-points input").first().check();
  await page.getByText("Фильтры эпизода").click();
  await page.locator("form[role=search] button[type=submit]").click();
  await expect(page.locator(".inv-track")).toHaveCount(1);
  await page.getByRole("button", { name: "Следующий кадр" }).click();
  await page
    .getByRole("button", {
      name: "Добавить закладку и интервалы в материалы дела",
    })
    .click();
  await page.getByRole("button", { name: "Материалы (1)" }).click();
  await page.getByRole("button", { name: /Новое дело/ }).click();
  await page.getByLabel("Номер дела").fill("DOCK-1");
  await page.getByLabel("Основание защиты").fill("Сверка погрузки");
  await page.getByRole("button", { name: "Сохранить дело" }).click();
  await page.getByRole("button", { name: /DOCK-1/ }).click();
  await expect(page.getByLabel("Номер дела")).toHaveValue("DOCK-1");
  await expect(page.locator(".inv-material")).toContainText("Погрузочные");
  await page.locator(".inv-material button").nth(1).click();
  await expect(page.locator(".inv-track")).toHaveCount(1);
});
test("Rule editor previews the draft and saves its version", async ({
  page,
}) => {
  await open(page, "video-rules");
  await page.getByRole("button", { name: "Добавить область" }).first().click();
  await expect(
    page.getByRole("button", { name: "Удалить область" }),
  ).toHaveCount(1);
  await page.getByLabel("Пересечение линии").check();
  const frame = page.getByRole("img", { name: "Линия пересечения на кадре" });
  await frame.scrollIntoViewIfNeeded();
  const box = await frame.boundingBox();
  await page.mouse.move(box.x + box.width / 2, box.y + 2);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width / 4, box.y + box.height / 4);
  await page.mouse.up();
  await page.getByText("Координаты линии").click();
  await expect(page.getByLabel("X₁, %")).toHaveValue("25");
  await expect(page.getByLabel("Y₁, %")).toHaveValue("25");
  await page.getByLabel("X₂, %").fill("75");
  await expect(frame.locator("line")).toHaveAttribute("x2", "75");
  await page.getByText(/Расписание ·/).click();
  await page.getByRole("button", { name: "Добавить период" }).click();
  await expect(page.getByLabel("День недели")).toHaveCount(1);
  await page.getByText("Проверка на записи", { exact: true }).click();
  await page.getByRole("button", { name: "Проверить на записи" }).click();
  await expect(page.getByText(/Срабатываний · 1/)).toBeVisible();
  await page.getByRole("button", { name: "Сохранить правило" }).click();
  await expect(
    page.getByRole("status").filter({ hasText: /сохран/i }),
  ).toBeVisible();
});
test("Desktop sidebar preserves its compact mode across navigation, reload and resizing", async ({
  page,
}, info) => {
  await page.setViewportSize({ width: 1440, height: 900 });
  await open(page, "monitor");
  const nav = page.getByRole("navigation", { name: "Рабочее место" });
  const main = page.getByRole("main");
  const collapse = page.getByRole("button", { name: "Свернуть меню" });
  const expand = page.getByRole("button", { name: "Развернуть меню" });
  const audit = nav.getByRole("link", { name: "Журнал действий" });
  const expanded = await main.boundingBox();
  const menu = await nav.boundingBox();
  expect(menu.x + menu.width).toBeLessThanOrEqual(expanded.x);
  await expect(collapse).toHaveAttribute("aria-expanded", "true");
  await collapse.press("Space");
  await expect(expand).toBeFocused();
  await expect(expand).toHaveAttribute("aria-expanded", "false");
  await expect(audit.locator(":scope > span")).toBeHidden();
  await expect(audit).toHaveAttribute("title", "Журнал действий");
  expect((await main.boundingBox()).width).toBeGreaterThan(expanded.width);
  await audit.press("Enter");
  await expect(audit).toHaveAttribute("aria-current", "page");
  await expectRouteName(page, "Журнал действий");
  await expect(expand).toBeVisible();
  await info.attach("sidebar-collapsed", {
    body: await page.screenshot(),
    contentType: "image/png",
  });
  await page.reload();
  await expect(expand).toBeVisible();
  await page.setViewportSize({ width: 390, height: 844 });
  await expect(expand).toBeHidden();
  await expect(audit.locator(":scope > span")).toBeVisible();
  await page.setViewportSize({ width: 1200, height: 900 });
  await expand.press("Enter");
  await expect(collapse).toBeFocused();
  await expect(audit.locator(":scope > span")).toBeVisible();
});
test("Desktop subsections belong to the active workspace section and operate its content", async ({
  page,
}) => {
  await page.setViewportSize({ width: 1440, height: 900 });
  await open(page, "plan");
  const nav = page.getByRole("navigation", { name: "Рабочее место" });
  function section(name) {
    return nav
      .locator(".ws-nav-section")
      .filter({ has: page.getByRole("link", { name, exact: true }) });
  }
  const maps = section("Карты и планы");
  await expectRouteName(page, "Карты и планы");
  await maps
    .getByRole("button", { name: "Карта региона", exact: true })
    .click();
  await expect(
    maps.getByRole("button", { name: "Карта региона", exact: true }),
  ).toHaveAttribute("aria-pressed", "true");
  await maps.getByRole("button", { name: "Планы", exact: true }).press("Enter");
  await expect(page.locator(".ws-plan img")).toBeVisible();
  await section("Мониторинг").getByRole("link").click();
  await expect(maps.getByRole("button")).toHaveCount(0);
  await section("Мониторинг")
    .getByRole("button", { name: "Архив", exact: true })
    .click();
  await expect(page.getByRole("region", { name: "Видеоплеер" })).toContainText(
    "Основной поток",
  );
  await expect(
    page.getByRole("heading", { name: "Тревоги", exact: true }),
  ).toBeInViewport();
  await section("Журнал действий").getByRole("link").click();
  await expect(section("Журнал действий").getByRole("button")).toHaveCount(0);
  const filters = page.getByRole("search", { name: "Поиск и фильтры журнала" });
  await expect(
    filters.getByRole("searchbox", { name: "Результат или основание" }),
  ).toBeVisible();
  await expect(filters.getByLabel("Идентификатор тревоги")).toBeVisible();
  await expect(
    page.getByRole("region", { name: "Результаты поиска" }),
  ).toBeVisible();
  const cameras = section("Камеры");
  await cameras.getByRole("link").click();
  await expectRouteName(page, "Каталог подключённых устройств");
  await expect(
    page.getByRole("search", { name: "Фильтры каталога" }),
  ).toBeInViewport();
  await expect(
    cameras.getByRole("group", { name: "Способы подключения" }),
  ).toHaveCount(0);
  await cameras
    .getByRole("button", { name: "Подключение", exact: true })
    .click();
  await cameras
    .getByRole("button", { name: "Поиск ONVIF и замена камеры" })
    .press("Enter");
  await expect(
    page.getByRole("button", { name: "Найти камеры ONVIF" }),
  ).toBeVisible();
  await cameras
    .getByRole("button", { name: "Массовое подключение" })
    .press("Space");
  await expectRouteName(page, "Массовое подключение");
  await expect(
    page.getByRole("button", { name: "Обнаружить устройства" }),
  ).toBeInViewport();
  await cameras
    .getByRole("button", { name: "Единичное подключение", exact: true })
    .click();
  await expectRouteName(page, "Подключение камеры");
  await expect(page.getByLabel("Название", { exact: true })).toBeVisible();
  await expect(cameras.getByRole("group")).toHaveCount(2);
  await section("Доступ").getByRole("link").click();
  await expectRouteName(page, "Доступ и пользователи");
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
test("Camera batch retries an interrupted channel and shows the acknowledged revision", async ({
  page,
}) => {
  const path = "**/api/v1/sites/*/camera-batches";
  const device = {
    endpoint_id: "test-camera",
    channel_token: "",
    manufacturer: "Test",
    model: "Encoder",
    serial: "stand",
    firmware: "1",
    events: false,
    ptz: false,
  };
  let receipt;
  let savedTemplate;
  let writes = 0;
  await page.route(path, (route) => route.fulfill({ json: [] }));
  await page.route(path + "/**", async (route) => {
    const parts = new URL(route.request().url()).pathname.split("/");
    const method = route.request().method();
    if (method === "PUT") {
      const body = route.request().postDataJSON();
      const settings = {
        name: body.name,
        main_url: "rtsps://camera.test/1",
        secondary_url: "rtsps://camera.test/2",
        point_id: "",
      };
      receipt = {
        item_id: parts.at(-1),
        state: "prepared",
        expected_revision: null,
        revision: null,
        after: settings,
        before: null,
        current: null,
        camera: device,
        brightness: 50,
        image: { min: 0, max: 100, value: 40 },
        template: savedTemplate,
      };
    } else if (method === "POST") {
      if (parts.at(-1) === "rollback")
        receipt = { ...receipt, state: "rolled_back", current: null };
      else if (++writes === 1)
        receipt = { ...receipt, state: "failed", error: "camera_unavailable" };
      else
        receipt = {
          ...receipt,
          state: "saved",
          error: undefined,
          revision: "1",
          current: {
            ...receipt.after,
            id: "camera",
            point_id: "point",
            binding_revision: "1",
            desired_revision: "1",
            applied_revision: "1",
            state: "applied",
          },
        };
    }
    await route.fulfill({
      json: method === "GET" ? (receipt ? [receipt] : []) : receipt,
    });
  });
  await page.route("**/api/v1/sites/*/camera-templates", (route) =>
    route.fulfill({ json: savedTemplate ? [savedTemplate] : [] }),
  );
  await page.route("**/api/v1/sites/*/camera-templates/*", async (route) => {
    savedTemplate = {
      ...route.request().postDataJSON(),
      id: "template",
      revision: "1",
    };
    await route.fulfill({ json: savedTemplate });
  });
  await open(page, "installer");
  await page.getByRole("button", { name: "Массовое подключение" }).click();
  await expect(
    page.getByRole("button", { name: "Массовое подключение" }),
  ).toBeFocused();
  await page.getByRole("button", { name: "Обнаружить устройства" }).click();
  await page.getByLabel("Пользователь камеры").fill("viewer");
  await page.getByLabel("Пароль камеры").fill("demo");
  await page.getByRole("button", { name: "Получить каналы" }).click();
  await page.getByText("Настройки потоков и яркости", { exact: true }).click();
  await page.getByLabel("Яркость по шкале камеры").fill("50");
  await page.getByText("Сохранить настройки шаблона", { exact: true }).click();
  await page.getByLabel("Название шаблона").fill("Вход");
  await page.getByRole("button", { name: "Сохранить шаблон" }).click();
  await expect(page.getByText("Шаблон сохранён.")).toBeVisible();
  await page.getByRole("button", { name: "Сравнить конфигурации" }).click();
  const channel = page.locator(".ws-batch-receipts .ws-batch-channel");
  const summary = channel.locator("summary");
  await expect(summary).toContainText("Готов к применению");
  await expect(page.getByLabel("Пароль камеры")).toBeHidden();
  const stream = channel.getByText("rtsps://camera.test/1", { exact: true });
  await expect(stream).toBeHidden();
  await summary.focus();
  await summary.press("Enter");
  await expect(stream).toBeVisible();
  await expect(channel.getByText("Задано 50")).toBeVisible();
  await summary.press("Space");
  await expect(stream).toBeHidden();
  await page.getByRole("button", { name: "Применить партию" }).click();
  await expect(summary).toContainText("Не применено");
  await expect(summary).toContainText("Камера не ответила вовремя.");
  await page.getByRole("button", { name: "Повторить незавершённые" }).click();
  await expect(summary).toContainText("Применено рекордером");
  await expect(
    page.getByRole("button", { name: "Повторить незавершённые" }),
  ).toHaveCount(0);
  await page.getByRole("button", { name: "Обновить результаты" }).click();
  await page.setViewportSize({ width: 320, height: 900 });
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(
    320,
  );
  await checkAccessibility(page, ".ws-shell");
  await summary.press("Enter");
  await expect(
    channel.locator("dt", { hasText: "Версия рекордера" }).locator("+ dd"),
  ).toHaveText("1");
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(
    320,
  );
  await page.getByRole("button", { name: "Откатить канал" }).click();
  await expect(summary).toContainText("Откат выполнен");
  await expect(
    page.getByRole("button", { name: "Закрыть", exact: true }),
  ).toHaveCount(0);
  await page
    .getByRole("button", { name: "Единичное подключение", exact: true })
    .click();
  await expectRouteName(page, "Подключение камеры");
  await expect(page.getByLabel("Название", { exact: true })).toBeVisible();
  await page.getByRole("button", { name: "Массовое подключение" }).click();
  await expect(channel).toHaveCount(1);
  await expectRouteName(page, "Массовое подключение");
  await expect(
    page.getByRole("button", { name: "Массовое подключение" }),
  ).toBeFocused();
});
test("Device catalog searches channels and combines recorder status with the query", async ({
  page,
}) => {
  await open(page, "catalog");
  const catalog = page.getByRole("region", { name: "Подключённые устройства" });
  const filters = catalog.getByRole("search");
  const channel = catalog.locator("summary");
  await expect(channel).toHaveCount(3);
  await expect(
    catalog.getByText("Устройств: 2", { exact: true }),
  ).toBeVisible();
  await filters.getByRole("searchbox").fill("wH-204");
  await expect(
    catalog.getByText("Устройств: 1 из 2", { exact: true }),
  ).toBeVisible();
  await expect(channel).toHaveCount(2);
  await filters
    .getByRole("combobox", { name: "Конфигурация канала" })
    .selectOption("pending");
  await expect(channel).toHaveCount(1);
  await expect(channel).toContainText("Склад");
  await filters.getByLabel("Связь с камерой").selectOption("online");
  await expect(channel).toHaveCount(0);
  await filters.getByLabel("Связь с камерой").selectOption("offline");
  await expect(channel).toHaveCount(1);
  await filters.getByRole("searchbox").fill("несуществующий");
  await expect(channel).toHaveCount(0);
  await filters.getByRole("button", { name: "Сбросить" }).click();
  await expect(channel).toHaveCount(3);
  await channel.first().click();
  await expect(
    catalog.locator("details[open]").getByText("Версия рекордера"),
  ).toBeVisible();
  await page.setViewportSize({ width: 320, height: 800 });
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(
    320,
  );
  await checkAccessibility(page, ".ws-shell");
  await page.getByRole("button", { name: "Подключение", exact: true }).click();
  await page.getByRole("button", { name: "Массовое подключение" }).click();
  await expect(
    page.getByRole("region", { name: "Подключённые устройства" }),
  ).toHaveCount(0);
  await page
    .getByRole("button", { name: "Каталог подключённых устройств" })
    .click();
  await expect(channel).toHaveCount(3);
});
test("A reopened batch keeps channels of identical encoder models separate", async ({
  page,
}) => {
  function saved(serial, token, name, desired, state) {
    const settings = {
      name,
      main_url: "rtsps://camera.test/main",
      secondary_url: "rtsps://camera.test/sub",
      point_id: "point",
    };
    return {
      item_id: serial + token,
      camera: {
        endpoint_id: serial,
        serial,
        model: "Encoder",
        channel_token: token,
        manufacturer: "",
        firmware: "",
        ptz: false,
        events: false,
      },
      after: settings,
      before: settings,
      brightness: null,
      image: null,
      expected_revision: "1",
      revision: "2",
      state: "saved",
      current: {
        ...settings,
        id: serial + token,
        desired_revision: desired,
        binding_revision: "1",
        applied_revision: "1",
        state,
      },
    };
  }
  const rows = [
    saved("A", "channel10", "Склад", "3", "applied"),
    saved("B", "channel2", "Въезд", "2", "rejected"),
    saved("A", "channel2", "Encoder · channel2", "2", "applying"),
  ];
  const path = "**/api/v1/sites/*/camera-batches";
  await page.route(path, (route) => route.fulfill({ json: [] }));
  await page.route(path + "/*", (route) => route.fulfill({ json: rows }));
  await page.route("**/api/v1/sites/*/camera-templates", (route) =>
    route.fulfill({ json: [] }),
  );
  await open(page, "installer");
  await page.getByRole("button", { name: "Массовое подключение" }).click();
  const first = page
    .locator(".ws-batch-device")
    .filter({ has: page.getByRole("heading", { name: "Encoder · A" }) });
  const second = page
    .locator(".ws-batch-device")
    .filter({ has: page.getByRole("heading", { name: "Encoder · B" }) });
  await expect(first.locator("summary strong")).toHaveText([
    "Канал 2",
    "Склад",
  ]);
  await expect(first.locator("summary").first()).toContainText(
    "Ожидает рекордера",
  );
  await expect(first.locator("summary").last()).toContainText(
    "Есть более новая версия",
  );
  await expect(second.locator("summary")).toContainText("Рекордер отклонил");
  await expect(second.locator("summary")).toContainText("Въезд");
  await expect(first.locator("details[open]")).toHaveCount(0);
});
test("Administrator selects discovered profiles and replaces an existing binding", async ({
  page,
}) => {
  await open(page, "installer");
  await page
    .getByRole("button", { name: "Поиск ONVIF и замена камеры" })
    .click();
  await page.getByLabel("Точка наблюдения").selectOption(ids.previousCamera);
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
test("Archive scrubbing keeps the current player and alert rows until release, and modes stop each other's video", async ({
  page,
}) => {
  await open(page, "monitor");
  const watch = page.locator(".ws-watch");
  const slider = watch.getByRole("slider", { name: "Позиция в архиве" });
  const video = watch.locator("video");
  await expect(watch.getByText("Эфир подключён")).toBeVisible();
  await expect(slider).toHaveCount(0);
  const live = await video.elementHandle();
  const rows = await page.locator(".ws-alerts li").elementHandles();
  await page.getByRole("button", { name: "Архив", exact: true }).click();
  await expect(watch.getByText("Кадр записи получен")).toBeVisible();
  expect(
    await live.evaluate(
      (el) => !el.isConnected && el.paused && !el.getAttribute("src"),
    ),
  ).toBe(true);
  await expect(slider).toBeVisible();
  const oldSrc = await video.getAttribute("src");
  const transport = watch.locator(".ws-player time");
  const before = await transport.getAttribute("datetime");
  let requests = 0;
  page.on("request", (request) => {
    if (new URL(request.url()).pathname.endsWith(".webm")) requests++;
  });
  await slider.scrollIntoViewIfNeeded();
  const box = await slider.boundingBox();
  const move = (fraction, steps = 1) =>
    page.mouse.move(box.x + box.width * fraction, box.y + box.height / 2, {
      steps,
    });
  await move(0.5);
  await page.mouse.down();
  await move(0.37, 6);
  await expect(slider).toHaveAttribute("aria-valuetext", /Пропуск записи/);
  await move(0.2, 8);
  const destination = new Date(Number(await slider.inputValue())).toISOString();
  expect(destination).not.toBe(before);
  await expect(transport).toHaveAttribute("datetime", before);
  await expect(video).toHaveAttribute("src", oldSrc);
  await expect(video).toBeVisible();
  expect(requests).toBe(0);
  await page.mouse.up();
  await expect(transport).toHaveAttribute("datetime", destination);
  await expect(watch.getByText("Кадр записи получен")).toBeVisible();
  expect(requests).toBe(1);
  await slider.focus();
  await page.keyboard.down("ArrowRight");
  await page.keyboard.down("ArrowRight");
  await expect(transport).toHaveAttribute("datetime", destination);
  await page.keyboard.up("ArrowRight");
  await expect(transport).toHaveAttribute(
    "datetime",
    new Date(Date.parse(destination) + 2000).toISOString(),
  );
  await expect(watch.getByText("Кадр записи получен")).toBeVisible();
  expect(requests).toBe(2);
  for (const row of rows)
    expect(await row.evaluate((el) => el.isConnected)).toBe(true);
  const archive = await video.elementHandle();
  await page.getByRole("button", { name: "Эфир", exact: true }).click();
  await expect(watch.getByText("Эфир подключён")).toBeVisible();
  await expect(slider).toHaveCount(0);
  await expect(video).toHaveCount(1);
  expect(
    await archive.evaluate(
      (el) => !el.isConnected && el.paused && !el.getAttribute("src"),
    ),
  ).toBe(true);
});
test("Operator opens alert, sees the gap, returns focus and handles an empty feed", async ({
  page,
}) => {
  await open(page, "monitor");
  await page.getByRole("button", { name: /14:22:12/ }).press("Enter");
  await expect(page.locator(".ws-overlay")).toContainText("Пропуск записи");
  await expect(page.locator("video")).toBeHidden();
  await page
    .getByRole("button", { name: "К выбранной тревоге" })
    .press("Enter");
  await expect(page.getByRole("button", { name: /14:22:12/ })).toBeFocused();
  await page.getByRole("link", { name: "Камеры" }).click();
  await expect(
    page.getByRole("search", { name: "Фильтры каталога" }),
  ).toBeVisible();
  await page.getByRole("link", { name: "Мониторинг" }).click();
  await expect(page.getByRole("button", { name: /14:22:12/ })).toBeFocused();
  await page.getByRole("button", { name: /14:25:01/ }).click();
  await expect(page.locator(".ws-overlay")).toContainText(
    "добавляется в каталог",
  );
  await page.getByRole("button", { name: /14:21:22/ }).click();
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
    else await expect(page.locator(".ws-install")).toBeVisible();
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
        route === "monitor" ? ".ws-player h2" : ".bc-camera h2",
        ".ws-panel",
        'nav [aria-current="page"]',
        route === "monitor" ? ".ws-monitor" : ".ws-install",
      ]);
      expect(actual).toMatchObject({
        ...colors,
        font: "Inter, system-ui, -apple-system, sans-serif",
        columns: width >= 768 ? 2 : 1,
      });
      expect(actual.titleSize).toBe("17px");
      await expectRouteName(
        page,
        route === "monitor" ? "Мониторинг" : "Подключение камеры",
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
      // Native video compositing can round a few high-contrast edge pixels differently.
      await expect(page).toHaveScreenshot(filename, {
        ...capture,
        ...(route === "monitor" ? { maxDiffPixels: 16 } : {}),
      });
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
      if (story === "plan") {
        const zoom = page.getByRole("radiogroup", { name: "Масштаб плана" });
        expect((await zoom.boundingBox()).width).toBeLessThan(160);
        for (const radio of await zoom.getByRole("radio").all()) {
          const height = await radio.evaluate(
            (el) => getComputedStyle(el).height,
          );
          expect(parseFloat(height)).toBeGreaterThanOrEqual(44);
        }
      }
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
  await panel.getByRole("button", { name: /14:22:12/ }).press("Enter");
  await expect(panel.locator(".ws-overlay")).toContainText(
    "Пропуск записи. Нет связи с камерой",
  );
  await expect(panel.locator("video")).toBeHidden();
  await panel.getByRole("button", { name: "Эфир", exact: true }).click();
  await expect(panel.getByText("Эфир подключён")).toBeVisible();
  await panel.getByRole("button", { name: "Архив", exact: true }).click();
  await expect(panel.locator("video")).toBeHidden();
  await panel.getByRole("button", { name: /Показать ещё/ }).click();
  await panel.getByRole("button", { name: /14:21:22/ }).press("Enter");
  await expect(panel.getByText("Кадр записи получен")).toBeVisible();
  await expect(panel.locator("video")).toHaveCount(1);
  await checkAccessibility(page, ".ws-shell");
  const zoom = page.getByRole("radiogroup", { name: "Масштаб плана" });
  await zoom.getByRole("radio", { name: "100%" }).focus();
  await page.keyboard.press("ArrowRight");
  await expect(zoom.getByRole("radio", { name: "200%" })).toBeChecked();
  await expect
    .poll(() =>
      page
        .locator(".ws-plan")
        .evaluate((el) =>
          Math.round(
            (el.getBoundingClientRect().width / el.parentElement.clientWidth) *
              100,
          ),
        ),
    )
    .toBe(200);
  await panel
    .getByRole("button", { name: "Открыть тревогу в мониторинге" })
    .click();
  await expect(page.locator(".ws-alert-work")).toBeVisible();
  await page.getByRole("link", { name: "Карты и планы" }).click();
  const row = panel.getByRole("button", { name: /14:21:22/ });
  await expect(row).toBeFocused();
  await expect(row).toHaveAttribute("aria-expanded", "true");
  await expect(zoom.getByRole("radio", { name: "200%" })).toBeChecked();
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
  await expect(x).toBeHidden();
  await page.getByText("Изменить положение точки", { exact: true }).click();
  await x.fill("0.42");
  await page.getByRole("button", { name: "Сохранить положение" }).click();
  await expect(page.getByText("Положение сохранено")).toBeVisible();
  const zoom = page.getByRole("radiogroup", { name: "Масштаб плана" });
  await zoom.getByRole("radio", { name: "200%" }).click();
  await expect(zoom.getByRole("radio", { name: "200%" })).toBeChecked();
  await page.getByRole("button", { name: "Эфир", exact: true }).click();
  await expectRouteName(page, "Мониторинг");
  await page.getByRole("link", { name: "Карты и планы" }).click();
  await expect(page.locator("#map-point-" + ids.dockPoint)).toBeFocused();
  await expect(x).toBeHidden();
  await page.getByText("Изменить положение точки", { exact: true }).click();
  await expect(x).toHaveValue("0.42");
  await expect(zoom.getByRole("radio", { name: "200%" })).toBeChecked();
  await page.getByText("Добавить план", { exact: true }).click();
  await page.getByLabel("Название плана").fill("Второй этаж");
  await page.getByLabel("Изображение PNG").setInputFiles("stories/floor.png");
  await page.getByRole("button", { name: "Загрузить план" }).click();
  await expect(
    page.getByRole("heading", { name: "Второй этаж" }),
  ).toBeVisible();
  await page.getByText("Изменить положение точки", { exact: true }).click();
  await page.locator(".ws-plan img").click({ position: { x: 75, y: 45 } });
  await page.getByRole("button", { name: "Сохранить положение" }).click();
  await expect(page.getByText("Положение сохранено")).toBeVisible();
  await page
    .getByRole("button", { name: "Карта региона", exact: true })
    .click();
  await expect(page.getByText("Карта региона не установлена")).toBeVisible();
});

test("Operator closes an alert, searches its history and controls delegated work", async ({
  page,
}) => {
  await open(page, "monitor");
  const feed = page.getByRole("complementary", { name: "Тревоги" });
  const movement = feed.getByRole("button", { name: /14:21:22/ });
  await expect(feed.getByText("Новая", { exact: true })).toHaveCount(4);
  await page.getByRole("link", { name: "Журнал действий" }).click();
  const results = page.getByRole("region", { name: "Результаты поиска" });
  await expect(results).toContainText("Действий пока нет.");
  await page.getByRole("link", { name: "Мониторинг" }).click();
  await movement.click();
  const panel = page.locator(".ws-alert-work");
  await panel.getByRole("button", { name: "Взять в работу" }).click();
  await expect(movement).toContainText("В работе");
  await expect(feed.getByText("Новая", { exact: true })).toHaveCount(3);
  await page.getByRole("link", { name: "Журнал действий" }).click();
  await expect(results.getByRole("listitem")).toHaveCount(1);
  await expect(results.getByRole("listitem")).toContainText(
    "Принятие в работу",
  );
  await page.getByRole("link", { name: "Мониторинг" }).click();
  await movement.click();
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
  await expect(movement).toContainText("Закрыта");
  await panel
    .getByText("Поручения сервисам на этой точке", { exact: true })
    .click();
  await panel.getByLabel("Имя сервиса").fill("Видеоанализ");
  await panel.getByRole("button", { name: "Выдать поручение" }).click();
  await expect(panel.getByLabel("Токен для передачи сервису")).toHaveValue(
    "test-credential",
  );
  await panel.getByRole("button", { name: "Отозвать" }).click();
  await expect(panel.getByText("Поручение отозвано")).toBeVisible();
  await checkAccessibility(page, ".ws-alert-work");
  await panel
    .getByRole("button", { name: "Открыть журнал этой тревоги" })
    .click();
  await expectRouteName(page, "Журнал действий");
  const query = page.getByLabel("Результат или основание");
  await query.fill("Другое основание");
  await query.press("Enter");
  await expect(page.getByText("Записи не найдены")).toBeVisible();
  await page.getByRole("button", { name: "Сбросить" }).click();
  await expect(query).toHaveValue("");
  await page.getByRole("button", { name: "Следующая страница" }).click();
  await expect(page.locator(".ws-history li")).toHaveCount(1);
  await page.getByRole("button", { name: "Предыдущая страница" }).click();
  await expect(page.locator(".ws-history li")).toHaveCount(2);
  await query.fill("Запись проверена");
  const submit = page
    .getByRole("search")
    .getByRole("button", { name: "Найти", exact: true });
  await expect(submit).toHaveText("");
  await expect(submit.locator("svg")).toBeVisible();
  await submit.click();
  await expect(page.locator(".ws-history li")).toHaveCount(1);
  await checkAccessibility(page, ".ws-alert-work");
  await page
    .getByRole("button", { name: "Открыть тревогу", exact: true })
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
  await area.getByLabel("Файл обновления").setInputFiles({
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
  await area.getByRole("button", { name: "Свернуть выбор регионов" }).click();
  await expect(catalog).toHaveCount(0);
  await expect(
    area.getByRole("button", { name: "Обновить установленные" }),
  ).toBeFocused();
});

test("Administrator references a floor offline and resolves an existing camera position", async ({
  page,
}) => {
  await open(page, "plan");
  const editPlan = page.getByRole("button", { name: "Привязать план к карте" });
  await editPlan.click();
  const zoom = page.getByRole("radiogroup", { name: "Масштаб изображения" });
  await zoom.getByRole("radio", { name: "400%" }).check();
  await expect(zoom.getByRole("radio", { name: "400%" })).toBeChecked();
  await page
    .getByRole("button", { name: "Вернуться к плану без сохранения" })
    .click();
  await expect(editPlan).toBeFocused();
  await editPlan.click();
  await expect(zoom.getByRole("radio", { name: "100%" })).toBeChecked();
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
  } else await floors.selectOption(ids.floorPlan);
  await expect(floors).toHaveValue(ids.floorPlan);
  await page.getByLabel("Непрозрачность плана").fill("0.3");
  await floors.selectOption(ids.upperPlan);
  await expect(page.locator(".maplibregl-marker .ws-sector")).toHaveCount(0);
  await floors.selectOption(ids.floorPlan);
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
  await expectRouteName(page, "Мониторинг");
});

test("Operator filters the feed and returns to the same visible event on a phone", async ({
  page,
}) => {
  await page.setViewportSize({ width: 320, height: 640 });
  await open(page, "monitor");
  const heading = page.getByRole("heading", { name: "Тревоги", exact: true });
  await expect(heading).not.toBeInViewport();
  await page.getByRole("button", { name: "К тревогам" }).press("Enter");
  await expect(heading).toBeFocused();
  await expect(heading).toBeInViewport();
  const filter = page.getByLabel("Состояние тревог в ленте");
  const closed = filter.getByRole("button", { name: "Закрытые", exact: true });
  const progress = filter.getByRole("button", {
    name: "В работе",
    exact: true,
  });
  const fresh = filter.getByRole("button", { name: "Новые", exact: true });
  await expect(filter.getByRole("button")).toHaveCount(3);
  for (const width of [1440, 1280, 1200, 1199, 1024, 768, 753, 752, 390, 320]) {
    await page.setViewportSize({ width, height: 640 });
    const geometry = await filter.evaluate((group) => {
      const panel = group.closest('[role="search"]').getBoundingClientRect();
      const rect = group.getBoundingClientRect();
      const buttons = [...group.querySelectorAll("button")].map((button) => {
        const range = document.createRange();
        range.selectNodeContents(button);
        const text = range.getBoundingClientRect();
        const { y, width, height, left, right } =
          button.getBoundingClientRect();
        return {
          y,
          width,
          height,
          lines: range.getClientRects().length,
          fits: text.left >= left && text.right <= right,
        };
      });
      return {
        overflow: group.scrollWidth - group.clientWidth,
        left: rect.left - panel.left,
        right: panel.right - rect.right,
        buttons,
      };
    });
    expect(geometry.overflow).toBe(0);
    expect(Math.abs(geometry.left)).toBeLessThan(1);
    expect(Math.abs(geometry.right)).toBeLessThan(1);
    for (const segment of geometry.buttons) {
      expect(segment.y).toBe(geometry.buttons[0].y);
      expect(Math.abs(segment.width - geometry.buttons[0].width)).toBeLessThan(
        1,
      );
      expect(segment.height).toBeGreaterThanOrEqual(44);
      expect(segment.lines).toBe(1);
      expect(segment.fits).toBe(true);
    }
  }
  await progress.press("Space");
  await expect(progress).toHaveAttribute("aria-pressed", "true");
  await closed.press("Enter");
  await expect(progress).toHaveAttribute("aria-pressed", "false");
  await expect(page.getByText("В этом состоянии тревог нет.")).toBeVisible();
  await closed.press("Enter");
  await expect(filter.getByRole("button", { pressed: true })).toHaveCount(0);
  await expect(page.locator(".ws-alerts li")).toHaveCount(4);
  await fresh.click();
  const feed = page.getByRole("complementary", { name: "Тревоги" });
  const times = await feed
    .locator("time")
    .evaluateAll((items) => items.map((item) => item.dateTime));
  expect(times).toHaveLength(4);
  expect(times).toEqual([...times].sort().reverse());
  const search = feed.getByRole("searchbox");
  // An alert is found by a fragment of its identifier in any letter case.
  await search.fill(ids.alerts[0].slice(0, 13).toUpperCase());
  await expect(feed.getByRole("listitem")).toHaveCount(1);
  const event = feed.getByRole("button", { name: /14:21:22/ });
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
  await search.fill(ids.alerts[3].slice(-4));
  await expect(feed.getByRole("listitem")).toHaveCount(1);
  await page
    .getByRole("button", { name: "К выбранной тревоге" })
    .press("Enter");
  await expect(event).toBeFocused();
  await expect(event).toBeInViewport();
  await expect(fresh).toHaveAttribute("aria-pressed", "true");
  await expect(search).toHaveValue("");
  await expect(
    page.getByRole("button", { name: "Повторить", exact: true }),
  ).toHaveCount(0);
  await page
    .getByRole("link", { name: "Журнал действий", exact: true })
    .click();
  await expect(
    page.getByRole("link", { name: "Журнал действий", exact: true }),
  ).toHaveAttribute("aria-current", "page");
  await expect(page).toHaveTitle("Журнал действий | Bacuri VMS");
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
  await expectRouteName(page, "Мониторинг");
});

test("PTZ panel sends scoped keyboard and equipment commands and stays with fullscreen video", async ({
  page,
}) => {
  await open(page, "intercom");
  const desk = page.getByRole("group", { name: "Управление PTZ", exact: true });
  const press = (name) => desk.getByRole("button", { name, exact: true });
  await press("Вверх").focus();
  await page.keyboard.press("ArrowRight");
  await expect(desk.getByRole("status")).toContainText("Камера подтвердила");
  await page.keyboard.press("Escape");
  await expect(desk.getByRole("status")).toContainText("остановки");
  const left = press("Влево");
  const box = await left.boundingBox();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await expect(left).toHaveAttribute("data-held", "true");
  await page.mouse.up();
  await expect(left).toHaveAttribute("data-held", "false");
  await expect(desk.getByRole("status")).toContainText("остановки");
  await press("Фокус ближе").click();
  await press("Перейти к позиции Ворота").click();
  await press("Запустить патруль Периметр").click();
  await press("Остановить патруль Периметр").click();
  await press("Оборудование и входы").click();
  await press("Включить подсветку").click();
  await press("Отключить выход 1").click();
  await expect(desk.getByText("Вход 1")).toBeVisible();
  await press("Журнал команд").click();
  for (const label of [
    "Вправо",
    "Остановить",
    "Фокус ближе",
    "Перейти к позиции gate",
    "Запустить патруль perimeter",
    "Включить подсветку",
    "Отключить выход 1",
  ])
    await expect(desk.getByText(label, { exact: true }).first()).toBeVisible();
  await page.getByRole("button", { name: "На весь экран" }).click();
  await expect(page.locator(":fullscreen .camera-console")).toBeVisible();
  await page.getByRole("button", { name: "Выйти из полного экрана" }).click();
  await expect(page.locator(":fullscreen")).toHaveCount(0);
  await press("Поворот, объектив и позиции").click();
  for (const width of [1440, 390, 320]) {
    await page.setViewportSize({ width, height: 900 });
    await expect(desk).toBeVisible();
    expect(await desk.evaluate((el) => el.scrollWidth <= el.clientWidth)).toBe(
      true,
    );
    for (const key of await desk.getByRole("button").all()) {
      const box = await key.boundingBox();
      expect(Math.round(box.width)).toBeGreaterThanOrEqual(44);
      expect(Math.round(box.height)).toBeGreaterThanOrEqual(44);
    }
  }
  await checkAccessibility(page, ".camera-console");
});
test("Camera controls retain an unknown result and separate audio directions", async ({
  page,
}) => {
  await open(page, "camera-controls");

  await expect(
    page.getByRole("button", { name: "Перейти к позиции Ворота" }),
  ).toBeVisible();
  await page
    .getByRole("button", { name: "Журнал команд", exact: true })
    .click();
  await expect(page.getByText(/Исход неизвестен/)).toBeVisible();
  await expect(page.getByRole("button", { name: "Говорить" })).toHaveCount(0);
  await page.getByRole("button", { name: "Записать звук" }).click();
  await expect(
    page.getByRole("button", { name: "Остановить запись звука" }),
  ).toHaveAttribute("aria-pressed", "true");
  await page.getByRole("button", { name: "Остановить запись звука" }).click();
  await expect(
    page.getByRole("button", { name: "Записать звук" }),
  ).toHaveAttribute("aria-pressed", "false");
});

test("Operator can save a four-camera layout, change the stream and return to the grid", async ({
  page,
}, info) => {
  await page.setViewportSize({ width: 1440, height: 900 });
  await open(page, "multiview");
  const grid = page.getByRole("region", { name: "Многоканальный эфир" });
  const tile = grid.locator(".mw-tile:visible");
  await expect(tile).toHaveCount(4);
  await expect(tile.first().getByText("Эфир подключён")).toBeVisible();
  await tile.first().getByRole("button", { name: "Развернуть камеру" }).click();
  await expect(tile).toHaveCount(1);
  await tile
    .first()
    .getByRole("combobox", { name: "Качество эфира" })
    .selectOption("main");
  await expect(tile.first()).toContainText("1280×720");
  await page.keyboard.press("Escape");
  await expect(tile).toHaveCount(4);
  await grid.getByRole("button", { name: "Создать раскладку" }).click();
  const editor = page.getByRole("dialog");
  await editor.getByLabel("Название раскладки").fill("Смена");
  await editor.getByRole("button", { name: "Сохранить", exact: true }).click();
  await expect(
    grid.getByRole("combobox", { name: "Раскладка", exact: true }),
  ).toHaveValue(/[a-f0-9-]{36}/);
  await expect(tile).toHaveCount(4);
  await checkAccessibility(page, ".mw-workspace");
  if (info.project.name === "chromium-light")
    await page.screenshot({ path: "/results/monitor-grid-1440.png" });
  await page.setViewportSize({ width: 320, height: 900 });
  await expect
    .poll(() => page.evaluate(() => document.documentElement.scrollWidth))
    .toBe(320);
});
