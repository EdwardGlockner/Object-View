import { defineConfig } from "vite";

export default defineConfig({
  server: {
    proxy: {
      "/api": "http://127.0.0.1:8080",
    },
    fs: {
      // Allow serving scene JSON + assets from sibling project directories
      // (e.g. ../../latent-world/exports) via Vite's /@fs/ dev-only path.
      allow: ["../.."],
    },
  },
});
