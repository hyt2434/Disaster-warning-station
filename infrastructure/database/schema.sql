CREATE TABLE IF NOT EXISTS devices (
    id SERIAL PRIMARY KEY,
    device_id VARCHAR(100) UNIQUE NOT NULL,
    device_name VARCHAR(150) NOT NULL,
    device_type VARCHAR(50) NOT NULL DEFAULT 'main_station',
    online BOOLEAN NOT NULL DEFAULT TRUE,
    last_seen TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS ix_devices_device_id ON devices (device_id);

CREATE TABLE IF NOT EXISTS sensor_readings (
    id SERIAL PRIMARY KEY,
    device_id VARCHAR(100) NOT NULL REFERENCES devices(device_id) ON DELETE CASCADE,
    temperature DOUBLE PRECISION,
    humidity DOUBLE PRECISION,
    gas_raw INTEGER,
    gas_filtered DOUBLE PRECISION,
    distance_cm DOUBLE PRECISION,
    water_level_cm DOUBLE PRECISION,
    water_level_percent DOUBLE PRECISION,
    angle_x DOUBLE PRECISION,
    angle_y DOUBLE PRECISION,
    vibration DOUBLE PRECISION,
    battery_percentage DOUBLE PRECISION,

    -- Trạng thái do ESP32 Main gửi trong mỗi telemetry.
    motion_status VARCHAR(30),
    status VARCHAR(30) NOT NULL DEFAULT 'NORMAL',
    buzzer BOOLEAN,
    buzzer_muted BOOLEAN,
    recorded_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS ix_sensor_readings_device_id ON sensor_readings (device_id);
CREATE INDEX IF NOT EXISTS ix_sensor_readings_recorded_at ON sensor_readings (recorded_at DESC);
CREATE INDEX IF NOT EXISTS ix_sensor_readings_device_recorded
    ON sensor_readings (device_id, recorded_at DESC);
