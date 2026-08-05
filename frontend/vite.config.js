import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  // Backend và frontend dùng chung file .env ở thư mục gốc repository.
  envDir: '..',
});
