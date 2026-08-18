const PREDICTION_CONTENT = {
  NORMAL: {
    label: 'DỰ ĐOÁN AN TOÀN',
    description: 'Các dữ liệu dự báo sau 5 phút vẫn nằm trong giới hạn an toàn.',
    tone: 'normal',
  },
  WARNING: {
    label: 'DỰ ĐOÁN CẢNH BÁO',
    description: 'Có dữ liệu dự báo tiến vào mức cảnh báo. Cần tiếp tục theo dõi.',
    tone: 'warning',
  },
  DANGER: {
    label: 'DỰ ĐOÁN NGUY HIỂM',
    description: 'Mô hình dự đoán hệ thống có khả năng chuyển sang mức nguy hiểm.',
    tone: 'danger',
  },
  INSUFFICIENT_DATA: {
    label: 'CHƯA ĐỦ DỮ LIỆU',
    description: 'ThingSpeak chưa có đủ dữ liệu hợp lệ để chạy dự đoán.',
    tone: 'offline',
  },
};

const STATUS_VALUE_LABELS = {
  0: 'AN TOÀN',
  1: 'CẢNH BÁO',
  2: 'NGUY HIỂM',
};

function formatCauseValue(cause) {
  if (cause.unit === 'status') {
    return STATUS_VALUE_LABELS[Math.round(cause.predicted_value)] ?? 'CHƯA XÁC ĐỊNH';
  }

  const fractionDigits = cause.unit === 'ADC' ? 0 : 1;
  return `${Number(cause.predicted_value).toFixed(fractionDigits)} ${cause.unit}`;
}

function formatThreshold(cause) {
  if (cause.unit === 'status') {
    return STATUS_VALUE_LABELS[Math.round(cause.threshold)] ?? cause.threshold;
  }

  return `${cause.threshold} ${cause.unit}`;
}

function PredictionCause({ cause }) {
  const tone = cause.level === 'DANGER' ? 'danger' : 'warning';

  return (
    <li className={`prediction-cause prediction-cause-${tone}`}>
      <div>
        <span>{cause.field.toUpperCase()}</span>
        <strong>{cause.sensor}</strong>
      </div>
      <div>
        <span>Dự đoán sau 5 phút</span>
        <strong>{formatCauseValue(cause)}</strong>
      </div>
      <div>
        <span>Ngưỡng {cause.level === 'DANGER' ? 'nguy hiểm' : 'cảnh báo'}</span>
        <strong>{formatThreshold(cause)}</strong>
      </div>
    </li>
  );
}

export function PredictionPanel({ cloudPrediction, predictionError }) {
  const predictionStatus = cloudPrediction?.system_prediction ?? 'INSUFFICIENT_DATA';
  const content = PREDICTION_CONTENT[predictionStatus] ?? PREDICTION_CONTENT.INSUFFICIENT_DATA;
  const causes = cloudPrediction?.causes ?? [];

  return (
    <section className="section-block prediction-panel" id="prediction">
      <div className="section-heading">
        <div>
          <p className="eyebrow">ThingSpeak + AI model</p>
          <h2>Dự đoán trạng thái hệ thống sau 5 phút</h2>
        </div>
        <span className="function-badge">[F5]</span>
      </div>

      {predictionError && <div className="empty-state">{predictionError}</div>}

      {!predictionError && (
        <>
          <div className={`system-prediction-result prediction-result-${content.tone}`}>
            <div>
              <span>Kết quả tổng hợp</span>
              <strong>{content.label}</strong>
              <p>{content.description}</p>
            </div>
            <div className="prediction-model-summary">
              <span>Nguồn</span>
              <strong>ThingSpeak</strong>
              <span>Số mẫu</span>
              <strong>{cloudPrediction?.sample_count ?? 0}</strong>
              <span>Model Random Forest</span>
              <strong>{cloudPrediction?.model_available ? 'ĐÃ SỬ DỤNG' : 'CHƯA CÓ'}</strong>
            </div>
          </div>

          {causes.length > 0 && (
            <div className="prediction-causes-section">
              <h3>Dữ liệu có khả năng gây cảnh báo</h3>
              <ul className="prediction-cause-list">
                {causes.map((cause) => (
                  <PredictionCause key={cause.field} cause={cause} />
                ))}
              </ul>
            </div>
          )}

          {predictionStatus === 'NORMAL' && (
            <div className="prediction-safe-note">
              Không có nhiệt độ, gas, mực nước hoặc chuyển động nào được dự đoán vượt ngưỡng cảnh báo.
            </div>
          )}

          <p className="prediction-explanation">
            Backend lấy dữ liệu lịch sử ThingSpeak, ước lượng giá trị sau 5 phút và đưa
            nhiệt độ, độ ẩm, gas, mực nước vào model được tạo bởi thư mục ai.
            Trạng thái rung/nghiêng được kiểm tra riêng vì model môi trường không có cảm biến này.
          </p>
        </>
      )}
    </section>
  );
}
