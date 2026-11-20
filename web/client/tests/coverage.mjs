import assert from "node:assert/strict";
import { existsSync, globSync, readFileSync } from "node:fs";
import { dirname, relative, resolve } from "node:path";
import { CoverageReport } from "monocart-coverage-reports";

function isProductSource(path) {
  return /^src\/.*\.tsx?$/.test(path) && !path.endsWith(".d.ts");
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

export function coverageReport() {
  return new CoverageReport({
    outputDir: "/results/coverage",
    reports: ["v8", "v8-json", "console-details"],
    entryFilter: containsProductSource,
    sourcePath: (path) => path.replace(/^.*\/web\/client\/src\//, "src/"),
    sourceFilter: isProductSource,
    all: {
      dir: "src",
      filter: {
        "**/*.d.ts": false,
        "**/*.ts": true,
        "**/*.tsx": true,
        "**/*": false,
      },
    },
  });
}
export default async function finishCoverage() {
  const result = await coverageReport().generate();
  const expected = globSync("src/**/*.{ts,tsx}").filter(isProductSource);
  assert.deepEqual(
    new Set(result.files.map((file) => file.sourcePath)),
    new Set(expected),
    "Coverage must include every product source and exclude test and vendor code",
  );
  const { covered, total } = result.summary.lines;
  assert.ok(
    total > 0 && covered * 100 >= total * 75,
    "UI Kit requires 75% line coverage",
  );
}
