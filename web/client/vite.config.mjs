import { defineConfig } from "vite";
export default defineConfig({
  cacheDir: "/tmp/bacuri-vite",
  build: {
    lib: {
      entry: "src/ui-kit/ArchiveTimeline.tsx",
      formats: ["es"],
      fileName: "archive-timeline",
    },
    outDir: "/results/library",
    emptyOutDir: true,
    sourcemap: true,
    rolldownOptions: { external: ["react", "react/jsx-runtime"] },
  },
});
