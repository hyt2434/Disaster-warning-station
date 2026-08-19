import { getWaterAlarmLevel, SENSOR_THRESHOLDS } from '../config/thresholds';

const alarmContent = {
  normal: {
    title: 'HỆ THỐNG AN TOÀN',
    description: 'Các chỉ số cảm biến đang nằm trong giới hạn bình thường.',
  },
  warning: {
    title: 'CẢNH BÁO BẤT THƯỜNG',
    description: 'Có chỉ số tiến gần ngưỡng nguy hiểm. Cần tiếp tục theo dõi.',
  },
  danger: {
    title: 'BÁO ĐỘNG NGUY HIỂM',
    description: 'Phát hiện chỉ số nguy hiểm. Hệ thống cần kích hoạt cảnh báo.',
  },
  offline: {
    title: 'CHƯA CÓ DỮ LIỆU',
    description: 'Không thể xác định mức an toàn vì backend hoặc thiết bị chưa gửi dữ liệu.',
  },
};

function findSensorWarnings(reading) {
  const warnings = [];

  if (reading.temperature >= SENSOR_THRESHOLDS.temperature.danger) {
    warnings.push('Nhiệt độ nguy hiểm');
  } else if (reading.temperature >= SENSOR_THRESHOLDS.temperature.warning) {
    warnings.push('Nhiệt độ ở mức cảnh báo');
  }

  if (reading.gas_average >= SENSOR_THRESHOLDS.gas.danger) {
    warnings.push('Khói hoặc gas nguy hiểm');
  } else if (reading.gas_average >= SENSOR_THRESHOLDS.gas.warning) {
    warnings.push('Khói hoặc gas ở mức cảnh báo');
  }

  const waterLevel = getWaterAlarmLevel(reading);

  if (waterLevel === 'danger') {
    warnings.push('Mực nước nguy hiểm');
  } else if (waterLevel === 'warning') {
    warnings.push('Mực nước ở mức cảnh báo');
  }

  return warnings;
}

function getAlarmLevel(reading, backendOnline) {
  if (!backendOnline || !reading) {
    return 'offline';
  }

  const deviceStatus = reading.status.toLowerCase();

  if (deviceStatus === 'danger') {
    return 'danger';
  }

  if (deviceStatus === 'warning') {
    return 'warning';
  }

  return 'normal';
}

function formatUpdateTime(dateValue) {
  if (!dateValue) {
    return 'Chưa có';
  }

  return new Intl.DateTimeFormat('vi-VN', {
    dateStyle: 'short',
    timeStyle: 'medium',
    timeZone: 'Asia/Ho_Chi_Minh',
  }).format(new Date(dateValue));
}

function getBuzzerStatusLabel(buzzer) {
  if (buzzer === true) {
    return 'ĐANG BẬT';
  }

  if (buzzer === false) {
    return 'ĐANG TẮT';
  }

  return 'CHƯA XÁC ĐỊNH';
}

function getMuteStatusLabel(buzzerMuted) {
  if (buzzerMuted === true) {
    return 'ĐÃ TẮT TIẾNG';
  }

  if (buzzerMuted === false) {
    return 'CHƯA TẮT TIẾNG';
  }

  return 'CHƯA XÁC ĐỊNH';
}

export function AlarmMonitor({ latestReading, backendOnline }) {
  const alarmLevel = getAlarmLevel(latestReading, backendOnline);
  const content = alarmContent[alarmLevel];
  const sensorWarnings = latestReading ? findSensorWarnings(latestReading) : [];
  const systemStatus = latestReading?.status ?? 'CHƯA XÁC ĐỊNH';
  const buzzerStatus = getBuzzerStatusLabel(latestReading?.buzzer);
  const muteStatus = getMuteStatusLabel(latestReading?.buzzer_muted);

  return (
    <section className={`alarm-monitor alarm-${alarmLevel}`} aria-live="polite">
      <div className="alarm-indicator" aria-hidden="true">
        <span className="alarm-ring" />
        <span className="alarm-light">!</span>
      </div>

      <div className="alarm-message">
        <p className="eyebrow">Monitor báo động trung tâm</p>
        <h2>{content.title}</h2>
        <p>{content.description}</p>

        {sensorWarnings.length > 0 && (
          <p className="alarm-reasons">
            Nguyên nhân: <strong>{sensorWarnings.join(' · ')}</strong>
          </p>
        )}

        <p className="alarm-function-note">
          Tổng hợp từ [F1], [F3], [F6] và trạng thái chuyển động [F7] do ESP32 Main gửi lên.
        </p>
      </div>

      <div className="alarm-summary">
        <div>
          <span>Thiết bị</span>
          <strong>{latestReading?.device_id ?? 'Chưa kết nối'}</strong>
        </div>
        <div>
          <span>Cập nhật cuối</span>
          <strong>{formatUpdateTime(latestReading?.recorded_at)}</strong>
        </div>
        <div>
          <span>System Status</span>
          <strong>{systemStatus}</strong>
        </div>
        <div>
          <span>Buzzer State</span>
          <strong>{buzzerStatus}</strong>
        </div>
        <div>
          <span>Alarm Mute State</span>
          <strong>{muteStatus}</strong>
        </div>
      </div>
    </section>
  );
}
