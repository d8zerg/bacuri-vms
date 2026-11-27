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
  const { pathname, protocol } = new URL(entry.url);
  const localBuild =
    protocol === "file:" && pathname.startsWith("/results/client/assets/");
  if (!localBuild && !pathname.startsWith("/assets/")) return false;
  const map =
    (localBuild ? pathname : "/results/storybook" + pathname) + ".map";
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
      // Count unloaded files as uncovered in the integrated report.
      dir: integrated ? ["src", "public/assets"] : "src",
      filter: {
        "**/*.d.ts": false,
        ...(!integrated
          ? { "**/adapters/**": false, "**/main.tsx": false }
          : {}),
        "**/*.ts": true,
        "**/*.tsx": true,
        ...(integrated ? { "**/*.js": true } : {}),
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
  const minimum = JSON.parse(
    readFileSync("../../tests/coverage-policy.json"),
  ).minimum_line_percent;
  const modules = new Map([["client", result.summary.lines]]);
  for (const file of result.files) {
    const [directory, area] = file.sourcePath.split("/");
    const owner = dirname(file.sourcePath).slice("src/".length);
    const name =
      directory === "public"
        ? "browser scripts"
        : area === "main.tsx"
          ? "app"
          : integrated
            ? owner
            : area;
    const counts = modules.get(name) ?? { covered: 0, total: 0 };
    counts.covered += file.summary.lines.covered;
    counts.total += file.summary.lines.total;
    modules.set(name, counts);
  }
  const failed = [];
  for (const [name, { covered, total }] of modules) {
    console.log(
      `Coverage for ${name}: ${covered}/${total} lines (${((100 * covered) / total).toFixed(2)}%)`,
    );
    if (total === 0 || covered * 100 < minimum * total) failed.push(name);
  }
  assert.equal(
    failed.length,
    0,
    `Modules below ${minimum}%: ${failed.join(", ")}`,
  );
}
export default async function finishCoverage() {
  await assertCoverage(coverageReport(), false);
}
