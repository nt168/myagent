from fastapi import FastAPI, Response
from fastapi.staticfiles import StaticFiles
from fastapi.middleware.cors import CORSMiddleware
from pathlib import Path
import uvicorn

from .config import get_config
from .database import init_db
from .api import vnc, ai, chat, vnc_websocket, terminal, llm
from .api.ai_ws import router as ai_ws_router
from .services.ai_engine import ai_engine
from .services.llm_process_manager import llm_process_manager
from .services.terminal_manager import terminal_manager

app = FastAPI(title="CheckPilot - VNC Remote Testing Platform")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

frontend_path = Path(__file__).parent.parent.parent / "frontend"
if frontend_path.exists():
    app.mount("/static", StaticFiles(directory=str(frontend_path)), name="static")

novnc_path = frontend_path / "noVNC"
if novnc_path.exists():
    app.mount("/noVNC", StaticFiles(directory=str(novnc_path)), name="novnc")

app.include_router(vnc.router)
app.include_router(terminal.router)
app.include_router(ai.router)
app.include_router(chat.router)
app.include_router(vnc_websocket.router)
app.include_router(ai_ws_router)
app.include_router(llm.router)


@app.on_event("startup")
async def startup():
    await init_db()
    config = get_config()
    if config.llm.enabled and config.llm.auto_start:
        print("[Startup] Starting llm_serv...")
        await llm_process_manager.start()
    if config.mcp.enabled:
        print("[Startup] Starting MCP server...")
        await ai_engine.start_mcp()


@app.on_event("shutdown")
async def shutdown():
    await terminal_manager.stop()
    await llm_process_manager.stop()
    config = get_config()
    if config.mcp.enabled:
        print("[Shutdown] Stopping MCP server...")
        await ai_engine.stop_mcp()


@app.get("/")
async def root():
    frontend_path = Path(__file__).parent.parent.parent / "frontend" / "index.html"
    if frontend_path.exists():
        with open(frontend_path, "r") as f:
            content = f.read()
        return Response(content=content, media_type="text/html")
    return {"message": "CheckPilot API", "docs": "/docs"}


@app.get("/api/health")
async def health():
    llm_status = await llm_process_manager.status()
    return {"status": "ok", "llm": llm_status}


if __name__ == "__main__":
    config = get_config()
    uvicorn.run(app, host=config.server.host, port=config.server.port)
