// Browser part of the desktop sign-in check: opens the confirmation page that the desktop client
// asked for, signs in with a password and confirms or cancels the desktop sign-in.
import { readFileSync, existsSync } from "node:fs";
import { chromium } from "playwright";

const { URL_FILE, USERNAME, PASSWORD, ACTION } = process.env;
const deadline = Date.now() + 30_000;
while (!existsSync(URL_FILE) || readFileSync(URL_FILE, "utf8") === "") {
  if (Date.now() > deadline) throw new Error("The desktop client did not open the confirmation page");
  await new Promise((resolve) => setTimeout(resolve, 200));
}
const url = readFileSync(URL_FILE, "utf8");
const browser = await chromium.launch();
try {
  const context = await browser.newContext({ ignoreHTTPSErrors: true, locale: "en-US" });
  const page = await context.newPage();
  await page.goto(url);
  await page.fill("input[name=username]", USERNAME);
  await page.fill("input[name=password]", PASSWORD);
  await page.click("button[type=submit]");
  const name = ACTION === "allow" ? "Sign in to the application" : "Cancel";
  await page.getByRole("button", { name }).click();
  await page.waitForURL(/^http:\/\/127\.0\.0\.1:\d+\/callback\?/);
  console.log(`Browser: ${ACTION}, loopback page: ${(await page.textContent("body")).trim()}`);
} finally {
  await browser.close();
}
