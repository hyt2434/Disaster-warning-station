import { useCallback, useEffect, useState } from 'react';
import { AlarmMonitor } from './components/AlarmMonitor';
import { Header } from './components/Header';
import { MetricCard } from './components/MetricCard';
import { ReadingForm } from './components/ReadingForm';
import { ReadingsTable } from './components/ReadingsTable';
import { TopNavigation } from './components/TopNavigation';
import { createReading, getHealth, getReadings } from './services/api';

const AUTO_REFRESH_INTERVAL_MS = 5000;

function valueOrDash(value, fractionDigits = 1) {
  return value === null || value === undefined ? '—' : value.toFixed(fractionDigits);
}

function getErrorMessage(error, fallbackMessage) {
  if (error instanceof TypeError) {
    return fallbackMessage;
  }

  return error instanceof Error ? error.message : fallbackMessage;
}

function getSensorTone(value, warningLevel, dangerLevel) {
  if (value === null || value === undefined) {
    return 'offline';
  }

  if (value >= dangerLevel) {
    return 'danger';
  }

  if (value >= warningLevel) {
    return 'warning';
  }

  return 'normal';
}

function toMeterPercent(value, maximumValue) {
  if (value === null || value === undefined) {
    return 0;
  }

  return (value / maximumValue) * 100;
}

export default function App() {
  const [health, setHealth] = useState(null);
  const [readings, setReadings] = useState([]);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState('');
  const [message, setMessage] = useState('');

  const loadDashboard = useCallback(async (showLoading = true) => {
    if (showLoading) {
      setLoading(true);
    }

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
      if (showLoading) {
        setLoading(false);
      }
    }
  }, []);

  useEffect(() => {
    loadDashboard();

    const refreshTimer = window.setInterval(() => {
      loadDashboard(false);
    }, AUTO_REFRESH_INTERVAL_MS);

    return () => window.clearInterval(refreshTimer);
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
  const backendOnline = health?.backend === 'online';

  return (
    <div className="app-shell">
      <TopNavigation />

      <main className="dashboard">
        <Header health={health} onRefresh={loadDashboard} refreshing={loading} />

        {error && <div className="notice error-notice">{error}</div>}
        {message && <div className="notice success-notice">{message}</div>}

        <AlarmMonitor latestReading={latest} backendOnline={backendOnline} />

        <section className="section-block" id="monitoring" aria-labelledby="monitoring-title">
          <div className="section-heading">
            <div>
              <p className="eyebrow">Real-time monitoring</p>
              <h2 id="monitoring-title">Dữ liệu cảm biến mới nhất</h2>
            </div>
            <span className={`reading-status status-${(latest?.status ?? 'offline').toLowerCase()}`}>
              {latest?.status ?? 'NO DATA'}
            </span>
          </div>
          <div className="metrics-grid">
            <MetricCard
              label="Nhiệt độ"
              value={valueOrDash(latest?.temperature)}
              unit="°C"
              hint="DHT22 · Cảnh báo từ 40°C"
              tone={getSensorTone(latest?.temperature, 40, 50)}
              meter={toMeterPercent(latest?.temperature, 70)}
            />
            <MetricCard
              label="Độ ẩm"
              value={valueOrDash(latest?.humidity)}
              unit="%"
              hint="DHT22 · Độ ẩm môi trường"
              tone={latest ? 'normal' : 'offline'}
              meter={toMeterPercent(latest?.humidity, 100)}
            />
            <MetricCard
              label="Khói / gas"
              value={valueOrDash(latest?.gas_raw, 0)}
              hint="MQ-2 · Cảnh báo từ 500"
              tone={getSensorTone(latest?.gas_raw, 500, 700)}
              meter={toMeterPercent(latest?.gas_raw, 1000)}
            />
            <MetricCard
              label="Mực nước"
              value={valueOrDash(latest?.water_level_cm)}
              unit="cm"
              hint="Siêu âm · Cảnh báo từ 25 cm"
              tone={getSensorTone(latest?.water_level_cm, 25, 40)}
              meter={toMeterPercent(latest?.water_level_cm, 50)}
            />
          </div>
        </section>

        <section className="content-grid">
          <article className="section-block" id="system-status" aria-labelledby="system-title">
            <div className="section-heading">
              <div>
                <p className="eyebrow">System flow</p>
                <h2 id="system-title">Luồng hoạt động của hệ thống</h2>
              </div>
            </div>
            <div className="system-flow">
              <span>ESP32</span>
              <strong>→</strong>
              <span>MQTT</span>
              <strong>→</strong>
              <span>Backend</span>
              <strong>→</strong>
              <span>Database</span>
              <strong>→</strong>
              <span>Frontend</span>
            </div>
            <p className="flow-description">
              Backend nhận telemetry từ ESP32, lưu vào PostgreSQL và MongoDB, sau đó frontend
              đọc dữ liệu qua REST API để cập nhật monitor mỗi 5 giây.
            </p>
          </article>

          <article className="section-block compact-section" id="add-reading" aria-labelledby="form-title">
            <div className="section-heading">
              <div>
                <p className="eyebrow">Demo database</p>
                <h2 id="form-title">Nhập dữ liệu thử</h2>
              </div>
            </div>
            <p className="form-explanation">
              Dùng biểu mẫu này khi chưa bật ESP32 nhưng vẫn muốn kiểm tra monitor.
            </p>
          </article>
        </section>

        <section className="section-block form-section" aria-label="Biểu mẫu nhập dữ liệu">
          <ReadingForm saving={saving} onSubmit={saveReading} />
        </section>

        <section className="section-block" id="history" aria-labelledby="history-title">
          <div className="section-heading">
            <div>
              <p className="eyebrow">Historical data</p>
              <h2 id="history-title">20 bản ghi cảm biến gần nhất</h2>
            </div>
            <span className="row-count">{readings.length} bản ghi</span>
          </div>
          {loading ? <div className="empty-state">Đang tải dữ liệu…</div> : <ReadingsTable readings={readings} />}
        </section>
      </main>

      <footer className="page-footer">
        Disaster Warning Station · Frontend demo kết nối FastAPI
      </footer>
    </div>
  );
}
