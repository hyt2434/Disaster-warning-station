function formatValue(value, unit, fractionDigits = 1) {
  if (value === null || value === undefined) {
    return '—';
  }

  return `${Number(value).toFixed(fractionDigits)} ${unit}`;
}

function SensorCard({ functionId, title, children }) {
  return (
    <article className="sensor-function-card">
      <div className="function-card-heading">
        <span className="function-badge">[{functionId}]</span>
        <h3>{title}</h3>
      </div>
      {children}
    </article>
  );
}

export function MonitoringFunctions({ latest }) {
  return (
    <section className="section-block" id="monitoring" aria-labelledby="monitoring-title">
      <div className="section-heading">
        <h2 id="monitoring-title">Cảm biến</h2>
      </div>

      <div className="sensor-functions-grid">
        <SensorCard functionId="F1" title="Nhiệt độ và độ ẩm">
          <div className="sensor-value-grid">
            <div className="sensor-value-box">
              <span>Nhiệt độ</span>
              <strong>{formatValue(latest?.temperature, '°C')}</strong>
            </div>
            <div className="sensor-value-box">
              <span>Độ ẩm</span>
              <strong>{formatValue(latest?.humidity, '%')}</strong>
            </div>
          </div>
        </SensorCard>

        <SensorCard functionId="F3" title="Khói và khí gas">
          <div className="single-sensor-value">
            <span>MQ-2</span>
            <strong>{formatValue(latest?.gas_average, 'ADC', 0)}</strong>
          </div>
        </SensorCard>

        <SensorCard functionId="F6" title="Mực nước">
          <div className="single-sensor-value">
            <span>JSN-SR04T</span>
            <strong>{formatValue(latest?.water_level_cm, 'cm')}</strong>
          </div>
        </SensorCard>
      </div>
    </section>
  );
}
