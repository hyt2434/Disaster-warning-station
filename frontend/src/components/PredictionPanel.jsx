import { getWaterAlarmLevel, SENSOR_THRESHOLDS } from '../config/thresholds';

const PREDICTION_MINUTES = 5;
const MAXIMUM_HISTORY_POINTS = 6;
const MAXIMUM_PREDICTED_CHANGE = 10;

function getValidTemperatureReadings(readings) {
  return readings
    .filter((reading) => Number.isFinite(reading.temperature))
    .slice(0, MAXIMUM_HISTORY_POINTS)
    .reverse();
}

function calculateTemperaturePrediction(readings) {
  const temperatureReadings = getValidTemperatureReadings(readings);

  if (temperatureReadings.length < 2) {
    return null;
  }

  const firstReading = temperatureReadings[0];
  const latestReading = temperatureReadings[temperatureReadings.length - 1];
  const firstTime = new Date(firstReading.recorded_at).getTime();
  const latestTime = new Date(latestReading.recorded_at).getTime();
  const elapsedMinutes = (latestTime - firstTime) / 60000;

  if (elapsedMinutes <= 0) {
    return null;
  }

  const temperatureChange = latestReading.temperature - firstReading.temperature;
  const changePerMinute = temperatureChange / elapsedMinutes;
  const rawPredictedChange = changePerMinute * PREDICTION_MINUTES;
  const predictedChange = Math.max(
    -MAXIMUM_PREDICTED_CHANGE,
    Math.min(rawPredictedChange, MAXIMUM_PREDICTED_CHANGE),
  );
  const predictedTemperature = latestReading.temperature + predictedChange;

  return {
    currentTemperature: latestReading.temperature,
    predictedTemperature,
    temperatureReadings,
  };
}

function getTrend(currentTemperature, predictedTemperature) {
  const difference = predictedTemperature - currentTemperature;

  if (difference > 0.2) {
    return { label: 'ĐANG TĂNG', tone: 'warning' };
  }

  if (difference < -0.2) {
    return { label: 'ĐANG GIẢM', tone: 'normal' };
  }

  return { label: 'ỔN ĐỊNH', tone: 'normal' };
}

function getPredictedRisk(latestReading, predictedTemperature) {
  if (!latestReading) {
    return { label: 'CHƯA XÁC ĐỊNH', tone: 'offline' };
  }

  const status = String(latestReading.status ?? '').toLowerCase();
  const gasLevel = latestReading.gas_raw ?? 0;
  const waterAlarmLevel = getWaterAlarmLevel(latestReading);

  if (
    status === 'danger' ||
    status === 'critical' ||
    predictedTemperature >= SENSOR_THRESHOLDS.temperature.danger ||
    gasLevel >= SENSOR_THRESHOLDS.gas.danger ||
    waterAlarmLevel === 'danger'
  ) {
    return { label: 'NGUY HIỂM', tone: 'danger' };
  }

  if (
    status === 'warning' ||
    predictedTemperature >= SENSOR_THRESHOLDS.temperature.warning ||
    gasLevel >= SENSOR_THRESHOLDS.gas.warning ||
    waterAlarmLevel === 'warning'
  ) {
    return { label: 'CẢNH BÁO', tone: 'warning' };
  }

  return { label: 'AN TOÀN', tone: 'normal' };
}

function createChartPoints(temperatures, chartWidth, chartHeight) {
  const chartPadding = 24;
  const lowestTemperature = Math.min(...temperatures) - 1;
  const highestTemperature = Math.max(...temperatures) + 1;
  const temperatureRange = Math.max(highestTemperature - lowestTemperature, 1);
  const horizontalStep =
    (chartWidth - chartPadding * 2) / Math.max(temperatures.length - 1, 1);

  return temperatures.map((temperature, index) => {
    const x = chartPadding + horizontalStep * index;
    const normalizedTemperature =
      (temperature - lowestTemperature) / temperatureRange;
    const y = chartHeight - chartPadding - normalizedTemperature * (chartHeight - chartPadding * 2);

    return { x, y };
  });
}

