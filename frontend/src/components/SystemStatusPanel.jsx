const functions = [
  ['F1', 'DHT11: theo dõi nhiệt độ và độ ẩm'],
  ['F2', 'Website → MQTT → ESP32: điều khiển còi'],
  ['F3', 'MQ-2: theo dõi khói và khí gas'],
  ['F4', 'ThingSpeak lưu lịch sử Cloud và hiển thị chart trên web'],
  ['F5', 'ThingSpeak + model AI dự đoán trạng thái sau 5 phút'],
  ['F6', 'JSN-SR04T: đo mực nước và cảnh báo ba LED'],
  ['F7', 'MPU6050: rung/nghiêng, thông báo trình duyệt và Pushsafer'],
  ['F8', 'Website quản lý và trạng thái các kết nối'],
];

function getStatusTone(value) {
  if (['online', 'connected', 'available', 'safe', 'normal', 'off', 'ready'].includes(value)) {
    return 'normal';
  }

  if (['danger', 'on'].includes(value)) {
    return 'danger';
  }

  if (['warning', 'direct', 'not_run', 'insufficient_data', 'muted', 'rate_limited'].includes(value)) {
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
    muted: 'ĐÃ TẮT TIẾNG',
    ready: 'CHƯA TẮT TIẾNG',
    unknown: 'CHƯA XÁC ĐỊNH',
  };

  return labels[value] ?? value?.toUpperCase() ?? 'OFFLINE';
}

function getMuteState(buzzerMuted) {
  if (buzzerMuted === true) {
    return 'muted';
  }

  if (buzzerMuted === false) {
    return 'ready';
  }

  return 'unknown';
}

export function SystemStatusPanel({ health }) {
  const muteState = getMuteState(health?.buzzer_muted);

  const services = [
    ['Frontend website', 'online'],
    ['Backend FastAPI', health?.backend ?? 'offline'],
    ['PostgreSQL', health?.database ?? 'offline'],
    ['ThingSpeak Cloud', health?.thingspeak ?? 'offline'],
    ['Pushsafer', health?.pushsafer ?? 'not_configured'],
    ['MQTT Broker', health?.mqtt ?? 'offline'],
    ['ESP32 Main', health?.main_device ?? 'unknown'],
    ['System risk', health?.system ?? 'unknown'],
    ['Buzzer', health?.buzzer ?? 'unknown'],
    ['Alarm mute', muteState],
    ['ESP32-C3 F7', health?.f7_device ?? 'unknown'],
    ['Mô hình AI', health?.ai ?? 'unavailable'],
  ];

  return (
    <section className="section-block" id="system-status" aria-labelledby="system-status-title">
      <div className="section-heading">
        <div>
          <p className="eyebrow">Self-built management website</p>
          <h2 id="system-status-title">Trạng thái hệ thống và chức năng</h2>
        </div>
        <span className="function-badge">[F8]</span>
      </div>

      <div className="system-status-grid">
        {services.map(([label, value]) => (
          <div className="system-status-item" key={label}>
            <span>{label}</span>
            <strong className={`text-${getStatusTone(value)}`}>{displayStatus(value)}</strong>
          </div>
        ))}
      </div>

      <div className="function-legend-section">
        <p className="eyebrow">Function-ID reference</p>
        <h3>Chú thích chức năng F1–F8</h3>
        <div className="function-legend">
          {functions.map(([functionId, description]) => (
            <div key={functionId}>
              <strong>[{functionId}]</strong>
              <span>{description}</span>
            </div>
          ))}
        </div>
      </div>
    </section>
  );
}
