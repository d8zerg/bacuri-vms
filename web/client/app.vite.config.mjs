import { defineConfig } from "vite";
import { fileURLToPath } from "node:url";
export default defineConfig(({ mode }) => ({
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
