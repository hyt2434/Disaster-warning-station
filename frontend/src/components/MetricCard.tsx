interface MetricCardProps {
  label: string;
  value: string;
  unit?: string;
  hint: string;
}

export function MetricCard({ label, value, unit, hint }: MetricCardProps) {
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
