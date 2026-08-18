ROOT_TOPIC = "disaster"

MAIN_TELEMETRY_TOPIC = f"{ROOT_TOPIC}/main/telemetry"
MAIN_STATUS_TOPIC = f"{ROOT_TOPIC}/main/status"
BUZZER_COMMAND_TOPIC = f"{ROOT_TOPIC}/main/command/buzzer"
BUZZER_STATE_TOPIC = f"{ROOT_TOPIC}/main/state/buzzer"
BACKEND_STATUS_TOPIC = f"{ROOT_TOPIC}/backend/status"

F7_TELEMETRY_TOPIC = f"{ROOT_TOPIC}/f7/telemetry"
F7_STATE_TOPIC = f"{ROOT_TOPIC}/f7/state"
F7_STATUS_TOPIC = f"{ROOT_TOPIC}/f7/status"

# Tên tương thích với code tích hợp AI hiện tại.
TELEMETRY_TOPIC = MAIN_TELEMETRY_TOPIC
COMMAND_BUZZER_TOPIC = BUZZER_COMMAND_TOPIC
