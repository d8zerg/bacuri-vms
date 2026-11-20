import { defineConfig } from "vite";
export default defineConfig({
  cacheDir: "/tmp/bacuri-vite",
  build: {
    lib: {
      entry: {
        "archive-timeline": "src/ui-kit/ArchiveTimeline.tsx",
        "camera-connection-form": "src/ui-kit/CameraConnectionForm.tsx",
        "form-controls": "src/ui-kit/FormControls.tsx",
        "camera-connection-check": "src/workflows/useCameraConnectionCheck.ts",
      },
      formats: ["es"],
      fileName: (_format, name) => name + ".js",
      cssFileName: "archive-timeline",
    },
    outDir: "/results/library",
    emptyOutDir: true,
    sourcemap: true,
    rolldownOptions: { external: ["react", "react/jsx-runtime"] },
  },
});