function TemperaturePredictionChart({ actualTemperatures, predictedTemperature }) {
  const chartWidth = 620;
  const chartHeight = 210;
  const allTemperatures = [...actualTemperatures, predictedTemperature];
  const allPoints = createChartPoints(allTemperatures, chartWidth, chartHeight);
  const actualPoints = allPoints.slice(0, -1);
  const predictedPoint = allPoints[allPoints.length - 1];
  const latestActualPoint = actualPoints[actualPoints.length - 1];
  const actualPolyline = actualPoints.map((point) => `${point.x},${point.y}`).join(' ');

  return (
    <div className="prediction-chart">
      <div className="chart-legend">
        <span className="actual-legend">Dữ liệu thực tế</span>
        <span className="predicted-legend">Dự đoán sau 5 phút</span>
      </div>

      <svg viewBox={`0 0 ${chartWidth} ${chartHeight}`} role="img" aria-label="Biểu đồ dự đoán nhiệt độ">
        {[45, 85, 125, 165].map((lineY) => (
          <line key={lineY} className="chart-grid-line" x1="0" y1={lineY} x2={chartWidth} y2={lineY} />
        ))}

        <polyline className="actual-temperature-line" points={actualPolyline} />
        <line
          className="predicted-temperature-line"
          x1={latestActualPoint.x}
          y1={latestActualPoint.y}
          x2={predictedPoint.x}
          y2={predictedPoint.y}
        />

        {actualPoints.map((point, index) => (
          <circle key={`${point.x}-${point.y}`} className="actual-chart-point" cx={point.x} cy={point.y} r="4" />
        ))}
        <circle className="predicted-chart-point" cx={predictedPoint.x} cy={predictedPoint.y} r="5" />
      </svg>
    </div>
  );
}

function formatTemperature(value) {
  return Number.isFinite(value) ? `${value.toFixed(1)}°C` : '—';
}

function ModelStatus({ aiStatus, aiPrediction }) {
  const modelAvailable = aiStatus === 'available';
  const predictionLabels = {
    safe: 'AN TOÀN',
    danger: 'NGUY HIỂM',
    insufficient_data: 'THIẾU DỮ LIỆU CẢM BIẾN',
    not_run: 'CHƯA NHẬN TELEMETRY',
    unavailable: 'KHÔNG CÓ MODEL',
  };

  return (
    <div className="model-status-row">
      <span>
        Model backend: <strong className={modelAvailable ? 'text-normal' : 'text-offline'}>
          {modelAvailable ? 'ĐÃ NẠP' : 'CHƯA CÓ'}
        </strong>
      </span>
      <span>
        AI nhận định gần nhất: <strong>{predictionLabels[aiPrediction] ?? 'CHƯA XÁC ĐỊNH'}</strong>
      </span>
    </div>
  );
}

export function PredictionPanel({ readings, aiStatus, aiPrediction }) {
  const prediction = calculateTemperaturePrediction(readings);
  const latestReading = readings[0];

  if (!prediction) {
    return (
      <section className="section-block prediction-panel" id="prediction">
        <div className="section-heading">
          <div>
            <p className="eyebrow">AI/DS risk prediction</p>
            <h2>Dự đoán xu hướng dữ liệu</h2>
          </div>
          <span className="function-badge">[F5]</span>
        </div>
        <ModelStatus aiStatus={aiStatus} aiPrediction={aiPrediction} />
        <div className="empty-state">
          Cần ít nhất 2 bản ghi nhiệt độ có thời gian khác nhau để tạo dự đoán.
        </div>
      </section>
    );
  }

  const trend = getTrend(
    prediction.currentTemperature,
    prediction.predictedTemperature,
  );
  const risk = getPredictedRisk(latestReading, prediction.predictedTemperature);
  const actualTemperatures = prediction.temperatureReadings.map(
    (reading) => reading.temperature,
  );

  return (
    <section className="section-block prediction-panel" id="prediction">
      <div className="section-heading">
        <div>
          <p className="eyebrow">AI/DS risk prediction</p>
          <h2>Dự đoán xu hướng dữ liệu</h2>
        </div>
        <span className="function-badge">[F5]</span>
      </div>

      <ModelStatus aiStatus={aiStatus} aiPrediction={aiPrediction} />

      <div className="prediction-layout">
        <div>
          <div className="prediction-metrics">
            <div className="prediction-metric">
              <span>Nhiệt độ hiện tại</span>
              <strong>{formatTemperature(prediction.currentTemperature)}</strong>
            </div>
            <div className="prediction-metric">
              <span>Dự đoán sau 5 phút</span>
              <strong>{formatTemperature(prediction.predictedTemperature)}</strong>
            </div>
            <div className={`prediction-metric prediction-${trend.tone}`}>
              <span>Xu hướng</span>
              <strong>{trend.label}</strong>
            </div>
            <div className={`prediction-metric prediction-${risk.tone}`}>
              <span>Mức rủi ro dự đoán</span>
              <strong>{risk.label}</strong>
            </div>
          </div>

          <p className="prediction-explanation">
            Phương pháp demo: tính tốc độ thay đổi từ tối đa 6 bản ghi gần nhất,
            sau đó ngoại suy nhiệt độ thêm 5 phút. Gas, mực nước và trạng thái thiết bị
            được dùng để phân loại mức rủi ro.
          </p>
        </div>

        <TemperaturePredictionChart
          actualTemperatures={actualTemperatures}
          predictedTemperature={prediction.predictedTemperature}
        />
      </div>
    </section>
  );
}
