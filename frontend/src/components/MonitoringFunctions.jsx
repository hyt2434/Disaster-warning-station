import { getWaterAlarmLevel, SENSOR_THRESHOLDS } from '../config/thresholds';

function formatValue(value, fractionDigits = 1) {
  if (value === null || value === undefined) {
    return '—';
  }

  return Number(value).toFixed(fractionDigits);
}

function getTone(value, thresholds) {
  if (value === null || value === undefined) {
    return 'offline';
  }

  if (value >= thresholds.danger) {
    return 'danger';
  }

  if (value >= thresholds.warning) {
    return 'warning';
  }

  return 'normal';
}

function getToneLabel(tone) {
  const labels = {
    normal: 'AN TOÀN',
    warning: 'CẢNH BÁO',
    danger: 'NGUY HIỂM',
    offline: 'CHƯA CÓ DỮ LIỆU',
  };

  return labels[tone];
}

function getMeterWidth(value, maximumValue) {
  if (value === null || value === undefined) {
    return 0;
  }

  return Math.max(0, Math.min((value / maximumValue) * 100, 100));
}

function FunctionHeader({ eyebrow, title, functionId }) {
  return (
    <div className="function-card-heading">
      <div>
        <p className="eyebrow">{eyebrow}</p>
        <h3>{title}</h3>
      </div>
      <span className="function-badge">{functionId}</span>
    </div>
  );
}

function SensorMeter({ value, maximumValue, tone }) {
  return (
    <div className={`sensor-meter meter-${tone}`} aria-hidden="true">
      <span style={{ width: `${getMeterWidth(value, maximumValue)}%` }} />
    </div>
  );
}

export function MonitoringFunctions({ latest }) {
  const temperatureTone = getTone(latest?.temperature, SENSOR_THRESHOLDS.temperature);
  const gasTone = getTone(latest?.gas_raw, SENSOR_THRESHOLDS.gas);
  const waterTone = getWaterAlarmLevel(latest);

  return (
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

      <div className="sensor-functions-grid">
        <article className="sensor-function-card" id="function-f1">
          <FunctionHeader
            eyebrow="Environmental monitoring"
            title="Nhiệt độ và độ ẩm"
            functionId="[F1]"
          />
          <div className="sensor-value-grid">
            <div className="sensor-value-box">
              <span>Nhiệt độ</span>
              <strong>{formatValue(latest?.temperature)}°C</strong>
            </div>
            <div className="sensor-value-box">
              <span>Độ ẩm</span>
              <strong>{formatValue(latest?.humidity)}%</strong>
            </div>
          </div>
          <div className={`function-state state-${temperatureTone}`}>
            {getToneLabel(temperatureTone)}
          </div>
          <p className="function-note">
            Cảm biến DHT11 · Cảnh báo nhiệt độ từ {SENSOR_THRESHOLDS.temperature.warning}°C.
          </p>
        </article>

        <article className="sensor-function-card" id="function-f3">
          <FunctionHeader
            eyebrow="Air safety"
            title="Khói và khí gas"
            functionId="[F3]"
          />
          <div className="single-sensor-value">
            <span>Giá trị MQ-2</span>
            <strong>{formatValue(latest?.gas_raw, 0)}</strong>
          </div>
          <SensorMeter value={latest?.gas_raw} maximumValue={1500} tone={gasTone} />
          <div className={`function-state state-${gasTone}`}>{getToneLabel(gasTone)}</div>
          <p className="function-note">
            Cảnh báo từ {SENSOR_THRESHOLDS.gas.warning}, nguy hiểm từ {SENSOR_THRESHOLDS.gas.danger}.
          </p>
        </article>

        <article className="sensor-function-card" id="function-f6">
          <FunctionHeader
            eyebrow="Flood monitoring"
            title="Mực nước và đèn cảnh báo"
            functionId="[F6]"
          />
          <div className="water-monitor-row">
            <div className="water-level-value">
              <span>Mực nước</span>
              <strong>{formatValue(latest?.water_level_cm)} cm</strong>
            </div>
            <div className="led-status" aria-label={`Đèn cảnh báo ${getToneLabel(waterTone)}`}>
              <span className={waterTone === 'normal' ? 'led-active led-green' : 'led-green'}>G</span>
              <span className={waterTone === 'warning' ? 'led-active led-yellow' : 'led-yellow'}>Y</span>
              <span className={waterTone === 'danger' ? 'led-active led-red' : 'led-red'}>R</span>
            </div>
          </div>
          <SensorMeter
            value={latest?.water_level_cm}
            maximumValue={SENSOR_THRESHOLDS.water.maximumLevel}
            tone={waterTone}
          />
          <div className={`function-state state-${waterTone}`}>{getToneLabel(waterTone)}</div>
          <p className="function-note">
            JSN-SR04T · Khoảng cách trên 40 cm: an toàn; trên 30 đến 40 cm: cảnh báo;
            từ 23 đến 30 cm: nguy hiểm.
          </p>
        </article>
      </div>
    </section>
  );
}
