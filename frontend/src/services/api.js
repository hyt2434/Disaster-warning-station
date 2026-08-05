import { API_BASE_URL } from '../config/env';

async function request(path, options) {
  const response = await fetch(`${API_BASE_URL}${path}`, {
    headers: { 'Content-Type': 'application/json' },
    ...options,
  });

  if (!response.ok) {
    const body = await response.json().catch(() => null);
    throw new Error(body?.detail ?? `API trả về lỗi ${response.status}`);
  }

  return response.json();
}

export function getHealth() {
  return request('/api/health');
}

export function getReadings(limit = 20) {
  return request(`/api/readings?limit=${limit}`);
}

export function createReading(payload) {
  return request('/api/readings', {
    method: 'POST',
    body: JSON.stringify(payload),
  });
}
