from fastapi import FastAPI

app = FastAPI(title="Disaster Warning Station API")


@app.get("/health")
def health_check() -> dict[str, str]:
    return {"status": "ok"}
