import { defineConfig } from "playwright/test";

export default defineConfig({
  testMatch: "routes.spec.mjs",
  outputDir: "/results/artifacts",
  snapshotPathTemplate: "{testDir}/screens/{projectName}/{arg}{ext}",
  workers: 1,
  projects: ["chromium", "firefox"].flatMap((browserName) =>
    ["light", "dark"].map((colorScheme) => ({
      name: `${browserName}-${colorScheme}`,
      use: { browserName, colorScheme },
    })),
  ),
  use: { viewport: { width: 1440, height: 900 }, trace: "retain-on-failure" },
  webServer: {
    command: "python3 -m http.server 8080 --bind 127.0.0.1 --directory /usr/share/nginx/html",
    url: "http://127.0.0.1:8080",
  },
});
