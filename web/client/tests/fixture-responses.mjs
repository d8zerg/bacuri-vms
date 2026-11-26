// Writes the responses of the Storybook fixture to a JSON file, so that they can be checked against the OpenAPI
// contract. Usage: node tests/fixture-responses.mjs OUTPUT.json
import { build } from "esbuild";
import { mkdtemp, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";

const output = process.argv[2];
if (!output) throw new Error("Usage: fixture-responses.mjs OUTPUT.json");

const directory = await mkdtemp(join(tmpdir(), "bacuri-fixture-"));
const bundle = join(directory, "fixture.mjs");
await build({
  entryPoints: ["stories/workspaceFixture.ts"],
  bundle: true,
  format: "esm",
  platform: "node",
  outfile: bundle,
  loader: { ".png": "dataurl", ".webm": "dataurl" },
  logLevel: "error",
});
const { fixture } = await import(pathToFileURL(bundle).href);

// Each read names the OpenAPI operation whose 200 response describes the returned value.
const site = await (async () =>
  (await fixture("normal").session()).site_roles[0].site_id)();
const reads = {
  getSession: (services) => services.session(),
  listPoints: (services, signal) => services.points(site, signal),
  listCameras: (services, signal) => services.cameras(site, signal),
  getMapScene: (services, signal) => services.maps(site, signal),
  listRecordingPolicies: (services, signal) =>
    services.capturePolicies(site, signal),
  getRecordingStorage: (services, signal) =>
    services.captureStorage(site, signal),
  listMonitorLayouts: (services, signal) => services.layouts(site, signal),
  listInvestigations: (services, signal) => services.cases(site, signal),
};
const conditions = [
  "normal",
  "empty",
  "catalog",
  "recording",
  "multiple-sites",
  "linked-floors",
  "ptz-plan",
  "map-management",
  "controls",
  "multiview",
];

const responses = [];
for (const condition of conditions) {
  for (const [operation, read] of Object.entries(reads)) {
    const services = fixture(condition);
    try {
      const value = await read(services, new AbortController().signal);
      responses.push({ condition, operation, value });
    } catch (error) {
      // A read the condition makes fail is a refusal, not a response body.
      responses.push({ condition, operation, failed: String(error) });
    }
  }
}
await writeFile(output, JSON.stringify(responses, null, 1));
console.log(`${responses.length} fixture responses written to ${output}`);
