import { useCallback, useEffect, useState } from 'react';
import { AlarmMonitor } from './components/AlarmMonitor';
import { DeviceControlPanel } from './components/DeviceControlPanel';
import { F7Panel } from './components/F7Panel';
import { Header } from './components/Header';
import { MonitoringFunctions } from './components/MonitoringFunctions';
import { PredictionPanel } from './components/PredictionPanel';
import { ReadingForm } from './components/ReadingForm';
import { ReadingsTable } from './components/ReadingsTable';
import { SystemStatusPanel } from './components/SystemStatusPanel';
import { ThingSpeakHistoryChart } from './components/ThingSpeakHistoryChart';
import { TopNavigation } from './components/TopNavigation';
import {
  controlBuzzer,
  createReading,
  getHealth,
  getLatestF7Reading,
  getReadings,
  getThingSpeakPrediction,
  getThingSpeakHistory,
} from './services/api';

const AUTO_REFRESH_INTERVAL_MS = 2000;

function getErrorMessage(error, fallbackMessage) {
  if (error instanceof TypeError) {
    return fallbackMessage;
  }

  return error instanceof Error ? error.message : fallbackMessage;
}

export default function App() {
  const [health, setHealth] = useState(null);
  const [readings, setReadings] = useState([]);
  const [latestF7, setLatestF7] = useState(null);
  const [cloudPrediction, setCloudPrediction] = useState(null);
  const [predictionError, setPredictionError] = useState('');
  const [cloudHistory, setCloudHistory] = useState(null);
  const [historyError, setHistoryError] = useState('');
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [sendingCommand, setSendingCommand] = useState(false);
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
      setLatestF7(await getLatestF7Reading());

      try {
        setCloudPrediction(await getThingSpeakPrediction());
        setPredictionError('');
      } catch (predictionRequestError) {
        setCloudPrediction(null);
        setPredictionError(
          getErrorMessage(
            predictionRequestError,
            'Không thể dự đoán dữ liệu từ ThingSpeak.',
          ),
        );
      }

      try {
        setCloudHistory(await getThingSpeakHistory());
        setHistoryError('');
      } catch (historyRequestError) {
        setCloudHistory(null);
        setHistoryError(
          getErrorMessage(
            historyRequestError,
            'Không thể tải lịch sử từ ThingSpeak.',
          ),
        );
      }

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

  async function sendBuzzerCommand(state) {
    setSendingCommand(true);
    setError('');
    setMessage('');

    try {
      const result = await controlBuzzer(state);
      setMessage(`${result.message} Đang chờ ESP32 phản hồi trạng thái thực tế.`);
      await loadDashboard(false);
    } catch (requestError) {
      setError(getErrorMessage(requestError, 'Không thể gửi lệnh điều khiển còi.'));
    } finally {
      setSendingCommand(false);
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

        <MonitoringFunctions latest={latest} />

        <DeviceControlPanel
          health={health}
          latestReading={latest}
          sending={sendingCommand}
          onCommand={sendBuzzerCommand}
        />

        <PredictionPanel
          cloudPrediction={cloudPrediction}
          predictionError={predictionError}
        />

        <section className="section-block cloud-data-section" id="history" aria-labelledby="history-title">
          <div className="section-heading">
            <div>
              <p className="eyebrow">Historical data</p>
              <h2 id="history-title">Lịch sử PostgreSQL và dữ liệu ThingSpeak</h2>
            </div>
            <div className="heading-badges">
              <span className="row-count">{readings.length} bản ghi</span>
              <span className="function-badge">[F4]</span>
            </div>
          </div>
          <p className="function-note history-note">
            Monitor đọc dữ liệu PostgreSQL mỗi 2 giây. Biểu đồ dưới đây được tải trực tiếp từ ThingSpeak Cloud.
          </p>
          <h3 className="history-subtitle">Biểu đồ lịch sử ThingSpeak</h3>
          <ThingSpeakHistoryChart
            cloudHistory={cloudHistory}
            historyError={historyError}
          />
          <h3 className="history-subtitle">Nhật ký dữ liệu PostgreSQL</h3>
          {loading ? <div className="empty-state">Đang tải dữ liệu…</div> : <ReadingsTable readings={readings} />}
        </section>

        <F7Panel latestF7={latestF7} connectionStatus={health?.f7_device} />

        <SystemStatusPanel health={health} />

        <section className="section-block form-section" id="add-reading" aria-labelledby="form-title">
          <div className="section-heading">
            <div>
              <p className="eyebrow">Demo database</p>
              <h2 id="form-title">Nhập dữ liệu thử</h2>
            </div>
          </div>
          <p className="form-explanation">
            Dùng biểu mẫu này khi chưa bật ESP32 nhưng vẫn muốn kiểm tra monitor và dự đoán.
          </p>
          <ReadingForm saving={saving} onSubmit={saveReading} />
        </section>
      </main>

      <footer className="page-footer">
        Disaster Warning Station · Frontend demo kết nối FastAPI
      </footer>
    </div>
  );
}
