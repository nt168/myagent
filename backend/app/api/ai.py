import json
from fastapi import APIRouter, HTTPException
from typing import Dict, Any, List, Optional
from ..models.schemas import (
    ChatRequest,
    ChatResponse,
    ToolRequest,
    ToolResponse,
    PermissionRequest,
    ContinueRequest,
)
from ..services.ai_engine import ai_engine
from ..database import add_message, add_tool_execution, get_messages
from ..config import get_config

router = APIRouter(prefix="/api/ai", tags=["AI"])

pending_permissions: Dict[str, Dict] = {}


def build_message_list(messages: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
    result = []
    for m in messages:
        msg: Dict[str, Any] = {"role": m["role"], "content": m.get("content", "") or ""}
        if m.get("reasoning_content"):
            msg["reasoning_content"] = m["reasoning_content"]
        if m.get("tool_calls"):
            msg["tool_calls"] = m["tool_calls"]
        if m.get("tool_call_id"):
            msg["tool_call_id"] = m["tool_call_id"]
        result.append(msg)
    return result


@router.post("/chat", response_model=ChatResponse)
async def chat(request: ChatRequest):
    config = get_config()

    print(f"\n========== NEW CHAT REQUEST ==========")
    print(f"User message: {request.message}")
    print(f"Session: {request.session_id}")

    await add_message(request.session_id, "user", request.message)

    messages = await get_messages(request.session_id)
    message_list = build_message_list(messages)

    print(f"Total messages in history: {len(message_list)}")
    for i, m in enumerate(message_list):
        has_reasoning = "reasoning_content" in m
        has_tool_calls = "tool_calls" in m
        print(
            f"  {i + 1}. [{m['role']}] reasoning={has_reasoning} tool_calls={has_tool_calls}"
        )

    result = await ai_engine.chat(message_list, not config.permission_mode)

    if not result["success"]:
        raise HTTPException(status_code=500, detail=result["error"])

    tool_calls_json = json.dumps(result["tool_calls"]) if result["tool_calls"] else None
    reasoning_content = result.get("reasoning_content")
    msg_id = await add_message(
        request.session_id,
        result["role"],
        result["content"],
        tool_calls=tool_calls_json,
        reasoning_content=reasoning_content,
    )

    if result["tool_calls"] and config.permission_mode:
        for tc in result["tool_calls"]:
            pending_permissions[tc["id"]] = {
                "session_id": request.session_id,
                "tool_name": tc["function"]["name"],
                "arguments": json.loads(tc["function"]["arguments"]),
                "message_id": msg_id,
            }

    parsed_tool_calls = None
    if result["tool_calls"]:
        parsed_tool_calls = []
        for tc in result["tool_calls"]:
            func_args = tc["function"].get("arguments", "{}")
            if isinstance(func_args, dict):
                func_args = json.dumps(func_args)
            parsed_tool_calls.append(
                {
                    "id": tc["id"],
                    "type": tc["type"],
                    "function": {
                        "name": tc["function"]["name"],
                        "arguments": func_args,
                    },
                    "arguments": json.loads(func_args) if func_args else {},
                }
            )

    return ChatResponse(
        session_id=request.session_id,
        message_id=msg_id,
        response=result["content"],
        tool_calls=parsed_tool_calls,
    )


@router.post("/execute-tool")
async def execute_tool(request: ToolRequest):
    config = get_config()

    if config.permission_mode:
        return {"success": False, "result": "", "error": "工具执行需要用户许可"}

    result = await ai_engine.execute_tool_call(
        request.tool_call_id, request.tool_name, request.arguments
    )

    if not result.get("result"):
        return {
            "success": False,
            "result": "",
            "error": result.get("error", "Unknown error"),
        }

    tool_result = result["result"] or {}
    data = tool_result.get("data")
    screenshot_len = (
        len(data.get("screenshot", "")) if data and data.get("screenshot") else 0
    )

    return {
        "success": tool_result.get("success", False),
        "result": tool_result.get("message", ""),
        "data": data,
    }


@router.post("/continue")
async def continue_after_tool(request: ContinueRequest):
    config = get_config()

    tool_name = request.tool_name
    tool_result = request.result
    tool_data = tool_result.get("data") or {}
    analysis = tool_data.get("analysis", "") if tool_data else ""

    print(f"\n=== Continue Step ===")
    print(f"Tool: {tool_name}")
    print(f"Success: {tool_result.get('success', False)}")
    if analysis:
        print(f"Analysis preview: {analysis[:200]}...")

    tool_result_content = {
        "success": tool_result.get("success", False),
        "message": tool_result.get("message", ""),
        "analysis": analysis[:500] if analysis else "",
    }

    # 存储到数据库的消息，不包含截图
    tool_data = tool_result.get("data") or {}
    db_data = {
        "success": tool_result.get("success", False),
        "message": tool_result.get("message", ""),
        "analysis": analysis[:500] if analysis else "",
    }

    await add_message(
        request.session_id,
        "tool",
        json.dumps(db_data, ensure_ascii=False),
        tool_call_id=request.tool_call_id,
    )

    messages = await get_messages(request.session_id)
    message_list = build_message_list(messages)

    print(f"\n=== Messages sent to continue_chat ({len(message_list)} total) ===")
    for i, m in enumerate(message_list):
        role = m["role"]
        content = (
            m.get("content", "")[:80] + "..."
            if len(m.get("content", "")) > 80
            else m.get("content", "")
        )
        has_reasoning = "reasoning_content" in m
        has_tool_calls = "tool_calls" in m
        print(
            f"  {i + 1}. [{role}] reasoning={has_reasoning} tool_calls={has_tool_calls}: {content}"
        )

    chat_result = await ai_engine.continue_chat(
        message_list,
        [{"tool_call_id": "latest", "result": tool_result_content}],
        not config.permission_mode,
    )

    if not chat_result["success"]:
        print(f"[Continue] AI error: {chat_result.get('error')}")
        raise HTTPException(status_code=500, detail=chat_result.get("error"))

    response_content = chat_result.get("content", "") or ""
    tool_calls = chat_result.get("tool_calls")
    reasoning_content = chat_result.get("reasoning_content")

    tool_calls_json = json.dumps(tool_calls) if tool_calls else None
    await add_message(
        request.session_id,
        "assistant",
        response_content,
        tool_calls=tool_calls_json,
        reasoning_content=reasoning_content,
    )

    print(f"\n=== AI Response ===")
    print(f"Content: {response_content[:200] if response_content else '(empty)'}")
    print(f"Tool calls: {len(tool_calls) if tool_calls else 0}")
    if tool_calls:
        for tc in tool_calls:
            print(f"  -> {tc['function']['name']}: {tc['function']['arguments']}")

    parsed_tool_calls = None
    if tool_calls:
        parsed_tool_calls = []
        for tc in tool_calls:
            func_args = tc["function"].get("arguments", "{}")
            if isinstance(func_args, dict):
                func_args = json.dumps(func_args)
            parsed_tool_calls.append(
                {
                    "id": tc["id"],
                    "type": tc["type"],
                    "function": {
                        "name": tc["function"]["name"],
                        "arguments": func_args,
                    },
                    "arguments": json.loads(func_args) if func_args else {},
                }
            )

    return {"response": response_content, "tool_calls": parsed_tool_calls}


@router.post("/permission")
async def request_permission(request: PermissionRequest):
    if request.tool_call_id in pending_permissions:
        pending = pending_permissions[request.tool_call_id]

        if request.approved:
            result = await ai_engine.execute_tool_call(
                request.tool_call_id, request.tool_name, request.arguments
            )

            await add_tool_execution(
                pending["message_id"],
                request.tool_name,
                json.dumps(request.arguments),
                json.dumps(result["result"]),
                "success" if result["result"]["success"] else "failed",
            )

            tool_result_content = {
                "success": result["result"].get("success", False),
                "message": result["result"].get("message", ""),
                "analysis": result["result"].get("data", {}).get("analysis", "")
                if result["result"].get("data")
                else "",
            }
            await add_message(
                pending["session_id"],
                "tool",
                json.dumps(tool_result_content, ensure_ascii=False),
                tool_call_id=request.tool_call_id,
            )

            messages = await get_messages(pending["session_id"])
            message_list = build_message_list(messages)

            tool_results = [
                {"tool_call_id": request.tool_call_id, "result": result["result"]}
            ]

            continuation = await ai_engine.continue_chat(message_list, tool_results)

            if continuation["success"]:
                cont_content = continuation.get("content", "") or ""
                cont_tool_calls = continuation.get("tool_calls")
                cont_reasoning = continuation.get("reasoning_content")
                cont_tc_json = json.dumps(cont_tool_calls) if cont_tool_calls else None
                await add_message(
                    pending["session_id"],
                    "assistant",
                    cont_content,
                    tool_calls=cont_tc_json,
                    reasoning_content=cont_reasoning,
                )

            if continuation["success"] and continuation["tool_calls"]:
                for tc in continuation["tool_calls"]:
                    pending_permissions[tc["id"]] = {
                        "session_id": pending["session_id"],
                        "tool_name": tc["function"]["name"],
                        "arguments": json.loads(tc["function"]["arguments"]),
                        "message_id": 0,
                    }

            return {
                "approved": True,
                "result": result["result"],
                "continuation": continuation,
            }
        else:
            await add_tool_execution(
                pending["message_id"],
                request.tool_name,
                json.dumps(request.arguments),
                "用户拒绝执行",
                "rejected",
            )
            return {"approved": False, "reason": "用户拒绝执行"}

    return {"approved": False, "reason": "未找到待执行的工具调用"}


@router.post("/permission-mode")
async def set_permission(enabled: bool):
    config = get_config()
    config.permission_mode = enabled
    return {"success": True, "permission_mode": enabled}
