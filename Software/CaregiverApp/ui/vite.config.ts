import { defineConfig } from 'vite';

export default defineConfig({
  build: {
    // Output goes into CaregiverApp/data/ so PlatformIO can upload it to SPIFFS
    outDir: '../data',
    emptyOutDir: true,
  },
});
