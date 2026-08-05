import type { SensorReading } from '../types';

interface ReadingsTableProps {
  readings: SensorReading[];
}

function formatDate(value: string) {
  return new Intl.DateTimeFormat('vi-VN', {
    dateStyle: 'short',
    timeStyle: 'medium',
    timeZone: 'Asia/Bangkok',
  }).format(new Date(value));
}

function display(value: number | null, unit = '') {
  return value === null ? '—' : `${value}${unit}`;
}

export function ReadingsTable({ readings }: ReadingsTableProps) {
  if (readings.length === 0) {
    return <div className="empty-state">Database chưa có dữ liệu. Hãy thêm bản ghi đầu tiên bằng biểu mẫu bên trên.</div>;
  }

  return (
    <div className="table-wrapper">
      <table>
        <thead>
          <tr>
            <th>Thời gian</th>
            <th>Thiết bị</th>
            <th>Nhiệt độ</th>
            <th>Độ ẩm</th>
            <th>Gas</th>
            <th>Mực nước</th>
            <th>Trạng thái</th>
          </tr>
        </thead>
        <tbody>
          {readings.map((reading) => (
            <tr key={reading.id}>
              <td>{formatDate(reading.recorded_at)}</td>
              <td>{reading.device_id}</td>
              <td>{display(reading.temperature, '°C')}</td>
              <td>{display(reading.humidity, '%')}</td>
              <td>{display(reading.gas_raw)}</td>
              <td>{display(reading.water_level_cm, ' cm')}</td>
              <td>
                <span className={`reading-status status-${reading.status.toLowerCase()}`}>{reading.status}</span>
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
