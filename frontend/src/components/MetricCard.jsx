export function MetricCard({ label, value, unit, hint, tone = 'normal', meter = 0 }) {
  const safeMeter = Math.max(0, Math.min(meter, 100));

  return (
    <article className={`metric-card metric-${tone}`}>
      <div className="metric-card-header">
        <p className="card-label">{label}</p>
        <span className="sensor-dot" aria-hidden="true" />
      </div>
      <p className="metric-value">
        {value} {unit && <span>{unit}</span>}
      </p>
      <div className="metric-meter" aria-hidden="true">
        <span style={{ width: `${safeMeter}%` }} />
      </div>
      <p className="card-hint">{hint}</p>
    </article>
  );
}
