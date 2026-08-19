function getStatusTone(value) {
  if (['online', 'connected', 'available', 'ready'].includes(value)) {
    return 'normal';
  }

  if (['rate_limited', 'not_run', 'insufficient_data'].includes(value)) {
    return 'warning';
  }

  return 'offline';
}

function displayStatus(value) {
  const labels = {
    not_configured: 'CHƯA CẤU HÌNH',
    rate_limited: 'ĐỢI GIỚI HẠN 15 GIÂY',
    not_run: 'CHƯA DỰ ĐOÁN',
    insufficient_data: 'THIẾU DỮ LIỆU',
    ready: 'SẴN SÀNG',
    unknown: 'CHƯA XÁC ĐỊNH',
  };

  return labels[value] ?? value?.toUpperCase() ?? 'OFFLINE';
}

export function SystemStatusPanel({ health }) {
  const services = [
    ['Backend', health?.backend ?? 'offline'],
    ['PostgreSQL', health?.database ?? 'offline'],
    ['MQTT', health?.mqtt ?? 'offline'],
    ['ThingSpeak', health?.thingspeak ?? 'offline'],
    ['Pushsafer', health?.pushsafer ?? 'not_configured'],
    ['ESP32 Main', health?.main_device ?? 'unknown'],
    ['ESP32 F7', health?.f7_device ?? 'unknown'],
    ['AI Model', health?.ai ?? 'unavailable'],
  ];

  return (
    <section className="section-block" id="system-status" aria-labelledby="system-status-title">
      <div className="section-heading section-heading-with-badge">
        <span className="function-badge">[F8]</span>
        <h2 id="system-status-title">Trạng thái kết nối</h2>
      </div>

      <div className="system-status-grid">
        {services.map(([label, value]) => (
          <div className="system-status-item" key={label}>
            <span>{label}</span>
            <strong className={`text-${getStatusTone(value)}`}>{displayStatus(value)}</strong>
          </div>
        ))}
      </div>
    </section>
  );
}
