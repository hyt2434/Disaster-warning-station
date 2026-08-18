import logging
import threading
import time
from collections import deque
from typing import Any

from pymongo import MongoClient
from pymongo.collection import Collection
from pymongo.errors import PyMongoError

from ..config import settings


logger = logging.getLogger("uvicorn.error")


class MongoTelemetryStore:
    def __init__(self, retry_interval_seconds: float = 10.0) -> None:
        self._client: MongoClient | None = None
        self._collection: Collection | None = None
        self._connected = False
        self._last_connect_attempt = 0.0
        self._retry_interval_seconds = retry_interval_seconds
        self._pending: deque[dict[str, Any]] = deque(maxlen=10000)
        self._lock = threading.Lock()
        self._missing_config_logged = False

    @property
    def status(self) -> str:
        if not settings.mongodb_uri:
            return "not_configured"
        return "connected" if self._connected else "disconnected"

    @property
    def pending_count(self) -> int:
        with self._lock:
            return len(self._pending)

    def connect(self) -> bool:
        with self._lock:
            return self._connect_locked()

    def store(self, record: dict[str, Any]) -> bool:
        with self._lock:
            if not self._connect_locked():
                self._enqueue_locked(record)
                return False

            try:
                self._flush_pending_locked()
                assert self._collection is not None
                self._collection.insert_one(dict(record))
                return True
            except PyMongoError as error:
                logger.warning("Mất kết nối MongoDB Cloud: %s", error)
                self._enqueue_locked(record)
                self._reset_connection_locked()
                return False

    def close(self) -> None:
        with self._lock:
            self._reset_connection_locked()

    def _connect_locked(self) -> bool:
        if self._connected and self._collection is not None:
            return True

        if not settings.mongodb_uri:
            if not self._missing_config_logged:
                logger.error(
                    "Thiếu MONGODB_URI trong .env; telemetry Cloud sẽ chờ đồng bộ."
                )
                self._missing_config_logged = True
            return False

        now = time.monotonic()
        if now - self._last_connect_attempt < self._retry_interval_seconds:
            return False

        self._last_connect_attempt = now

        try:
            client = MongoClient(
                settings.mongodb_uri,
                serverSelectionTimeoutMS=3000,
                connectTimeoutMS=3000,
                socketTimeoutMS=5000,
                retryWrites=True,
            )
            client.admin.command("ping")
            database = client[settings.mongodb_database]

            self._client = client
            self._collection = database[settings.mongodb_collection]
            self._connected = True
            logger.info("Đã kết nối MongoDB Cloud.")
            return True
        except PyMongoError as error:
            logger.warning("MongoDB Cloud chưa sẵn sàng: %s", error)
            self._reset_connection_locked()
            return False

    def _flush_pending_locked(self) -> None:
        assert self._collection is not None
        pending_before_flush = len(self._pending)

        while self._pending:
            self._collection.insert_one(self._pending[0])
            self._pending.popleft()

        if pending_before_flush:
            logger.info(
                "Đã đồng bộ %s telemetry đang chờ lên MongoDB Cloud.",
                pending_before_flush,
            )

    def _enqueue_locked(self, record: dict[str, Any]) -> None:
        queue_was_full = len(self._pending) == self._pending.maxlen
        self._pending.append(dict(record))

        if queue_was_full:
            logger.error("Hàng đợi MongoDB đầy; bản ghi cũ nhất đã bị loại bỏ.")

        if len(self._pending) == 1 or len(self._pending) % 10 == 0:
            logger.warning(
                "Đã xếp telemetry vào hàng đợi MongoDB (%s bản ghi).",
                len(self._pending),
            )

    def _reset_connection_locked(self) -> None:
        if self._client is not None:
            self._client.close()

        self._client = None
        self._collection = None
        self._connected = False


mongo_store = MongoTelemetryStore()
