import assert from "node:assert/strict";
import { existsSync, globSync, readFileSync } from "node:fs";
import { basename, resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { coverageReport, assertCoverage } from "./coverage.mjs";
const report = coverageReport(true);
const runs = globSync("/results/chrome/*.json");
assert.ok(runs.length, "Missing real Chrome executions");
for (const file of runs) {
  const scripts = JSON.parse(readFileSync(file, "utf8"));
  assert.ok(scripts.length);
  await report.add(
    scripts.map((script) => {
      const name = basename(new URL(script.url).pathname);
      const path = "/results/client/assets/" + name;
      const source = readFileSync(path, "utf8");
      const publicAsset = resolve("public/assets", name);
      if (existsSync(publicAsset)) {
        assert.equal(source, readFileSync(publicAsset, "utf8"));
        return { ...script, url: pathToFileURL(publicAsset).href, source };
      }
      return {
        ...script,
        url: "file://" + path,
        source,
        sourceMap: JSON.parse(readFileSync(path + ".map", "utf8")),
      };
    }),
  );
}
await assertCoverage(report, true);
