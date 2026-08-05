export function MetricCard({ label, value, unit, hint }) {
  return (
    <article className="metric-card">
      <p className="card-label">{label}</p>
      <p className="metric-value">
        {value} {unit && <span>{unit}</span>}
      </p>
      <p className="card-hint">{hint}</p>
    </article>
  );
}
