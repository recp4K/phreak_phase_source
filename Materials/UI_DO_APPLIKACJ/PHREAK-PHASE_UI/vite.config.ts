import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'
import path from 'node:path'

// Vite config — production build served by the JUCE WebView2 ResourceProvider.
// `base: './'` is mandatory: relative asset paths for the custom resource scheme.
export default defineConfig({
  base: './',
  plugins: [
    react(),
    tailwindcss(),
  ],
  resolve: {
    alias: {
      '@': path.resolve(import.meta.dirname, './src'),
    },
  },
  server: {
    host: '127.0.0.1',
    port: 5173,
    strictPort: true,
  },
  build: {
    target: 'es2021',
    sourcemap: false,
    minify: true,
    chunkSizeWarningLimit: 900,
  },
})
