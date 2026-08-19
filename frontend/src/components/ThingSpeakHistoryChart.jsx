import { useState } from 'react';


const FIELD_OPTIONS = [
  { value: 'field1', label: 'Nhiệt độ', unit: '°C' },
  { value: 'field2', label: 'Độ ẩm', unit: '%' },
  { value: 'field3', label: 'Khói / gas', unit: 'ADC' },
  { value: 'field4', label: 'Mực nước', unit: 'cm' },
  { value: 'field5', label: 'Chuyển động F7', unit: 'status' },
  { value: 'field6', label: 'Trạng thái hệ thống', unit: 'status' },
  { value: 'field7', label: 'F7 - Rung', unit: 'm/s²' },
];

const STATUS_LABELS = {
  0: 'AN TOÀN',
  1: 'CẢNH BÁO',
  2: 'NGUY HIỂM',
};

function formatTime(dateValue) {
  return new Intl.DateTimeFormat('vi-VN', {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  }).format(new Date(dateValue));
}

function formatValue(value, unit) {
  if (unit === 'status') {
    return STATUS_LABELS[Math.round(value)] ?? 'CHƯA XÁC ĐỊNH';
  }

  const fractionDigits = unit === 'ADC' ? 0 : 1;
  return `${Number(value).toFixed(fractionDigits)} ${unit}`;
}

function createChartPoints(readings, fieldName, isStatus) {
  const chartWidth = 900;
  const chartHeight = 260;
  const horizontalPadding = 48;
  const verticalPadding = 32;

  const validReadings = readings
    .map((reading) => {
      const rawValue = reading[fieldName];

      return {
        time: reading.recorded_at
          ? new Date(reading.recorded_at).getTime()
          : Number.NaN,
        value: rawValue === null || rawValue === undefined
          ? Number.NaN
          : Number(rawValue),
      };
    })
    .filter((reading) => Number.isFinite(reading.time) && Number.isFinite(reading.value))
    .sort((firstReading, secondReading) => firstReading.time - secondReading.time);

  if (validReadings.length === 0) {
    return null;
  }

  const firstTime = validReadings[0].time;
  const latestTime = validReadings[validReadings.length - 1].time;
  const timeRange = Math.max(latestTime - firstTime, 1);
  const values = validReadings.map((reading) => reading.value);

  let minimumValue = Math.min(...values);
  let maximumValue = Math.max(...values);

  if (isStatus) {
    minimumValue = 0;
    maximumValue = 2;
  } else {
    const valuePadding = Math.max((maximumValue - minimumValue) * 0.15, 0.5);
    minimumValue -= valuePadding;
    maximumValue += valuePadding;
  }

  const valueRange = Math.max(maximumValue - minimumValue, 1);
  const points = validReadings.map((reading) => {
    const xRatio = (reading.time - firstTime) / timeRange;
    const yRatio = (reading.value - minimumValue) / valueRange;

    return {
      x: horizontalPadding + xRatio * (chartWidth - horizontalPadding * 2),
      y: chartHeight - verticalPadding - yRatio * (chartHeight - verticalPadding * 2),
    };
  });

  return {
    chartWidth,
    chartHeight,
    points,
    firstRecordedAt: validReadings[0].time,
    latestRecordedAt: validReadings[validReadings.length - 1].time,
    latestValue: validReadings[validReadings.length - 1].value,
    minimumValue,
    maximumValue,
  };
}

export function ThingSpeakHistoryChart({ cloudHistory, historyError }) {
  const [selectedField, setSelectedField] = useState('field1');
  const selectedOption = FIELD_OPTIONS.find((option) => option.value === selectedField);
  const readings = cloudHistory?.readings ?? [];
  const sampleCount = cloudHistory?.sample_count ?? 0;
  const chartData = createChartPoints(
    readings,
    selectedOption.value,
    selectedOption.unit === 'status',
  );

  if (historyError) {
    return <div className="empty-state">{historyError}</div>;
  }

  if (!chartData) {
    return <div className="empty-state">ThingSpeak chưa có dữ liệu để vẽ biểu đồ.</div>;
  }

  const polylinePoints = chartData.points
    .map((point) => `${point.x},${point.y}`)
    .join(' ');

  return (
    <div className="thingspeak-history-chart">
      <div className="cloud-chart-header">
        <div>
          <span>Dữ liệu mới nhất từ Cloud</span>
          <strong>{formatValue(chartData.latestValue, selectedOption.unit)}</strong>
        </div>
        <label>
          Chọn dữ liệu
          <select value={selectedField} onChange={(event) => setSelectedField(event.target.value)}>
            {FIELD_OPTIONS.map((option) => (
              <option key={option.value} value={option.value}>
                {option.label}
              </option>
            ))}
          </select>
        </label>
      </div>

      <svg
        viewBox={`0 0 ${chartData.chartWidth} ${chartData.chartHeight}`}
        role="img"
        aria-label={`Biểu đồ lịch sử ${selectedOption.label} từ ThingSpeak`}
      >
        {[55, 105, 155, 205].map((lineY) => (
          <line
            key={lineY}
            className="cloud-history-grid-line"
            x1="48"
            y1={lineY}
            x2={chartData.chartWidth - 48}
            y2={lineY}
          />
        ))}

        <polyline className="cloud-history-line" points={polylinePoints} />
        {chartData.points.map((point, index) => (
          <circle
            key={`${point.x}-${point.y}-${index}`}
            className="cloud-history-point"
            cx={point.x}
            cy={point.y}
            r="3.5"
          />
        ))}

        <text className="cloud-axis-label" x="48" y={chartData.chartHeight - 6}>
          {formatTime(chartData.firstRecordedAt)}
        </text>
        <text
          className="cloud-axis-label cloud-axis-label-end"
          x={chartData.chartWidth - 48}
          y={chartData.chartHeight - 6}
        >
          {formatTime(chartData.latestRecordedAt)}
        </text>
      </svg>

      <p>
        Nguồn: ThingSpeak Cloud · {sampleCount} bản ghi gần nhất ·
        Cloud cập nhật khoảng 15 giây/lần.
      </p>
    </div>
  );
}
