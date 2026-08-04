# Disaster Warning Station

Monorepo skeleton for an IoT disaster warning system.

## Modules

- `firmware/main-station`: ESP32 Main for temperature, humidity, smoke/gas, water level, LED warning, and buzzer.
- `firmware/shelf-node`: ESP32-C3 for MPU6050 tilt/vibration monitoring and battery support.
- `backend`: FastAPI API, MQTT client, database access, WebSocket, and alert services.
- `frontend`: React + Vite + TypeScript dashboard for charts, history, and buzzer control.
- `ai`: Python data processing, training, evaluation, and model storage.
- `infrastructure`: MQTT broker, database, and system scripts.
- `docs`: system diagrams, pin map, MQTT topics, API notes, test cases, and demo script.
- `evidence`: evidence folders for F1-F8.
- `tests`: integration and stability tests.
- `.github`: issue templates and code-check workflow.

## Security

Do not store Wi-Fi credentials, MQTT credentials, database keys, or IFTTT keys in source code. Use `.env.example` as the template for local configuration.

## Status

This repository currently contains structure and starter files only. Feature logic will be added later.