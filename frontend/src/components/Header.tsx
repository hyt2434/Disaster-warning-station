import type { HealthStatus } from '../types';

interface HeaderProps {
  health: HealthStatus | null;
  onRefresh: () => void;
  refreshing: boolean;
}

export function Header({ health, onRefresh, refreshing }: HeaderProps) {
  const databaseConnected = health?.database === 'connected';

  return (
    <header className="topbar">
      <div>
        <p className="eyebrow">IoT monitoring project</p>
        <h1>Disaster Warning Station</h1>
        <p className="subtitle">Giai đoạn 1 · Website kết nối PostgreSQL</p>
      </div>
      <div className="header-actions">
        <span className={`connection-badge ${health ? 'online' : 'offline'}`}>
          <span className="status-dot" /> Backend {health ? 'ONLINE' : 'OFFLINE'}
        </span>
        <span className={`connection-badge ${databaseConnected ? 'online' : 'offline'}`}>
          <span className="status-dot" /> Database {databaseConnected ? 'CONNECTED' : 'DISCONNECTED'}
        </span>
        <button className="secondary-button" type="button" onClick={onRefresh} disabled={refreshing}>
          {refreshing ? 'Đang tải…' : 'Làm mới'}
        </button>
      </div>
    </header>
  );
}
