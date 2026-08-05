export type DatabaseState = 'connected' | 'disconnected';

export interface HealthStatus {
  backend: 'online' | 'offline';
  database: DatabaseState;
}

export interface SensorReading {
  id: number;
  device_id: string;
  temperature: number | null;
  humidity: number | null;
  gas_raw: number | null;
  water_level_cm: number | null;
  status: string;
  recorded_at: string;
}

export interface SensorReadingInput {
  device_id: string;
  temperature: number;
  humidity: number;
  gas_raw?: number;
  water_level_cm?: number;
  status: string;
}
