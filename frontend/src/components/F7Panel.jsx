function formatValue(value, unit = '') {
  if (value === null || value === undefined) {
    return '—';
  }

  return `${Number(value).toFixed(1)}${unit}`;
}

function getConnectionLabel(connectionStatus) {
  const labels = {
    online: 'ONLINE',
    offline: 'OFFLINE',
    unknown: 'CHƯA XÁC ĐỊNH',
  };

  return labels[connectionStatus] ?? 'CHƯA XÁC ĐỊNH';
}

function getF7Tone(status) {
  const normalizedStatus = status?.toLowerCase();

  if (normalizedStatus === 'safe') {
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
  const isOnline = connectionStatus === 'online';
  const f7Tone = isOnline ? getF7Tone(latestF7?.status) : 'offline';
  const statusLabel = isOnline ? (latestF7?.status ?? 'CHƯA CÓ DỮ LIỆU') : 'OFFLINE';
  const connectionLabel = getConnectionLabel(connectionStatus);

  return (
    <section className="section-block" id="f7-monitoring" aria-labelledby="f7-title">
      <div className="section-heading section-heading-with-badge">
        <span className="function-badge">[F7]</span>
        <h2 id="f7-title">Rung và độ nghiêng</h2>
      </div>

      {!isOnline ? (
        <div className="empty-state">F7 {connectionLabel} — không có dữ liệu mới.</div>
      ) : (
        <div className="f7-readings">
          <div className="mini-value"><span>Roll</span><strong>{formatValue(latestF7?.roll, '°')}</strong></div>
          <div className="mini-value"><span>Pitch</span><strong>{formatValue(latestF7?.pitch, '°')}</strong></div>
          <div className="mini-value"><span>Tilt</span><strong>{formatValue(latestF7?.tilt, '°')}</strong></div>
          <div className="mini-value"><span>Vibration</span><strong>{formatValue(latestF7?.vibration, ' m/s²')}</strong></div>
          <div className="mini-value"><span>Impact</span><strong>{formatValue(latestF7?.impact, ' m/s²')}</strong></div>
          <div className="mini-value"><span>Kết nối</span><strong>{connectionLabel}</strong></div>
          <div className={`mini-value status-value text-${f7Tone}`}><span>Trạng thái</span><strong>{statusLabel}</strong></div>
        </div>
      )}
    </section>
  );
}
