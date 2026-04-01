import logging
import asyncio
from fastapi import APIRouter, Query, WebSocket
from starlette.websockets import WebSocketDisconnect

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/vnc", tags=["VNC"])


@router.websocket("/ws")
async def vnc_websocket(
    websocket: WebSocket, host: str = Query(...), port: int = Query(5900)
):
    logger.info(f"[VNC WS] Connection: {host}:{port}")

    reader = None
    writer = None

    try:
        await websocket.accept()
        logger.info("[VNC WS] Accepted")

        reader, writer = await asyncio.wait_for(
            asyncio.open_connection(host, port), timeout=15.0
        )
        logger.info("[VNC WS] Connected to VNC server (plain)")

        async def browser_to_vnc():
            try:
                while True:
                    data = await websocket.receive_bytes()
                    logger.debug(f"[VNC WS] Browser -> VNC: {len(data)} bytes")
                    if writer:
                        writer.write(data)
                        await writer.drain()
            except WebSocketDisconnect:
                logger.info("[VNC WS] Browser disconnected")
            except Exception as e:
                logger.debug(f"[VNC WS] Browser -> VNC error: {e}")

        async def vnc_to_browser():
            try:
                while True:
                    data = await reader.read(8192)
                    if not data:
                        logger.info("[VNC WS] VNC server closed connection")
                        break
                    logger.debug(f"[VNC WS] VNC -> Browser: {len(data)} bytes")
                    await websocket.send_bytes(data)
            except Exception as e:
                logger.debug(f"[VNC WS] VNC -> Browser error: {e}")

        await asyncio.gather(browser_to_vnc(), vnc_to_browser())

    except asyncio.TimeoutError:
        logger.error(f"[VNC WS] Connection timeout to {host}:{port}")
    except Exception as e:
        logger.error(f"[VNC WS] Error: {e}")
    finally:
        if writer:
            try:
                writer.close()
                await writer.wait_closed()
            except Exception:
                pass
        logger.info("[VNC WS] Closed")
