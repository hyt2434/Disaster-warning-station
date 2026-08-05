import { useState, type FormEvent } from 'react';
import type { SensorReadingInput } from '../types';

interface ReadingFormProps {
  saving: boolean;
  onSubmit: (reading: SensorReadingInput) => Promise<void>;
}

const initialValues = {
  deviceId: 'main-station-01',
  temperature: '',
  humidity: '',
  gasRaw: '',
  waterLevelCm: '',
  status: 'NORMAL',
};

export function ReadingForm({ saving, onSubmit }: ReadingFormProps) {
  const [values, setValues] = useState(initialValues);

  async function handleSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    const payload: SensorReadingInput = {
      device_id: values.deviceId.trim(),
      temperature: Number(values.temperature),
      humidity: Number(values.humidity),
      status: values.status,
    };

    if (values.gasRaw !== '') payload.gas_raw = Number(values.gasRaw);
    if (values.waterLevelCm !== '') payload.water_level_cm = Number(values.waterLevelCm);

    await onSubmit(payload);
    setValues((current) => ({ ...initialValues, deviceId: current.deviceId }));
  }

  return (
    <form className="reading-form" onSubmit={handleSubmit}>
      <label>
        Device ID
        <input
          required
          value={values.deviceId}
          onChange={(event) => setValues({ ...values, deviceId: event.target.value })}
        />
      </label>
      <label>
        Nhiệt độ (°C)
        <input
          required
          type="number"
          min="-40"
          max="100"
          step="0.1"
          value={values.temperature}
          onChange={(event) => setValues({ ...values, temperature: event.target.value })}
        />
      </label>
      <label>
        Độ ẩm (%)
        <input
          required
          type="number"
          min="0"
          max="100"
          step="0.1"
          value={values.humidity}
          onChange={(event) => setValues({ ...values, humidity: event.target.value })}
        />
      </label>
      <label>
        Mức gas thô
        <input
          type="number"
          min="0"
          value={values.gasRaw}
          onChange={(event) => setValues({ ...values, gasRaw: event.target.value })}
        />
      </label>
      <label>
        Mực nước (cm)
        <input
          type="number"
          min="0"
          step="0.1"
          value={values.waterLevelCm}
          onChange={(event) => setValues({ ...values, waterLevelCm: event.target.value })}
        />
      </label>
      <label>
        Trạng thái
        <select value={values.status} onChange={(event) => setValues({ ...values, status: event.target.value })}>
          <option value="NORMAL">NORMAL</option>
          <option value="WARNING">WARNING</option>
          <option value="DANGER">DANGER</option>
        </select>
      </label>
      <button className="primary-button" type="submit" disabled={saving}>
        {saving ? 'Đang lưu…' : 'Lưu vào database'}
      </button>
    </form>
  );
}
