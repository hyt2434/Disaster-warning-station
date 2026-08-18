function getSystemState(health, latestReading) {
  if (health?.system && health.system !== 'unknown') {
    return health.system;
  }

  return latestReading?.status?.toLowerCase() ?? 'unknown';
}

function getSystemLabel(systemState) {
  const labels = {
    safe: 'AN TOÀN',
    normal: 'AN TOÀN',
    warning: 'CẢNH BÁO',
    danger: 'NGUY HIỂM',
    unknown: 'CHƯA XÁC ĐỊNH',
  };

  return labels[systemState] ?? systemState.toUpperCase();
}

function getSystemTone(systemState) {
  if (systemState === 'danger') {
    return 'danger';
  }

  if (systemState === 'warning') {
    return 'warning';
  }

  if (systemState === 'safe' || systemState === 'normal') {
    return 'normal';
  }

  return 'offline';
}

function getBuzzerLabel(buzzerState) {
  if (buzzerState === 'on') {
    return 'ĐANG BẬT';
  }

  if (buzzerState === 'off') {
    return 'ĐANG TẮT';
  }

  return 'CHƯA XÁC ĐỊNH';
}

function getBuzzerTone(buzzerState) {
  if (buzzerState === 'on') {
    return 'danger';
  }

  if (buzzerState === 'off') {
    return 'normal';
  }

  return 'offline';
}

function getBuzzerState(health, latestReading) {
  if (health?.buzzer === 'on' || health?.buzzer === 'off') {
    return health.buzzer;
  }

  if (latestReading?.buzzer === true) {
    return 'on';
  }

  if (latestReading?.buzzer === false) {
    return 'off';
  }

  return 'unknown';
}

function getMuteLabel(buzzerMuted) {
  if (buzzerMuted === true) {
    return 'ĐÃ TẮT TIẾNG';
  }

  if (buzzerMuted === false) {
    return 'CHƯA TẮT TIẾNG';
  }

  return 'CHƯA XÁC ĐỊNH';
}

function getMuteTone(buzzerMuted) {
  if (buzzerMuted === true) {
    return 'warning';
  }

  if (buzzerMuted === false) {
    return 'normal';
  }

  return 'offline';
}

export function DeviceControlPanel({ health, latestReading, sending, onCommand }) {
  const mqttConnected = health?.mqtt === 'connected';
  const systemState = getSystemState(health, latestReading);
  const buzzerState = getBuzzerState(health, latestReading);
  const buzzerMuted = health?.buzzer_muted ?? latestReading?.buzzer_muted;

  const buzzerLabel = getBuzzerLabel(buzzerState);
  const muteLabel = getMuteLabel(buzzerMuted);

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
        <div className="alarm-state-grid">
          <div className="buzzer-state-card">
            <span>System Status</span>
            <strong className={`text-${getSystemTone(systemState)}`}>
              {getSystemLabel(systemState)}
            </strong>
          </div>
          <div className="buzzer-state-card">
            <span>Buzzer State</span>
            <strong className={`text-${getBuzzerTone(buzzerState)}`}>
              {buzzerLabel}
            </strong>
          </div>
          <div className="buzzer-state-card">
            <span>Alarm Mute State</span>
            <strong className={`text-${getMuteTone(buzzerMuted)}`}>
              {muteLabel}
            </strong>
          </div>
        </div>
        <div className="control-buttons">
          <button
            className="danger-button"
            type="button"
            disabled={!mqttConnected || sending}
            onClick={() => onCommand('ON')}
          >
            Bật lại còi
          </button>
          <button
            className="safe-button"
            type="button"
            disabled={!mqttConnected || sending}
            onClick={() => onCommand('OFF')}
          >
            Tắt tiếng cảnh báo
          </button>
        </div>
      </div>

      <p className="function-note control-note">
        Website → Backend → MQTT → ESP32 Main. OFF chỉ tắt tiếng sự kiện DANGER hiện tại.
        Khi hệ thống trở về SAFE, ESP32 tự xóa trạng thái mute để sự kiện DANGER tiếp theo có thể bật còi.
      </p>
    </section>
  );
}
