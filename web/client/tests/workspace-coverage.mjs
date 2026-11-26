import assert from "node:assert/strict";
import { existsSync, globSync, readFileSync } from "node:fs";
import { basename, resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { coverageReport, assertCoverage } from "./coverage.mjs";
import { serviceAdapterCoverage } from "./service-adapters.mjs";
import { speechCaptureCoverage } from "./speech-capture.mjs";
const report = coverageReport(true);
await report.add(await speechCaptureCoverage());
await report.add(await serviceAdapterCoverage());
const runs = globSync("/results/chrome/*.json");
assert.ok(runs.length, "Missing real Chrome executions");
for (const file of runs) {
  const scripts = JSON.parse(readFileSync(file, "utf8"));
  assert.ok(scripts.length);
  await report.add(
    scripts.flatMap((script) => {
      const name = basename(new URL(script.url).pathname);
      const path = "/results/client/assets/" + name;
      const source = readFileSync(path, "utf8");
      const publicAsset = resolve("public/assets", name);
      if (existsSync(publicAsset)) {
        assert.equal(source, readFileSync(publicAsset, "utf8"));
        return { ...script, url: pathToFileURL(publicAsset).href, source };
      }
      if (!existsSync(path + ".map")) {
        // Rolldown emits its runtime and pure re-export facades without source maps.
        assert.ok(
          /^rolldown-runtime-[\w-]+\.js$/.test(name) ||
            /^export\{[\w$, ]+\}from"\.\/[\w.-]+\.js";\s*$/.test(source),
          "Missing source map for " + name,
        );
        return [];
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
