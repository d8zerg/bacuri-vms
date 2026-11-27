import { defineConfig } from "vite";
import { fileURLToPath } from "node:url";
import { readFileSync } from "node:fs";
const { version } = JSON.parse(
  readFileSync(
    new URL("../../build/contracts/product-version.json", import.meta.url),
    "utf8",
  ),
);
export default defineConfig(({ mode }) => ({
  define: { "import.meta.env.VITE_PRODUCT_VERSION": JSON.stringify(version) },
  cacheDir: "/tmp/bacuri-workspace-vite",
  resolve: {
    alias:
      mode === "fixture"
        ? {
            "./adapters/services": fileURLToPath(
              new URL("./stories/baseline.ts", import.meta.url),
            ),
          }
        : {},
  },
  build: {
    outDir: mode === "fixture" ? "/results/fixture" : "/results/client",
    emptyOutDir: true,
    assetsInlineLimit: 0,
    sourcemap: "hidden",
    license: { fileName: "assets/third-party-licenses.txt" },
    rolldownOptions: {
      input: { index: "index.html", help: "help.html" },
      output: {
        assetFileNames: (asset) =>
          asset.names.some((name) =>
            /^(sprite\.svg|inter-(latin|cyrillic)\.woff2)$/.test(name),
          )
            ? "assets/[name][extname]"
            : "assets/[name]-[hash][extname]",
      },
    },
  },
}));
