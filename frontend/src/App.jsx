import { useCallback, useEffect, useState } from 'react';
import { Header } from './components/Header';
import { MetricCard } from './components/MetricCard';
import { ReadingForm } from './components/ReadingForm';
import { ReadingsTable } from './components/ReadingsTable';
import { createReading, getHealth, getReadings } from './services/api';

function valueOrDash(value, fractionDigits = 1) {
  return value === null || value === undefined ? '—' : value.toFixed(fractionDigits);
}

function getErrorMessage(error, fallbackMessage) {
  return error instanceof Error ? error.message : fallbackMessage;
}

export default function App() {
  const [health, setHealth] = useState(null);
  const [readings, setReadings] = useState([]);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState('');
  const [message, setMessage] = useState('');

  const loadDashboard = useCallback(async () => {
    setLoading(true);
    setError('');

    try {
      const nextHealth = await getHealth();
      setHealth(nextHealth);

      if (nextHealth.database === 'connected') {
        setReadings(await getReadings());
      } else {
        setReadings([]);
        setError('Backend đang chạy nhưng chưa kết nối được PostgreSQL.');
      }
    } catch (requestError) {
      setHealth(null);
      setError(getErrorMessage(requestError, 'Không thể kết nối backend.'));
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    loadDashboard();
  }, [loadDashboard]);

  async function saveReading(payload) {
    setSaving(true);
    setError('');
    setMessage('');

    try {
      await createReading(payload);
      setMessage('Đã lưu bản ghi vào PostgreSQL thành công.');
      await loadDashboard();
    } catch (requestError) {
      setError(getErrorMessage(requestError, 'Không thể lưu dữ liệu.'));
    } finally {
      setSaving(false);
    }
  }

  const latest = readings[0];

  return (
    <div className="app-shell">
      <aside className="sidebar">
        <div className="brand-mark">DWS</div>
        <nav aria-label="Điều hướng chính">
          <a className="active" href="#dashboard">Dashboard</a>
          <a href="#add-reading">Thêm dữ liệu</a>
          <a href="#history">Lịch sử</a>
        </nav>
        <p>F1 · F3 · F4<br />Basic database slice</p>
      </aside>

      <main className="dashboard" id="dashboard">
        <Header health={health} onRefresh={loadDashboard} refreshing={loading} />

        {error && <div className="notice error-notice">{error}</div>}
        {message && <div className="notice success-notice">{message}</div>}

        <section className="section-block" aria-labelledby="monitoring-title">
          <div className="section-heading">
            <div>
              <p className="eyebrow">Environmental monitoring</p>
              <h2 id="monitoring-title">Dữ liệu mới nhất</h2>
            </div>
            <span className={`reading-status status-${(latest?.status ?? 'offline').toLowerCase()}`}>
              {latest?.status ?? 'NO DATA'}
            </span>
          </div>
          <div className="metrics-grid">
            <MetricCard label="Nhiệt độ" value={valueOrDash(latest?.temperature)} unit="°C" hint="Cảm biến DHT" />
            <MetricCard label="Độ ẩm" value={valueOrDash(latest?.humidity)} unit="%" hint="Cảm biến DHT" />
            <MetricCard label="Khói / gas" value={valueOrDash(latest?.gas_raw, 0)} hint="Giá trị tương đối MQ-2" />
            <MetricCard label="Mực nước" value={valueOrDash(latest?.water_level_cm)} unit="cm" hint="Cảm biến siêu âm" />
          </div>
        </section>

        <section className="section-block" id="add-reading" aria-labelledby="form-title">
          <div className="section-heading">
            <div>
              <p className="eyebrow">Database test</p>
              <h2 id="form-title">Thêm bản ghi cảm biến</h2>
            </div>
          </div>
          <ReadingForm saving={saving} onSubmit={saveReading} />
        </section>

        <section className="section-block" id="history" aria-labelledby="history-title">
          <div className="section-heading">
            <div>
              <p className="eyebrow">Cloud storage</p>
              <h2 id="history-title">20 bản ghi gần nhất</h2>
            </div>
            <span className="row-count">{readings.length} bản ghi</span>
          </div>
          {loading ? <div className="empty-state">Đang tải dữ liệu…</div> : <ReadingsTable readings={readings} />}
        </section>
      </main>
    </div>
  );
}
