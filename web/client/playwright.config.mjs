import { defineConfig } from "playwright/test";
export default defineConfig({
  testDir: "tests",
  testMatch: "*.spec.mjs",
  outputDir: "/results/artifacts",
  snapshotPathTemplate: "/screen-references/{projectName}/{arg}{ext}",
  workers: 2,
  reporter: [["list"], ["json", { outputFile: "/results/tests.json" }]],
  projects: ["chromium", "firefox"].flatMap((browserName) =>
    ["light", "dark"].map((colorScheme) => ({
      name: browserName + "-" + colorScheme,
      use: { browserName, colorScheme },
    })),
  ),
  use: {
    baseURL: "http://127.0.0.1:6006",
    viewport: { width: 1024, height: 720 },
    trace: "retain-on-failure",
  },
  webServer: [
    {
      command:
        "python3 -m http.server 6007 --bind 127.0.0.1 --directory /workspace/prototype/dist",
      url: "http://127.0.0.1:6007",
      stderr: "ignore",
    },
    {
      command:
        "python3 -m http.server 6006 --bind 127.0.0.1 --directory /results/storybook",
      url: "http://127.0.0.1:6006/iframe.html",
      stderr: "ignore",
    },
  ],
  globalTeardown: "./tests/coverage.mjs",
});
