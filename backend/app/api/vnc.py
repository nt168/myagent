import asyncio
from fastapi import APIRouter, HTTPException
from ..models.schemas import ScreenshotResponse
from ..services.vnc_client import vnc_manager
from ..config import get_config
from ..database import create_session

router = APIRouter(prefix="/api/vnc", tags=["VNC"])


@router.get("/target")
async def get_vnc_target():
    config = get_config()
    return {
        "host": config.vnc.host,
        "port": config.vnc.port,
        "password": config.vnc.password,
    }


@router.post("/session")
async def create_vnc_session():
    config = get_config()
    session_id = await create_session(config.vnc.host, config.vnc.port)

    connected = await asyncio.to_thread(
        vnc_manager.connect, config.vnc.host, config.vnc.port, config.vnc.password
    )

    if not connected:
        raise HTTPException(
            status_code=502,
            detail=f"无法连接VNC服务器: {config.vnc.host}:{config.vnc.port}",
        )

    return {"session_id": session_id}


@router.post("/disconnect")
async def disconnect_vnc():
    await asyncio.to_thread(vnc_manager.disconnect)
    return {"success": True, "message": "VNC已断开"}


@router.get("/status")
async def get_vnc_status():
    return {"connected": vnc_manager.connected}


@router.get("/screenshot", response_model=ScreenshotResponse)
async def get_screenshot():
    config = get_config()
    screenshot = await asyncio.to_thread(
        vnc_manager.capture_screen,
        config.tools.screenshot.max_width,
        config.tools.screenshot.quality,
    )

    if screenshot:
        return {"success": True, "screenshot": screenshot}
    else:
        return {"success": False, "error": "无法获取截图"}


@router.post("/click")
async def click(x: int, y: int):
    success = await asyncio.to_thread(vnc_manager.click, x, y)
    if success:
        return {"success": True}
    raise HTTPException(status_code=400, detail="点击失败")


@router.post("/double-click")
async def double_click(x: int, y: int):
    success = await asyncio.to_thread(vnc_manager.double_click, x, y)
    if success:
        return {"success": True}
    raise HTTPException(status_code=400, detail="双击失败")


@router.post("/type")
async def type_text(text: str):
    success = await asyncio.to_thread(vnc_manager.type_text, text)
    if success:
        return {"success": True}
    raise HTTPException(status_code=400, detail="输入失败")
