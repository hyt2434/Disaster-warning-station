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

  if (reading.temperature >= 50) {
    warnings.push('Nhiệt độ cao');
  }

  if (reading.gas_raw >= 700) {
    warnings.push('Khói hoặc gas cao');
  }

  if (reading.water_level_cm >= 40) {
    warnings.push('Mực nước nguy hiểm');
  }

  return warnings;
}

function getAlarmLevel(reading, backendOnline) {
  if (!backendOnline || !reading) {
    return 'offline';
  }

  const deviceStatus = reading.status.toLowerCase();
  const dangerWarnings = findSensorWarnings(reading);

  if (deviceStatus === 'danger' || deviceStatus === 'critical' || dangerWarnings.length > 0) {
    return 'danger';
  }

  const hasWarningValue =
    reading.temperature >= 40 ||
    reading.gas_raw >= 500 ||
    reading.water_level_cm >= 25;

  if (deviceStatus === 'warning' || hasWarningValue) {
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
    timeZone: 'Asia/Bangkok',
  }).format(new Date(dateValue));
}

export function AlarmMonitor({ latestReading, backendOnline }) {
  const alarmLevel = getAlarmLevel(latestReading, backendOnline);
  const content = alarmContent[alarmLevel];
  const sensorWarnings = latestReading ? findSensorWarnings(latestReading) : [];

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
          <span>Còi cảnh báo</span>
          <strong>{alarmLevel === 'danger' ? 'CẦN BẬT' : 'SẴN SÀNG'}</strong>
        </div>
      </div>
    </section>
  );
}
