import type { HealthStatus, SensorReading, SensorReadingInput } from '../types';

const API_BASE_URL = import.meta.env.VITE_API_BASE_URL ?? 'http://localhost:8000';

async function request<T>(path: string, options?: RequestInit): Promise<T> {
  const response = await fetch(`${API_BASE_URL}${path}`, {
    headers: { 'Content-Type': 'application/json' },
    ...options,
  });

  if (!response.ok) {
    const body = (await response.json().catch(() => null)) as { detail?: string } | null;
    throw new Error(body?.detail ?? `API trả về lỗi ${response.status}`);
  }

  return response.json() as Promise<T>;
}

export function getHealth(): Promise<HealthStatus> {
  return request<HealthStatus>('/api/health');
}

export function getReadings(limit = 20): Promise<SensorReading[]> {
  return request<SensorReading[]>(`/api/readings?limit=${limit}`);
}

export function createReading(payload: SensorReadingInput): Promise<SensorReading> {
  return request<SensorReading>('/api/readings', {
    method: 'POST',
    body: JSON.stringify(payload),
  });
}
