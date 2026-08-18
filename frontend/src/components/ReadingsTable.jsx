function formatDate(value) {
  return new Intl.DateTimeFormat('vi-VN', {
    dateStyle: 'short',
    timeStyle: 'medium',
    timeZone: 'Asia/Bangkok',
  }).format(new Date(value));
}

function display(value, unit = '') {
  return value === null || value === undefined ? '—' : `${value}${unit}`;
}

function displayBooleanState(value, trueLabel, falseLabel) {
  if (value === null || value === undefined) {
    return '—';
  }

  return value ? trueLabel : falseLabel;
}

export function ReadingsTable({ readings }) {
  if (readings.length === 0) {
    return (
      <div className="empty-state">
        Database chưa có dữ liệu. Hãy thêm bản ghi đầu tiên bằng biểu mẫu bên trên.
      </div>
    );
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
            <th>Còi</th>
            <th>Alarm</th>
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
                <span className={`reading-status status-${reading.status.toLowerCase()}`}>
                  {reading.status}
                </span>
              </td>
              <td>{displayBooleanState(reading.buzzer, 'ON', 'OFF')}</td>
              <td>{displayBooleanState(reading.buzzer_muted, 'MUTED', 'READY')}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
