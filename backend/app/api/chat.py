from fastapi import APIRouter
from typing import List
from ..database import get_messages, get_session

router = APIRouter(prefix="/api/chat", tags=["Chat"])


@router.get("/sessions/{session_id}/messages")
async def get_chat_history(session_id: int, limit: int = 50):
    messages = await get_messages(session_id)
    return {"messages": messages}


@router.get("/sessions/{session_id}")
async def get_session_info(session_id: int):
    session = await get_session(session_id)
    if session:
        return session
    return {"error": "会话不存在"}
