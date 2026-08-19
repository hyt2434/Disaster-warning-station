import { useCallback, useEffect, useState } from 'react';
import { AlarmMonitor } from './components/AlarmMonitor';
import { DeviceControlPanel } from './components/DeviceControlPanel';
import { F7Panel } from './components/F7Panel';
import { Header } from './components/Header';
import { MonitoringFunctions } from './components/MonitoringFunctions';
import { PredictionPanel } from './components/PredictionPanel';
import { ReadingsTable } from './components/ReadingsTable';
import { SystemStatusPanel } from './components/SystemStatusPanel';
import { ThingSpeakHistoryChart } from './components/ThingSpeakHistoryChart';
import { TopNavigation } from './components/TopNavigation';
import {
  controlBuzzer,
  getHealth,
  getLatestF7Telemetry,
  getReadings,
  getThingSpeakHistory,
  getThingSpeakPrediction,
} from './services/api';

const LIVE_REFRESH_INTERVAL_MS = 2000;
const CLOUD_REFRESH_INTERVAL_MS = 15000;

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
  const [cloudHistory, setCloudHistory] = useState(null);
  const [predictionError, setPredictionError] = useState('');
  const [historyError, setHistoryError] = useState('');
  const [loading, setLoading] = useState(true);
  const [sendingCommand, setSendingCommand] = useState(false);
  const [error, setError] = useState('');
  const [message, setMessage] = useState('');

  const loadLiveData = useCallback(async (initialLoad = false) => {
    try {
      const nextHealth = await getHealth();
      setHealth(nextHealth);
      setLatestF7(await getLatestF7Telemetry());

      if (nextHealth.database === 'connected') {
        setReadings(await getReadings());
        setError('');
      } else {
        setReadings([]);
        setError('Backend chưa kết nối được PostgreSQL.');
      }
    } catch (requestError) {
      setHealth(null);
      setLatestF7(null);
      setReadings([]);
      setError(getErrorMessage(requestError, 'Không thể kết nối backend.'));
    } finally {
      if (initialLoad) {
        setLoading(false);
      }
    }
  }, []);

  const loadCloudData = useCallback(async () => {
    try {
      setCloudHistory(await getThingSpeakHistory());
      setHistoryError('');
    } catch (requestError) {
      setCloudHistory(null);
      setHistoryError(getErrorMessage(requestError, 'Không thể tải lịch sử ThingSpeak.'));
    }

    try {
      setCloudPrediction(await getThingSpeakPrediction());
      setPredictionError('');
    } catch (requestError) {
      setCloudPrediction(null);
      setPredictionError(getErrorMessage(requestError, 'Không thể tải dự đoán ThingSpeak.'));
    }
  }, []);

  useEffect(() => {
    loadLiveData(true);
    loadCloudData();

    const liveTimer = window.setInterval(loadLiveData, LIVE_REFRESH_INTERVAL_MS);
    const cloudTimer = window.setInterval(loadCloudData, CLOUD_REFRESH_INTERVAL_MS);

    return () => {
      window.clearInterval(liveTimer);
      window.clearInterval(cloudTimer);
    };
  }, [loadCloudData, loadLiveData]);

  async function sendBuzzerCommand(state) {
    setSendingCommand(true);
    setError('');
    setMessage('');

    try {
      const result = await controlBuzzer(state);
      setMessage(`${result.message} Trạng thái sẽ cập nhật ở lần polling tiếp theo.`);
    } catch (requestError) {
      setError(getErrorMessage(requestError, 'Không thể gửi lệnh điều khiển còi.'));
    } finally {
      setSendingCommand(false);
    }
  }

  const latest = readings[0];

  return (
    <div className="app-shell">
      <TopNavigation />

      <main className="dashboard">
        <Header />

        {error && <div className="notice error-notice">{error}</div>}
        {message && <div className="notice success-notice">{message}</div>}

        <AlarmMonitor health={health} latestReading={latest} />
        <MonitoringFunctions latest={latest} />
        <DeviceControlPanel
          health={health}
          sending={sendingCommand}
          onCommand={sendBuzzerCommand}
        />

        <section className="section-block" id="history" aria-labelledby="history-title">
          <div className="section-heading section-heading-with-badge">
            <span className="function-badge">[F4]</span>
            <h2 id="history-title">Lịch sử dữ liệu</h2>
          </div>
          <h3 className="history-subtitle">ThingSpeak</h3>
          <ThingSpeakHistoryChart cloudHistory={cloudHistory} historyError={historyError} />
          <h3 className="history-subtitle">PostgreSQL</h3>
          {loading ? <div className="empty-state">Đang tải dữ liệu…</div> : <ReadingsTable readings={readings} />}
        </section>

        <PredictionPanel cloudPrediction={cloudPrediction} predictionError={predictionError} />
        <F7Panel latestF7={latestF7} connectionStatus={health?.f7_device} />
        <SystemStatusPanel health={health} />
      </main>

      <footer className="page-footer">Disaster Warning Station</footer>
    </div>
  );
}
