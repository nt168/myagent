from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

from ..config import get_config
from ..database import create_machine, create_session, delete_machine, get_machine, list_machines
from ..services.terminal_manager import terminal_manager

router = APIRouter(prefix="/api/terminal", tags=["Terminal"])


class MachineCreateRequest(BaseModel):
    ip: str
    username: str
    password: str


class MachineSessionRequest(BaseModel):
    machine_id: int


@router.get("/target")
async def get_terminal_target():
    config = get_config().terminal
    browser_host = config.browser_host.strip() or config.host.strip()
    return {
        "enabled": config.enabled,
        "protocol": config.protocol,
        "host": browser_host,
        "port": config.port,
        "path": config.path,
    }


@router.post("/session")
async def create_terminal_session(payload: MachineSessionRequest):
    full_config = get_config()
    terminal_config = full_config.terminal

    if not terminal_config.enabled:
        raise HTTPException(status_code=400, detail="终端功能未启用")

    machine = await get_machine(payload.machine_id)
    if not machine:
        raise HTTPException(status_code=404, detail="机器不存在")

    if terminal_config.auto_start:
        try:
            await terminal_manager.start(machine=machine)
        except Exception as e:
            raise HTTPException(status_code=500, detail=f"终端服务启动失败: {e}") from e

    session_id = await create_session(machine["ip"], full_config.vnc.port)
    return {"session_id": session_id, "machine": {"id": machine["id"], "ip": machine["ip"], "username": machine["username"]}}


@router.post("/disconnect")
async def disconnect_terminal():
    await terminal_manager.stop()
    return {"success": True, "message": "终端服务已关闭"}


@router.get("/status")
async def terminal_status():
    running = await terminal_manager.is_running()
    return {"running": running}


@router.get("/machines")
async def get_machine_list():
    machines = await list_machines()
    return {
        "machines": [
            {"id": item["id"], "ip": item["ip"], "username": item["username"]}
            for item in machines
        ]
    }


@router.post("/machines")
async def add_machine(payload: MachineCreateRequest):
    ip = payload.ip.strip()
    username = payload.username.strip()
    password = payload.password

    if not ip:
        raise HTTPException(status_code=400, detail="IP 不能为空")
    if not username:
        raise HTTPException(status_code=400, detail="用户名不能为空")

    try:
        machine_id = await create_machine(ip, username, password)
    except Exception as e:
        raise HTTPException(status_code=400, detail=f"保存机器失败: {e}") from e

    return {"id": machine_id, "ip": ip, "username": username}


@router.delete("/machines/{machine_id}")
async def remove_machine(machine_id: int):
    removed = await delete_machine(machine_id)
    if not removed:
        raise HTTPException(status_code=404, detail="机器不存在")
    return {"success": True, "message": "机器已删除", "machine_id": machine_id}
