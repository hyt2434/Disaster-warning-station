export function DeviceControlPanel({ health, sending, onCommand }) {
  const mqttConnected = health?.mqtt === 'connected';
  const buzzerState = health?.buzzer ?? 'unknown';
  const stateLabel = buzzerState === 'on' ? 'ĐANG BẬT' : buzzerState === 'off' ? 'ĐANG TẮT' : 'CHƯA XÁC ĐỊNH';

  return (
    <section className="section-block" id="device-control" aria-labelledby="device-control-title">
      <div className="section-heading">
        <div>
          <p className="eyebrow">Output device control</p>
          <h2 id="device-control-title">Điều khiển còi cảnh báo</h2>
        </div>
        <span className="function-badge">[F2]</span>
      </div>

      <div className="device-control-layout">
        <div className="buzzer-state-card">
          <span>Trạng thái còi do ESP32 phản hồi</span>
          <strong className={buzzerState === 'on' ? 'text-danger' : ''}>{stateLabel}</strong>
        </div>
        <div className="control-buttons">
          <button
            className="danger-button"
            type="button"
            disabled={!mqttConnected || sending}
            onClick={() => onCommand('ON')}
          >
            Bật còi
          </button>
          <button
            className="safe-button"
            type="button"
            disabled={!mqttConnected || sending}
            onClick={() => onCommand('OFF')}
          >
            Tắt còi
          </button>
        </div>
      </div>

      <p className="function-note control-note">
        Website → Backend → MQTT → ESP32 Main. Nút bị khóa khi MQTT chưa kết nối; lệnh OFF không tắt được cảnh báo an toàn tự động đang hoạt động.
      </p>
    </section>
  );
}
