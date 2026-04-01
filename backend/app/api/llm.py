from fastapi import APIRouter, HTTPException

from ..services.llm_process_manager import llm_process_manager

router = APIRouter(prefix="/api/llm", tags=["LLM"])


@router.get("/status")
async def llm_status():
    return await llm_process_manager.status()


@router.post("/start")
async def llm_start():
    try:
        return await llm_process_manager.start()
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"llm_serv 启动失败: {e}") from e
