-- Chạy toàn bộ file này đúng một lần trên database PostgreSQL mới.
-- File đã chứa cấu trúc cuối cùng, không cần chạy thêm ALTER TABLE.

BEGIN;

CREATE TABLE devices (
    id SERIAL PRIMARY KEY,
    device_id VARCHAR(100) UNIQUE NOT NULL,
    device_name VARCHAR(150) NOT NULL,
    device_type VARCHAR(50) NOT NULL DEFAULT 'main_station',
    online BOOLEAN NOT NULL DEFAULT TRUE,
    last_seen TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE sensor_readings (
    id SERIAL PRIMARY KEY,
    device_id VARCHAR(100) NOT NULL REFERENCES devices(device_id) ON DELETE CASCADE,
    temperature DOUBLE PRECISION,
    humidity DOUBLE PRECISION,
    gas_raw INTEGER,
    gas_filtered DOUBLE PRECISION,
    distance_cm DOUBLE PRECISION,
    water_level_cm DOUBLE PRECISION,
    water_level_percent DOUBLE PRECISION,
    -- Dữ liệu F7 mới nhất được ghép vào mỗi bản ghi Main.
    angle_x DOUBLE PRECISION,          -- Roll của MPU6050
    angle_y DOUBLE PRECISION,          -- Pitch của MPU6050
    vibration DOUBLE PRECISION,        -- Độ rung của MPU6050

    -- Trạng thái do ESP32 Main gửi trong mỗi telemetry.
    motion_status VARCHAR(30),
    status VARCHAR(30) NOT NULL DEFAULT 'NORMAL',
    buzzer BOOLEAN,
    buzzer_muted BOOLEAN,
    recorded_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX ix_sensor_readings_device_id ON sensor_readings (device_id);
CREATE INDEX ix_sensor_readings_recorded_at ON sensor_readings (recorded_at DESC);
CREATE INDEX ix_sensor_readings_device_recorded
    ON sensor_readings (device_id, recorded_at DESC);

CREATE TABLE f7_readings (
    id SERIAL PRIMARY KEY,
    device_id VARCHAR(100) NOT NULL REFERENCES devices(device_id) ON DELETE CASCADE,
    roll DOUBLE PRECISION,
    pitch DOUBLE PRECISION,
    tilt DOUBLE PRECISION,
    vibration DOUBLE PRECISION,
    impact DOUBLE PRECISION,
    status VARCHAR(30) NOT NULL DEFAULT 'NORMAL',
    recorded_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX ix_f7_readings_device_id ON f7_readings (device_id);
CREATE INDEX ix_f7_readings_recorded_at ON f7_readings (recorded_at DESC);
CREATE INDEX ix_f7_readings_device_recorded
    ON f7_readings (device_id, recorded_at DESC);

COMMIT;
