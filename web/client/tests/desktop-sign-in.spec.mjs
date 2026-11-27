import { test, expect, checkAccessibility } from "./browser.mjs";

const story = "/iframe.html?viewMode=story&globals=language:en&id=workspace--";

for (const [button, expected] of [
  ["Sign in to the application", /code=0{64}&state=story-state-0000000001$/],
  ["Cancel", /error=access_denied&state=story-state-0000000001$/],
])
  test(`Desktop sign-in sends the answer to the loopback address: ${button}`, async ({
    page,
  }) => {
    const answers = [];
    await page.route("http://127.0.0.1:49152/**", (route) => {
      answers.push(route.request().url());
      return route.fulfill({
        status: 200,
        contentType: "text/html",
        body: "<p>received</p>",
      });
    });
    await page.goto(story + "desktop-sign-in");
    await expect(
      page.getByRole("heading", { name: "Sign in to Bacuri Desktop" }),
    ).toBeVisible();
    await checkAccessibility(page, "main");
    await page.getByRole("button", { name: button }).click();
    await expect.poll(() => answers.length).toBe(1);
    expect(answers[0]).toMatch(/^http:\/\/127\.0\.0\.1:49152\/callback\?/);
    expect(answers[0]).toMatch(expected);
  });

test("Desktop sign-in refuses a request without a valid loopback address and challenge", async ({
  page,
}) => {
  await page.goto(story + "desktop-sign-in-invalid");
  await expect(page.getByRole("alert")).toHaveText(
    "The sign-in request of the application is not valid. Start the sign-in from the application again.",
  );
  await expect(
    page.getByRole("button", { name: "Sign in to the application" }),
  ).toHaveCount(0);
});

test("A corporate sign-in keeps the desktop request and returns to its confirmation", async ({
  page,
}) => {
  await page.route("**/api/v1/session/oidc/methods", (route) =>
    route.fulfill({
      status: 200,
      contentType: "application/json",
      body: '{"oidc":true}',
    }),
  );
  await page.route("**/api/v1/session/oidc/start", (route) =>
    route.fulfill({
      status: 200,
      contentType: "text/html",
      body: "<p>provider</p>",
    }),
  );
  await page.goto(story + "desktop-sign-in-login");
  await page.getByRole("link", { name: "Sign in with Keycloak" }).click();
  await page.waitForURL(/oidc\/start$/);
  const kept = await page.evaluate(() =>
    sessionStorage.getItem("bacuri.desktop.signIn"),
  );
  expect(kept).toContain(
    "code_challenge=E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
  );

  // Back from the provider on the monitor, the workspace returns to the confirmation once.
  await page.goto(story + "monitor");
  await expect(
    page.getByRole("heading", { name: "Sign in to Bacuri Desktop" }),
  ).toBeVisible();
  expect(
    await page.evaluate(() => sessionStorage.getItem("bacuri.desktop.signIn")),
  ).toBeNull();
});
