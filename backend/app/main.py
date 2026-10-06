from fastapi import FastAPI

app = FastAPI(title="TrueSense Backend")

@app.get("/health")
def health_check():
    return {"status": "healthy"}
