export function getRequiredEnvironmentVariable(name) {
  const value = import.meta.env[name];

  if (typeof value !== 'string' || value.trim() === '') {
    throw new Error(`Thiếu biến ${name} trong file .env ở thư mục gốc dự án.`);
  }

  return value.trim();
}

export const API_BASE_URL = getRequiredEnvironmentVariable('VITE_API_BASE_URL');
