// Các ngưỡng này phải giống firmware/main_station/main_station.ino.
export const SENSOR_THRESHOLDS = {
  temperature: { warning: 35, danger: 40 },
  gas: { warning: 1300, danger: 1600 },
  water: {
    sensorHeight: 100,
    minimumValidDistance: 23,
    warningDistance: 40,
    dangerDistance: 30,
    maximumLevel: 77,
  },
};

export function getWaterDistance(reading) {
  if (Number.isFinite(reading?.distance_cm)) {
    return reading.distance_cm;
  }

  if (Number.isFinite(reading?.water_level_cm)) {
    return SENSOR_THRESHOLDS.water.sensorHeight - reading.water_level_cm;
  }

  return null;
}

export function getWaterAlarmLevel(reading) {
  const distance = getWaterDistance(reading);

  if (distance === null) {
    return 'offline';
  }

  if (distance <= SENSOR_THRESHOLDS.water.dangerDistance) {
    return 'danger';
  }

  if (distance <= SENSOR_THRESHOLDS.water.warningDistance) {
    return 'warning';
  }

  return 'normal';
}
