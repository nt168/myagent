import json
import asyncio
from typing import Any, Dict, List
from fastapi import APIRouter, WebSocket, WebSocketDisconnect
from starlette.websockets import WebSocketState

from ..config import get_config
from ..database import add_message, get_messages
from ..services.ai_engine import ai_engine
from ..services.llm_service import LLMService
from ..services.task_agent import task_agent_service
from ..api.ai import build_message_list

router = APIRouter(tags=["AI-WebSocket"])

llm_service = LLMService()


class AIChatSession:
    def __init__(self, websocket: WebSocket, session_id: int):
        self.websocket = websocket
        self.session_id = session_id

    async def send_event(self, event_type: str, data: Dict[str, Any]) -> None:
        if self.websocket.client_state != WebSocketState.CONNECTED:
            return
        payload = json.dumps({"type": event_type, "data": data}, ensure_ascii=False)
        await self.websocket.send_text(payload)

    async def run_chat_loop(self, user_message: str) -> None:
        config = get_config()

        await add_message(self.session_id, "user", user_message)

        messages = await get_messages(self.session_id)
        message_list = build_message_list(messages)

        await self.send_event("thinking", {"message": user_message})

        chat_result = await ai_engine.chat(message_list, not config.permission_mode)

        if not chat_result["success"]:
            await self.send_event(
                "error", {"message": chat_result.get("error", "未知错误")}
            )
            return

        reasoning = chat_result.get("reasoning_content") or ""
        content = chat_result.get("content") or ""

        if reasoning:
            await self.send_event("reasoning", {"content": reasoning})
        if content:
            await self.send_event("response", {"content": content})

        tool_calls_json = (
            json.dumps(chat_result["tool_calls"]) if chat_result["tool_calls"] else None
        )
        msg_id = await add_message(
            self.session_id,
            chat_result["role"],
            content,
            tool_calls=tool_calls_json,
            reasoning_content=reasoning,
        )

        if chat_result["tool_calls"]:
            await self._execute_tool_chain(
                chat_result["tool_calls"], message_list, msg_id
            )

    async def run_llm_loop(self, user_message: str) -> None:
        if self.session_id:
            await self._add_user_message(user_message)
        await self.send_event("thinking", {"message": user_message})

        await llm_service.query_llm(user_message, self.send_event)

    async def run_agent_task(self, user_message: str, machine_id: int) -> None:
        await task_agent_service.run_task(
            self.session_id,
            machine_id,
            user_message,
            self.send_event,
        )

    async def _add_user_message(self, content: str) -> None:
        await add_message(self.session_id, "user", content)

    async def _execute_tool_chain(
        self,
        tool_calls: List[Dict[str, Any]],
        message_list: List[Dict[str, Any]],
        msg_id: int,
    ) -> None:
        config = get_config()

        for tc in tool_calls:
            tc_id = tc["id"]
            func_name = tc["function"]["name"]
            func_args_str = tc["function"].get("arguments", "{}")
            if isinstance(func_args_str, dict):
                func_args_str = json.dumps(func_args_str)
            try:
                args = json.loads(func_args_str) if func_args_str else {}
            except json.JSONDecodeError:
                args = {}

            await self.send_event(
                "tool_start",
                {"tool_name": func_name, "arguments": args},
            )

            tool_result = await ai_engine.execute_tool_call(tc_id, func_name, args)

            result_data = tool_result.get("result") or {}
            success = result_data.get("success", False)
            message = result_data.get("message", "")
            data = result_data.get("data")

            await self.send_event(
                "tool_result",
                {
                    "tool_name": func_name,
                    "success": success,
                    "message": message,
                },
            )

            if data and data.get("screenshot"):
                analysis = data.get("analysis", "")
                await self.send_event(
                    "screenshot",
                    {"screenshot": data["screenshot"], "analysis": analysis},
                )

            if data and data.get("finished"):
                await self.send_event(
                    "finished", {"summary": data.get("summary", "任务完成")}
                )
                return

            tool_result_content = {
                "success": success,
                "message": message,
                "analysis": (data.get("analysis", "") if data else "")[:500],
            }

            db_data = {
                "success": success,
                "message": message,
                "analysis": (data.get("analysis", "") if data else "")[:500],
            }

            await add_message(
                self.session_id,
                "tool",
                json.dumps(db_data, ensure_ascii=False),
                tool_call_id=tc_id,
            )

            messages = await get_messages(self.session_id)
            message_list = build_message_list(messages)

            continue_result = await ai_engine.continue_chat(
                message_list,
                [{"tool_call_id": tc_id, "result": tool_result_content}],
                not config.permission_mode,
            )

            if not continue_result["success"]:
                await self.send_event(
                    "error", {"message": continue_result.get("error", "继续对话失败")}
                )
                return

            cont_reasoning = continue_result.get("reasoning_content") or ""
            cont_content = continue_result.get("content") or ""

            if cont_reasoning:
                await self.send_event("reasoning", {"content": cont_reasoning})
            if cont_content:
                await self.send_event("response", {"content": cont_content})

            cont_tc_json = (
                json.dumps(continue_result["tool_calls"])
                if continue_result["tool_calls"]
                else None
            )
            await add_message(
                self.session_id,
                "assistant",
                cont_content,
                tool_calls=cont_tc_json,
                reasoning_content=cont_reasoning,
            )

            if continue_result["tool_calls"]:
                messages = await get_messages(self.session_id)
                message_list = build_message_list(messages)
                await self._execute_tool_chain(
                    continue_result["tool_calls"], message_list, msg_id
                )
                return

        await self.send_event("done", {})


@router.websocket("/api/ai/ws")
async def ai_websocket(websocket: WebSocket):
    await websocket.accept()
    session_id: int = 0
    chat_session: AIChatSession = None  # type: ignore

    try:
        while True:
            raw = await websocket.receive_text()
            try:
                msg = json.loads(raw)
            except json.JSONDecodeError:
                continue

            msg_type = msg.get("type")

            if msg_type == "init":
                session_id = int(msg.get("session_id") or 0)
                chat_session = AIChatSession(websocket, session_id)
                await chat_session.send_event("connected", {"session_id": session_id})

            elif msg_type == "chat":
                if chat_session is None:
                    session_id = int(msg.get("session_id") or 0)
                    chat_session = AIChatSession(websocket, session_id)
                user_msg = msg.get("message", "")
                if user_msg:
                    asyncio.create_task(chat_session.run_chat_loop(user_msg))

            elif msg_type == "llm_query":
                if chat_session is None:
                    session_id = int(msg.get("session_id") or 0)
                    chat_session = AIChatSession(websocket, session_id)
                user_msg = msg.get("message", "")
                if user_msg:
                    asyncio.create_task(chat_session.run_llm_loop(user_msg))

            elif msg_type == "agent_task":
                if chat_session is None:
                    session_id = int(msg.get("session_id") or 0)
                    chat_session = AIChatSession(websocket, session_id)
                user_msg = msg.get("message", "")
                machine_id = int(msg.get("machine_id") or 0)
                if user_msg and machine_id:
                    asyncio.create_task(chat_session.run_agent_task(user_msg, machine_id))

            elif msg_type == "stop":
                pass

    except WebSocketDisconnect:
        pass
    except Exception:
        pass
