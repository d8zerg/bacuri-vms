import { test, expect, checkAccessibility } from "./browser.mjs";

const open = async (page, story) => {
  await page.goto("/iframe.html?viewMode=story&id=camera-connection--" + story);
  await expect(
    page.getByRole("form", { name: "Параметры камеры" }),
  ).toBeVisible();
};
const nameInput = (page) =>
  page.getByRole("textbox", { name: "Название", exact: true });
const main = (page) =>
  page.getByRole("textbox", { name: "Основной поток", exact: true });
const secondary = (page) =>
  page.getByRole("textbox", { name: "Дополнительный поток", exact: true });
const check = (page) =>
  page.getByRole("button", { name: "Проверить подключение", exact: true });
const retry = (page) =>
  page.getByRole("button", { name: "Повторить проверку", exact: true });
const status = (page) => page.getByRole("form").getByRole("status");

test("First use, keyboard correction and confirmation of both stream profiles", async ({
  page,
}) => {
  await open(page, "empty");
  await check(page).press("Enter");
  await expect(nameInput(page)).toBeFocused();
  await expect(nameInput(page)).toHaveAccessibleDescription(
    "Введите название камеры.",
  );
  await nameInput(page).fill("Ворота склада");
  await nameInput(page).press("Enter");
  await expect(main(page)).toBeFocused();
  await expect(main(page)).toHaveAccessibleDescription(
    /Для записи в архив.*Введите адрес потока/,
  );
  await main(page).fill("rtsps://warehouse.test/main");
  await main(page).press("Enter");
  await expect(secondary(page)).toBeFocused();
  await secondary(page).fill("rtsp://[2001:db8::1]:8554/sub");
  await secondary(page).press("Tab");
  await expect(check(page)).toBeFocused();
  await check(page).press("Space");
  await expect(status(page).getByText("Доступен", { exact: true })).toHaveCount(
    2,
  );
  await expect(check(page)).toBeFocused();
  await expect(page.locator('input[aria-invalid="true"]')).toHaveCount(0);
  await nameInput(page).fill("Другие ворота");
  await expect(status(page)).toBeEmpty();
});

test("Invalid stream URLs remain local and the error identifies the field", async ({
  page,
}) => {
  await open(page, "ready");
  for (const [address, message] of [
    ["https://camera.test/main", "Начните адрес"],
    ["rtsp://operator@camera.test/main", "Удалите учётные данные"],
    ["rtsp://camera.test/main?", "Удалите параметры"],
    ["rtsp://camera.test/main#t", "Удалите параметры"],
    ["rtsp://camera.test/my stream", "Проверьте символы"],
    ["rtsps://camera.test/%7", "Проверьте символы"],
    ["rtsp://:8554/main", "Проверьте имя узла"],
    ["rtsp://camera.test:0/main", "Проверьте имя узла"],
    ["rtsps://camera.test/" + "я".repeat(1024), "2048 байт"],
  ]) {
    await main(page).fill(address);
    await check(page).press("Enter");
    await expect(main(page)).toBeFocused();
    await expect(main(page)).toHaveAttribute("aria-invalid", "true");
    await expect(main(page)).toHaveAccessibleDescription(new RegExp(message));
    await expect(status(page)).toBeEmpty();
  }
  await nameInput(page).fill("я".repeat(257));
  await check(page).press("Enter");
  await expect(nameInput(page)).toBeFocused();
  await expect(nameInput(page)).toHaveAccessibleDescription(/256 символов/);
});

test("A disconnect preserves the draft, repeated submit starts one check and retry succeeds", async ({
  page,
}) => {
  await open(page, "disconnect");
  await page.clock.install();
  await check(page).focus();
  await check(page).press("Enter");
  await expect(check(page)).toBeDisabled();
  await expect(status(page)).toHaveText("Проверяем оба потока…");
  await page.keyboard.press("Enter");
  await page.clock.fastForward(350);
  await expect(status(page)).toHaveText(/Связь с площадкой потеряна/);
  await expect(retry(page)).toBeFocused();
  await expect(main(page)).toHaveValue("rtsps://camera.test/main");
  await expect(secondary(page)).toHaveValue("rtsps://camera.test/sub");
  await expect(nameInput(page)).toHaveValue("Погрузочные ворота 02");
  await retry(page).press("Enter");
  await page.clock.fastForward(350);
  await expect(status(page).getByText("Доступен", { exact: true })).toHaveCount(
    2,
  );
});

test("An unsuccessful check reports only the known cause and keeps focus on the edited field", async ({
  page,
}) => {
  await open(page, "failed");
  await expect(status(page)).toHaveText(
    "Не удалось завершить проверку. Повторите попытку.",
  );
  await retry(page).press("Enter");
  await nameInput(page).focus();
  await expect(status(page).getByText("Доступен", { exact: true })).toHaveCount(
    2,
  );
  await expect(nameInput(page)).toBeFocused();
  await main(page).fill("rtsps://camera.test/offline");
  await check(page).click();
  await expect(
    status(page).getByText("Нет связи", { exact: true }),
  ).toBeVisible();
  await expect(status(page).getByText("Доступен", { exact: true })).toHaveCount(
    1,
  );
});

test("A late reply for edited parameters cannot replace the latest profile results", async ({
  page,
}) => {
  await open(page, "late-reply");
  await page.clock.install();
  await main(page).fill("rtsps://camera.test/offline");
  await check(page).click();
  await expect(status(page)).toHaveText(/Проверяем оба потока/);
  await main(page).fill("rtsps://camera.test/main");
  await expect(status(page)).toBeEmpty();
  await check(page).click();
  await page.clock.fastForward(350);
  await expect(status(page).getByText("Доступен", { exact: true })).toHaveCount(
    2,
  );
  await page.clock.fastForward(3000);
  await expect(status(page).getByText("Доступен", { exact: true })).toHaveCount(
    2,
  );
});

test("Partial success and invalid fields are readable in both themes and at 200% text size", async ({
  page,
}, info) => {
  await open(page, "partial");
  const result = status(page);
  await expect(
    result.getByText("Несовместимый профиль", { exact: true }),
  ).toBeVisible();
  await expect(result.getByText("Доступен", { exact: true })).toHaveCount(1);
  const form = page.getByRole("form");
  await expect(form).toHaveCSS(
    "background-color",
    info.project.use.colorScheme === "dark"
      ? "rgb(37, 37, 38)"
      : "rgb(255, 253, 248)",
  );
  await info.attach("camera-profiles", {
    body: await form.screenshot(),
    contentType: "image/png",
  });
  await checkAccessibility(page, ".bc-camera");
  await page.setViewportSize({ width: 320, height: 850 });
  await page.locator("html").evaluate((el) => {
    el.style.fontSize = "200%";
  });
  await secondary(page).fill("");
  await check(page).press("Enter");
  await expect(secondary(page)).toBeFocused();
  await expect(secondary(page)).toHaveCSS("outline-style", "solid");
  await expect(secondary(page)).toHaveAccessibleDescription(
    /Для прямого эфира.*Введите адрес/,
  );
  expect(
    await page.evaluate(() => document.documentElement.scrollWidth),
  ).toBeLessThanOrEqual(320);
  await checkAccessibility(page, ".bc-camera");
  await info.attach("camera-error-320", {
    body: await form.screenshot(),
    contentType: "image/png",
  });
});
