-- Schema tham khảo / thiết lập thủ công cho một database PostgreSQL trống.
-- Runtime source of truth là SQLAlchemy models + create_tables().
-- create_tables() không migrate table cũ; database dùng schema cũ phải được
-- recreate hoặc migrate thủ công trước khi chạy backend mới.

BEGIN;

CREATE TABLE sensor_readings (
    id SERIAL PRIMARY KEY,
    device_id VARCHAR(100) NOT NULL,
    temperature DOUBLE PRECISION,
    humidity DOUBLE PRECISION,
    gas_average DOUBLE PRECISION,
    distance_cm DOUBLE PRECISION,
    water_level_cm DOUBLE PRECISION,
    -- Dữ liệu F7 mới nhất được ghép vào cùng bản ghi Main.
    f7_roll DOUBLE PRECISION,
    f7_pitch DOUBLE PRECISION,
    f7_tilt DOUBLE PRECISION,
    f7_vibration DOUBLE PRECISION,
    f7_impact DOUBLE PRECISION,
    f7_status VARCHAR(30),

    -- Trạng thái tổng hợp và trạng thái còi của Main.
    status VARCHAR(30) NOT NULL DEFAULT 'SAFE',
    buzzer BOOLEAN,
    buzzer_muted BOOLEAN,
    recorded_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX ix_sensor_readings_device_id ON sensor_readings (device_id);
CREATE INDEX ix_sensor_readings_recorded_at ON sensor_readings (recorded_at DESC);
CREATE INDEX ix_sensor_readings_device_recorded
    ON sensor_readings (device_id, recorded_at DESC);

COMMIT;
