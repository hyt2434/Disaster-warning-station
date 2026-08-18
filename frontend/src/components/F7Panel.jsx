import { useEffect, useRef, useState } from 'react';

function formatValue(value, unit = '') {
  if (value === null || value === undefined) {
    return '—';
  }

  return `${Number(value).toFixed(1)}${unit}`;
}

function getInitialPermission() {
  if (!('Notification' in window)) {
    return 'unsupported';
  }

  return Notification.permission;
}

function permissionLabel(permission) {
  const labels = {
    granted: 'ĐÃ CHO PHÉP',
    denied: 'BỊ CHẶN',
    default: 'CHƯA BẬT',
    unsupported: 'KHÔNG HỖ TRỢ',
  };

  return labels[permission];
}

function getConnectionLabel(connectionStatus) {
  const labels = {
    online: 'ONLINE',
    direct: 'KẾT NỐI TRỰC TIẾP',
    offline: 'OFFLINE',
    unknown: 'CHƯA XÁC ĐỊNH',
  };

  return labels[connectionStatus] ?? 'CHƯA XÁC ĐỊNH';
}

function getF7Tone(status) {
  const normalizedStatus = status?.toLowerCase();

  if (normalizedStatus === 'safe' || normalizedStatus === 'normal') {
    return 'normal';
  }

  if (normalizedStatus === 'warning') {
    return 'warning';
  }

  if (normalizedStatus === 'danger') {
    return 'danger';
  }

  return 'offline';
}

export function F7Panel({ latestF7, connectionStatus }) {
  const [permission, setPermission] = useState(getInitialPermission);
  const [lastNotification, setLastNotification] = useState('Chưa gửi');
  const notifiedReading = useRef('');

  useEffect(() => {
    const isDanger = latestF7?.status?.toUpperCase() === 'DANGER';
    const readingKey = latestF7?.received_at;

    if (!isDanger || permission !== 'granted' || !readingKey) {
      return;
    }

    if (notifiedReading.current === readingKey) {
      return;
    }

    notifiedReading.current = readingKey;
    new Notification('Cảnh báo rung/nghiêng F7', {
      body: `Độ nghiêng ${formatValue(latestF7.tilt, '°')}, rung ${formatValue(latestF7.vibration, ' m/s²')}.`,
    });
    setLastNotification(new Date().toLocaleTimeString('vi-VN'));
  }, [latestF7, permission]);

  async function enableNotifications() {
    if (!('Notification' in window)) {
      setPermission('unsupported');
      return;
    }

    const nextPermission = await Notification.requestPermission();
    setPermission(nextPermission);
  }

  const f7Tone = getF7Tone(latestF7?.status);
  const connectionLabel = getConnectionLabel(connectionStatus);

  return (
    <section className="section-block" id="f7-monitoring" aria-labelledby="f7-title">
      <div className="section-heading">
        <div>
          <p className="eyebrow">Wireless node · Web Notification demo</p>
          <h2 id="f7-title">Rung, độ nghiêng và thông báo trình duyệt</h2>
        </div>
        <span className="function-badge">[F7]</span>
      </div>

      <div className="f7-layout">
        <div className="f7-readings">
          <div className="mini-value"><span>Roll</span><strong>{formatValue(latestF7?.roll, '°')}</strong></div>
          <div className="mini-value"><span>Pitch</span><strong>{formatValue(latestF7?.pitch, '°')}</strong></div>
          <div className="mini-value"><span>Độ nghiêng</span><strong>{formatValue(latestF7?.tilt, '°')}</strong></div>
          <div className="mini-value"><span>Rung</span><strong>{formatValue(latestF7?.vibration, ' m/s²')}</strong></div>
          <div className="mini-value"><span>Va đập</span><strong>{formatValue(latestF7?.impact, ' m/s²')}</strong></div>
          <div className="mini-value"><span>Kết nối</span><strong>{connectionLabel}</strong></div>
          <div className={`function-state state-${f7Tone}`}>{latestF7?.status ?? 'CHƯA CÓ DỮ LIỆU'}</div>
        </div>

        <div className="notification-card">
          <div>
            <span>Quyền thông báo</span>
            <strong>{permissionLabel(permission)}</strong>
          </div>
          <div>
            <span>Thông báo gần nhất</span>
            <strong>{lastNotification}</strong>
          </div>
          <button
            className="primary-button"
            type="button"
            disabled={permission === 'granted' || permission === 'unsupported'}
            onClick={enableNotifications}
          >
            {permission === 'granted' ? 'Đã bật thông báo' : 'Bật thông báo'}
          </button>
          <p>
            Bản demo gửi thông báo của trình duyệt khi F7 báo DANGER và trang web đang mở.
          </p>
        </div>
      </div>
    </section>
  );
}
