import uvicorn

from app.config import settings


if __name__ == "__main__":
    uvicorn.run(
        "app.application:app",
        host=settings.fastapi_host,
        port=settings.fastapi_port,
        reload=settings.app_env == "development",
    )
