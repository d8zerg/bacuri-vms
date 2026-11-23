import { test, expect, checkAccessibility } from "./browser.mjs";

test("Access editors keep a single user open, resolve audit resources and show full-frame privacy effects", async ({
  page,
}, info) => {
  const user = {
    id: "u1",
    username: "Дежурный",
    role: "operator",
    enabled: true,
    issuer: null,
    subject: null,
  };
  const account = {
    ...user,
    id: "u2",
    username: "Монтажник",
    role: "administrator",
  };
  const points = [
    { id: "p1", name: "Главный вход" },
    { id: "p2", name: "Склад" },
  ];
  let policy = {
    point_id: "p1",
    archive: true,
    live: false,
    thumbnail: false,
    export: false,
    regions: [{ left: 0.12345678, right: 0.9, top: 0.15, bottom: 0.7 }],
  };
  let rev = 1;
  const audit = [
    {
      id: "a1",
      action: "site_roles.insert",
      method: "console",
      resource: user.id,
      actor: "local-console",
      recorded_at: "2025-01-01T00:00:00Z",
    },
  ];
  await page.route("**/api/v1/sites/*/**", async (route) => {
    const suffix = new URL(route.request().url()).pathname
      .split("/")
      .slice(5)
      .join("/");
    if (
      suffix === "privacy-policies/p1" &&
      route.request().method() === "PUT"
    ) {
      policy = { point_id: "p1", ...route.request().postDataJSON() };
      rev++;
    } else if (suffix === "users/u1" && route.request().method() === "PATCH") {
      Object.assign(user, route.request().postDataJSON());
      rev++;
    }
    const data = {
      users: [user, account],
      "resource-groups": [{ id: "g1", name: "Периметр", points: ["p1"] }],
      grants: [],
      "privacy-policies": [policy],
      "identity/provider": {},
      "audit/search": audit,
    }[suffix];
    await route.fulfill({
      json:
        suffix === "points"
          ? points
          : { revision: String(rev), data: data ?? {} },
    });
  });
  await page.goto("/iframe.html?viewMode=story&id=workspace--installer");
  await page.getByRole("link", { name: "Доступ", exact: true }).click();
  const panel = page.locator(".ws-access");
  const menu = page.getByRole("group", { name: "Управление доступом" });
  for (const [width, columns] of [
    [1440, 1],
    [1199, 6],
    [900, 6],
    [833, 6],
    [768, 6],
    [705, 6],
    [390, 3],
    [385, 3],
    [384, 2],
    [320, 2],
  ]) {
    await page.setViewportSize({ width, height: 900 });
    const inSidebar = page
      .locator(".ws-nav-section")
      .filter({ has: page.getByRole("link", { name: "Доступ", exact: true }) })
      .getByRole("group", { name: "Управление доступом" });
    await expect(inSidebar).toHaveCount(width >= 1200 ? 1 : 0);
    const controls = menu.getByRole("button");
    await expect(controls).toHaveCount(6);
    const boxes = await controls.evaluateAll((buttons) =>
      buttons.map((button) => {
        const rect = button.getBoundingClientRect();
        const text = button.querySelector("span").getBoundingClientRect();
        return {
          x: rect.x,
          y: rect.y,
          width: rect.width,
          height: rect.height,
          singleLine:
            text.height <= parseFloat(getComputedStyle(button).lineHeight) + 1,
          fits: text.left >= rect.left && text.right <= rect.right,
        };
      }),
    );
    expect(new Set(boxes.map((b) => Math.round(b.x))).size).toBe(columns);
    expect(new Set(boxes.map((b) => Math.round(b.y))).size).toBe(6 / columns);
    for (const b of boxes) {
      expect(b.height).toBeGreaterThanOrEqual(44);
      expect(b.fits).toBe(true);
      expect(b.singleLine).toBe(true);
      expect(Math.abs(b.width - boxes[0].width)).toBeLessThan(1);
    }
    expect(
      await page.evaluate(() => document.documentElement.scrollWidth),
    ).toBeLessThanOrEqual(width);
    if (width === 1440) {
      await menu
        .getByRole("button", { name: "Группы", exact: true })
        .press("Enter");
      await expect(
        panel.getByRole("button", { name: "Сохранить группу" }),
      ).toBeVisible();
      const groups = panel.getByRole("search", { name: "Фильтры групп" });
      await groups.getByRole("searchbox").fill("пЕРим");
      await groups.getByLabel("Группа ресурсов").selectOption("g1");
      await expect(
        panel.getByRole("checkbox", { name: "Главный вход" }),
      ).toBeChecked();
      await groups.getByRole("searchbox").fill("несуществующая");
      await expect(panel.getByText("Группы не найдены.")).toBeVisible();
      await expect(
        panel.getByRole("button", { name: "Сохранить группу" }),
      ).toBeHidden();
      await groups.getByRole("button", { name: "Сбросить фильтры" }).click();
      await expect(groups.getByLabel("Группа ресурсов")).toHaveValue("g1");
      if (info.project.name === "chromium-light")
        await page.screenshot({ path: "/results/access-sidebar.png" });
      await page.getByRole("button", { name: "Свернуть меню" }).click();
      await expect(
        menu
          .getByRole("button", { name: "Группы", exact: true })
          .locator("span"),
      ).toBeHidden();
      await expect(
        menu.getByRole("button", { name: "Группы", exact: true }),
      ).toHaveAttribute("aria-pressed", "true");
      await menu
        .getByRole("button", { name: "Пользователи", exact: true })
        .press("Space");
      await expect(
        menu.getByRole("button", { name: "Пользователи", exact: true }),
      ).toHaveAttribute("aria-pressed", "true");
      await page.getByRole("button", { name: "Развернуть меню" }).click();
    }
  }
  const operator = panel
    .locator(".ws-users summary")
    .filter({ hasText: user.username });
  await expect(operator).toBeVisible();
  const refresh = panel
    .getByRole("search", { name: "Фильтры пользователей" })
    .getByRole("button", { name: "Обновить данные" });
  await expect(refresh).toHaveText("");
  await expect(refresh).toHaveAttribute("title", "Обновить данные");
  await expect(refresh.locator("svg")).toBeVisible();
  await Promise.all([
    page.waitForResponse((res) => res.url().endsWith("/users")),
    refresh.press("Enter"),
  ]);
  await expect(
    panel.getByRole("button", { name: "Сохранить пользователя" }),
  ).toHaveCount(0);
  await operator.press("Enter");
  await panel.getByText("Добавить пользователя", { exact: true }).click();
  await expect(
    panel.getByRole("button", { name: "Сохранить пользователя" }),
  ).toHaveCount(0);
  await operator.click();
  await panel
    .getByRole("combobox", { name: "Роль", exact: true })
    .selectOption("administrator");
  await panel.getByRole("button", { name: "Сохранить пользователя" }).click();
  await expect(operator).toContainText("Администратор");
  await expect(
    panel.getByRole("button", { name: "Сохранить пользователя" }),
  ).toHaveCount(0);
  await page.setViewportSize({ width: 390, height: 844 });
  if (info.project.name === "chromium-light")
    await page.screenshot({ path: "/results/users-mobile.png" });
  await panel.getByRole("button", { name: "Аудит", exact: true }).click();
  const auditFilters = panel.getByRole("search", {
    name: "Фильтры аудита доступа",
  });
  await auditFilters.getByRole("searchbox").fill("нет совпадения");
  await expect(panel.getByText("Записи не найдены.")).toBeVisible();
  const reset = auditFilters.getByRole("button", { name: "Сбросить фильтры" });
  await expect(reset).toHaveText("");
  await auditFilters.getByRole("button", { name: "Обновить данные" }).click();
  await expect(auditFilters.getByRole("searchbox")).toHaveValue(
    "нет совпадения",
  );
  await reset.press("Space");
  await expect(auditFilters.getByRole("searchbox")).toBeFocused();
  const event = panel.locator(".ws-access-audit summary");
  await expect(event).toContainText("Назначена роль на площадке · Дежурный");
  await expect(
    panel.getByText("site_roles.insert", { exact: true }),
  ).toBeHidden();
  await event.press("Enter");
  await expect(
    panel.getByText("site_roles.insert", { exact: true }),
  ).toBeVisible();
  await panel.getByRole("button", { name: "Приватность", exact: true }).click();
  const privacyFilters = panel.getByRole("search", {
    name: "Фильтры точек приватности",
  });
  await privacyFilters.getByRole("searchbox").fill("Склад");
  await expect(panel.locator(".ws-privacy-effect")).toBeHidden();
  await privacyFilters
    .getByRole("button", { name: "Сбросить фильтры" })
    .click();
  const impact = panel.locator(".ws-privacy-effect");
  await expect(impact).toContainText("Архив");
  await panel.getByRole("checkbox", { name: "Эфир" }).check();
  await expect(impact).toContainText("весь кадр будет закрыт");
  await expect(impact).toContainText("Эфир · Архив");
  if (info.project.name === "chromium-light")
    await page.screenshot({ path: "/results/privacy-mobile.png" });
  await panel.getByText("Области маски (1)").click();
  const frame = panel.getByRole("img", { name: "Схема областей кадра" });
  await frame.scrollIntoViewIfNeeded();
  const bounds = await frame.boundingBox();
  await page.mouse.move(
    bounds.x + bounds.width * 0.2,
    bounds.y + bounds.height * 0.2,
  );
  await page.mouse.down();
  await page.mouse.move(
    bounds.x + bounds.width * 0.6,
    bounds.y + bounds.height * 0.7,
  );
  await page.mouse.up();
  await expect(panel.getByText("Области маски (2)")).toBeVisible();
  await panel.getByRole("button", { name: "Удалить область 2" }).click();
  await panel.getByRole("button", { name: "Добавить область" }).click();
  const area = panel.getByRole("group", { name: "Область 2", exact: true });
  await area.getByLabel("Слева, %").fill("90");
  const apply = panel.getByRole("button", { name: "Применить политику" });
  await expect(apply).toBeDisabled();
  await area.getByLabel("Слева, %").fill("30");
  await apply.click();
  await expect(panel.getByText("Области маски (2)")).toBeVisible();
  expect(policy.regions.map((b) => b.left)).toEqual([0.12345678, 0.3]);
  expect(policy.live && policy.archive).toBe(true);
  await panel
    .getByRole("combobox", { name: "Точка", exact: true })
    .selectOption("p2");
  await expect(impact).toContainText("не будет закрывать");
  await expect(panel.getByText("Области маски (0)")).toBeVisible();
  await checkAccessibility(page, ".ws-access");
  expect(
    await page.evaluate(() => document.documentElement.scrollWidth),
  ).toBeLessThanOrEqual(390);
});
