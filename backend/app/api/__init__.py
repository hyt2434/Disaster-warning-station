from .devices import router as devices_router
from .health import router as health_router
from .readings import router as readings_router

__all__ = ["devices_router", "health_router", "readings_router"]
