// Các ngưỡng này phải giống firmware/main_station/main_station.ino.
export const SENSOR_THRESHOLDS = {
  temperature: { warning: 35, danger: 40 },
  gas: { warning: 700, danger: 1000 },
  water: { warning: 20, danger: 40 },
};
