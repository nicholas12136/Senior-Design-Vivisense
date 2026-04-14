import { defineConfig } from 'vite';

export default defineConfig({
  build: {
    // Output goes into WebserverV3/data/ so PlatformIO can upload it to SPIFFS
    outDir: '../data',
    emptyOutDir: true,
  },
});
