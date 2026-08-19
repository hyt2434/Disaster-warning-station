const STATUS_CONTENT = {
  safe: {
    label: 'SAFE',
    description: 'Các cảm biến đang ở mức an toàn.',
    tone: 'normal',
  },
  warning: {
    label: 'WARNING',
    description: 'Hệ thống đang ghi nhận mức cảnh báo.',
    tone: 'warning',
  },
  danger: {
    label: 'DANGER',
    description: 'Hệ thống đang ghi nhận mức nguy hiểm.',
    tone: 'danger',
  },
  unknown: {
    label: 'UNKNOWN',
    description: 'Chưa nhận được trạng thái từ ESP32 Main.',
    tone: 'offline',
  },
};

function formatUpdateTime(dateValue) {
  if (!dateValue) {
    return 'Chưa có';
  }

  return new Intl.DateTimeFormat('vi-VN', {
    timeStyle: 'medium',
    timeZone: 'Asia/Ho_Chi_Minh',
  }).format(new Date(dateValue));
}

function displayBoolean(value, trueLabel, falseLabel) {
  if (value === true) {
    return trueLabel;
  }

  if (value === false) {
    return falseLabel;
  }

  return 'UNKNOWN';
}

export function AlarmMonitor({ health, latestReading }) {
  const systemState = health?.system ?? 'unknown';
  const content = STATUS_CONTENT[systemState] ?? STATUS_CONTENT.unknown;

  return (
    <section className={`alarm-monitor alarm-${content.tone}`} aria-live="polite">
      <div className="alarm-message">
        <h2>Trạng thái hệ thống</h2>
        <strong className="system-state-value">{content.label}</strong>
        <p>{content.description}</p>
      </div>

      <div className="alarm-summary">
        <div><span>ESP32 Main</span><strong>{health?.main_device?.toUpperCase() ?? 'UNKNOWN'}</strong></div>
        <div><span>Buzzer</span><strong>{health?.buzzer?.toUpperCase() ?? 'UNKNOWN'}</strong></div>
        <div><span>Mute</span><strong>{displayBoolean(health?.buzzer_muted, 'YES', 'NO')}</strong></div>
        <div><span>Cập nhật</span><strong>{formatUpdateTime(latestReading?.recorded_at)}</strong></div>
      </div>
    </section>
  );
}
