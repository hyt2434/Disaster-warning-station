function displayState(value) {
  return value?.toUpperCase() ?? 'UNKNOWN';
}

function displayMuteState(value) {
  if (value === true) {
    return 'YES';
  }

  if (value === false) {
    return 'NO';
  }

  return 'UNKNOWN';
}

export function DeviceControlPanel({ health, sending, onCommand }) {
  const isDanger = health?.system === 'danger';
  const isMuted = health?.buzzer_muted === true;
  const buzzerIsOn = health?.buzzer === 'on';
  const mqttConnected = health?.mqtt === 'connected';

  let action = null;

  if (isDanger && buzzerIsOn && !isMuted) {
    action = { command: 'OFF', label: 'Tắt tiếng', className: 'danger-button' };
  } else if (isDanger && isMuted) {
    action = { command: 'ON', label: 'Bật lại còi', className: 'safe-button' };
  }

  return (
    <section className="section-block" id="device-control" aria-labelledby="device-control-title">
      <div className="section-heading section-heading-with-badge">
        <span className="function-badge">[F2]</span>
        <h2 id="device-control-title">Điều khiển còi</h2>
      </div>

      <div className="device-control-layout">
        <div className="alarm-state-grid">
          <div className="buzzer-state-card"><span>Buzzer</span><strong>{displayState(health?.buzzer)}</strong></div>
          <div className="buzzer-state-card"><span>Mute</span><strong>{displayMuteState(health?.buzzer_muted)}</strong></div>
        </div>

        {action ? (
          <button
            className={action.className}
            type="button"
            disabled={!mqttConnected || sending}
            onClick={() => onCommand(action.command)}
          >
            {sending ? 'Đang gửi…' : action.label}
          </button>
        ) : (
          <p className="control-message">Không có cảnh báo nguy hiểm cần điều khiển.</p>
        )}
      </div>
    </section>
  );
}
