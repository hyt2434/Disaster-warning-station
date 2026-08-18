export function Header({ health, onRefresh, refreshing }) {
  const databaseConnected = health?.database === 'connected';
  const mqttConnected = health?.mqtt === 'connected';
  const thingspeakReady = ['ready', 'connected'].includes(health?.thingspeak);

  return (
    <header className="overview-header" id="overview">
      <div className="overview-copy">
        <p className="eyebrow">IoT monitoring project</p>
        <h1>Disaster Warning System</h1>
        <p className="subtitle">
          Theo dõi cảm biến ESP32, lưu PostgreSQL và đồng bộ ThingSpeak Cloud.
        </p>
      </div>
      <div className="header-actions">
        <span className={`connection-badge ${health ? 'online' : 'offline'}`}>
          <span className="status-dot" /> Backend {health ? 'ONLINE' : 'OFFLINE'}
        </span>
        <span className={`connection-badge ${databaseConnected ? 'online' : 'offline'}`}>
          <span className="status-dot" /> PostgreSQL {databaseConnected ? 'CONNECTED' : 'DISCONNECTED'}
        </span>
        <span className={`connection-badge ${thingspeakReady ? 'online' : 'offline'}`}>
          <span className="status-dot" /> ThingSpeak {thingspeakReady ? 'READY' : 'DISCONNECTED'}
        </span>
        <span className={`connection-badge ${mqttConnected ? 'online' : 'offline'}`}>
          <span className="status-dot" /> MQTT {mqttConnected ? 'CONNECTED' : 'DISCONNECTED'}
        </span>
        <button
          className="secondary-button"
          type="button"
          onClick={() => onRefresh()}
          disabled={refreshing}
        >
          {refreshing ? 'Đang tải…' : 'Làm mới'}
        </button>
      </div>
    </header>
  );
}
