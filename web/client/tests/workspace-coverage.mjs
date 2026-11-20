import assert from "node:assert/strict";
import { globSync, readFileSync } from "node:fs";
import { basename } from "node:path";
import { coverageReport, assertCoverage } from "./coverage.mjs";
const report = coverageReport(true);
const runs = globSync("/results/chrome/*.json");
assert.ok(runs.length, "Missing real Chrome executions");
for (const file of runs) {
  const scripts = JSON.parse(readFileSync(file, "utf8"));
  assert.ok(scripts.length);
  await report.add(
    scripts.map((script) => {
      const path =
        "/results/client/assets/" + basename(new URL(script.url).pathname);
      return {
        ...script,
        url: "file://" + path,
        source: readFileSync(path, "utf8"),
        sourceMap: JSON.parse(readFileSync(path + ".map", "utf8")),
      };
    }),
  );
}
await assertCoverage(report, true);
