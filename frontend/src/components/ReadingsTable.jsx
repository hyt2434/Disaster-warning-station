function formatDate(value) {
  return new Intl.DateTimeFormat('vi-VN', {
    dateStyle: 'short',
    timeStyle: 'medium',
    timeZone: 'Asia/Ho_Chi_Minh',
  }).format(new Date(value));
}

function display(value, unit = '') {
  return value === null || value === undefined ? '—' : `${value}${unit}`;
}

export function ReadingsTable({ readings }) {
  if (readings.length === 0) {
    return (
      <div className="empty-state">
        Chưa có telemetry từ ESP32.
      </div>
    );
  }

  return (
    <div className="table-wrapper">
      <table>
        <thead>
          <tr>
            <th>Thời gian</th>
            <th>Nhiệt độ</th>
            <th>Độ ẩm</th>
            <th>Gas</th>
            <th>Mực nước</th>
            <th>System</th>
          </tr>
        </thead>
        <tbody>
          {readings.map((reading) => (
            <tr key={reading.id}>
              <td>{formatDate(reading.recorded_at)}</td>
              <td>{display(reading.temperature, '°C')}</td>
              <td>{display(reading.humidity, '%')}</td>
              <td>{display(reading.gas_average)}</td>
              <td>{display(reading.water_level_cm, ' cm')}</td>
              <td>
                <span className={`reading-status status-${reading.status.toLowerCase()}`}>
                  {reading.status}
                </span>
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
