const STATUS_LABELS = {
  SAFE: 'SAFE',
  WARNING: 'WARNING',
  DANGER: 'DANGER',
  INSUFFICIENT_DATA: 'CHƯA ĐỦ DỮ LIỆU',
};

const STATUS_VALUE_LABELS = {
  0: 'SAFE',
  1: 'WARNING',
  2: 'DANGER',
};

function formatCauseValue(cause) {
  if (cause.unit === 'status') {
    return STATUS_VALUE_LABELS[Math.round(cause.predicted_value)] ?? 'UNKNOWN';
  }

  const fractionDigits = cause.unit === 'ADC' ? 0 : 1;
  return `${Number(cause.predicted_value).toFixed(fractionDigits)} ${cause.unit}`;
}

export function PredictionPanel({ cloudPrediction, predictionError }) {
  const status = cloudPrediction?.system_prediction ?? 'INSUFFICIENT_DATA';
  const causes = cloudPrediction?.causes ?? [];
  const tone = status === 'DANGER'
    ? 'danger'
    : status === 'WARNING'
      ? 'warning'
      : status === 'SAFE'
        ? 'normal'
        : 'offline';

  return (
    <section className="section-block" id="prediction" aria-labelledby="prediction-title">
      <div className="section-heading section-heading-with-badge">
        <span className="function-badge">[F5]</span>
        <h2 id="prediction-title">Dự đoán trạng thái sau 5 phút</h2>
      </div>

      {predictionError ? (
        <div className="empty-state">{predictionError}</div>
      ) : (
        <>
          <div className={`prediction-summary prediction-result-${tone}`}>
            <div><span>Kết quả</span><strong>{STATUS_LABELS[status] ?? status}</strong></div>
            <div><span>Nguồn</span><strong>ThingSpeak</strong></div>
            <div><span>Số mẫu</span><strong>{cloudPrediction?.sample_count ?? 0}</strong></div>
            <div><span>Model</span><strong>{cloudPrediction?.model_available ? 'Random Forest' : 'Unavailable'}</strong></div>
          </div>

          {causes.length > 0 && (
            <div className="prediction-causes-section">
              <h3>Nguyên nhân dự đoán</h3>
              <ul className="prediction-cause-list">
                {causes.map((cause) => (
                  <li className={`prediction-cause prediction-cause-${cause.level.toLowerCase()}`} key={cause.field}>
                    <strong>{cause.sensor}</strong>
                    <span>{formatCauseValue(cause)}</span>
                  </li>
                ))}
              </ul>
            </div>
          )}
        </>
      )}
    </section>
  );
}
