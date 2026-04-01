from .vnc import router as vnc_router
from .ai import router as ai_router
from .chat import router as chat_router
from .terminal import router as terminal_router
from .vnc_websocket import router as vnc_websocket_router
from .ai_ws import router as ai_ws_router
from .llm import router as llm_router

__all__ = [
    "vnc_router",
    "ai_router",
    "chat_router",
    "terminal_router",
    "vnc_websocket_router",
    "ai_ws_router",
    "llm_router",
]
