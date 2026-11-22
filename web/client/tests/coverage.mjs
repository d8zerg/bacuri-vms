import assert from "node:assert/strict";
import { existsSync, globSync, readFileSync } from "node:fs";
import { dirname, relative, resolve } from "node:path";
import { CoverageReport } from "monocart-coverage-reports";

function isProductSource(path) {
  return /^src\/.*\.tsx?$/.test(path) && !path.endsWith(".d.ts");
}
function inScope(path, integrated) {
  if (integrated && /^public\/assets\/[^/]+\.js$/.test(path)) return true;
  return (
    isProductSource(path) &&
    (integrated || !/^src\/(adapters\/|main\.tsx$)/.test(path))
  );
}
function containsProductSource(entry) {
  const pathname = new URL(entry.url).pathname;
  if (!pathname.startsWith("/assets/")) return false;
  const map = "/results/storybook" + pathname + ".map";
  if (!existsSync(map)) return false;
  const { sources } = JSON.parse(readFileSync(map, "utf8"));
  return sources.some((source) =>
    isProductSource(relative(process.cwd(), resolve(dirname(map), source))),
  );
}
export function coverageReport(integrated = false) {
  return new CoverageReport({
    outputDir: integrated ? "/results/coverage" : "/results/ui-coverage",
    ...(integrated ? { inputDir: "/results/ui-coverage/raw" } : {}),
    reports: [
      "v8",
      "v8-json",
      "console-details",
      ...(integrated ? [] : [["raw", { merge: true }]]),
    ],
    entryFilter: integrated ? () => true : containsProductSource,
    sourcePath: (path) =>
      path.replace(/^.*\/web\/client\/(src|public)\//, "$1/"),
    sourceFilter: (path) => inScope(path, integrated),
    all: {
      dir: "src",
      filter: {
        "**/*.d.ts": false,
        ...(!integrated
          ? { "**/adapters/**": false, "**/main.tsx": false }
          : {}),
        "**/*.ts": true,
        "**/*.tsx": true,
        "**/*": false,
      },
    },
  });
}
export async function assertCoverage(report, integrated) {
  const result = await report.generate();
  const expected = globSync("src/**/*.{ts,tsx}").filter((path) =>
    inScope(path, integrated),
  );
  if (integrated) expected.push(...globSync("public/assets/*.js"));
  assert.deepEqual(
    new Set(result.files.map((file) => file.sourcePath)),
    new Set(expected),
    "Coverage must account for every in-scope product source",
  );
  const { covered, total } = result.summary.lines;
  assert.ok(
    total > 0 && covered * 100 >= 75 * total,
    "Browser routes require 75% line coverage",
  );
}
export default async function finishCoverage() {
  await assertCoverage(coverageReport(), false);
}
