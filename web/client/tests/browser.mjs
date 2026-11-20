import { createRequire } from "node:module";
import { test as base, expect } from "playwright/test";
import { coverageReport } from "./coverage.mjs";
const require = createRequire(import.meta.url);
export { expect };
export const test = base.extend({
  page: async ({ page }, use, info) => {
    const measure = info.project.name === "chromium-light";
    if (measure)
      await page.coverage.startJSCoverage({ resetOnNavigation: false });
    await use(page);
    if (measure) {
      const entries = await page.coverage.stopJSCoverage();
      await coverageReport().add(
        entries.filter((entry) =>
          entry.url.startsWith("http://127.0.0.1:6006/assets/"),
        ),
      );
    }
  },
});
export async function checkAccessibility(page, selector) {
  await page.addScriptTag({ path: require.resolve("axe-core/axe.min.js") });
  const result = await page.evaluate(async (selector) => {
    const audit = await window.axe.run(document.querySelector(selector), {
      runOnly: ["wcag2a", "wcag2aa", "wcag21a", "wcag21aa", "wcag22aa"],
    });
    return {
      passes: audit.passes.map((rule) => rule.id),
      violations: audit.violations.map((rule) => ({
        id: rule.id,
        nodes: rule.nodes.map((node) => node.target),
      })),
    };
  }, selector);
  expect(result.violations).toEqual([]);
  return result;
}
