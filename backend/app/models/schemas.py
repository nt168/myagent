from typing import Optional, List, Dict, Any
from pydantic import BaseModel


class VNCSettings(BaseModel):
    host: str
    port: int = 5900
    password: str = ""


class ChatSettings(BaseModel):
    api_base: str
    api_key: str
    model: str
    temperature: float = 0.1


class VisionSettings(BaseModel):
    api_base: str
    api_key: str
    model: str


class ServerSettings(BaseModel):
    host: str = "0.0.0.0"
    port: int = 8000
    debug: bool = False


class ToolCallFunction(BaseModel):
    name: str
    arguments: str  # JSON string


class ToolCall(BaseModel):
    id: str
    type: str
    function: ToolCallFunction
    arguments: Optional[Dict[str, Any]] = None  # Parsed from function.arguments


class Message(BaseModel):
    id: int
    role: str
    content: str
    tool_calls: Optional[List[ToolCall]] = None
    created_at: str


class ToolExecution(BaseModel):
    id: int
    tool_name: str
    arguments: Dict[str, Any]
    result: str
    status: str
    created_at: str


class Session(BaseModel):
    id: int
    vnc_host: str
    vnc_port: int
    status: str
    permission_mode: bool
    created_at: str


class ChatRequest(BaseModel):
    session_id: int
    message: str


class ChatResponse(BaseModel):
    session_id: int
    message_id: int
    response: str
    tool_calls: Optional[List[ToolCall]] = None


class ToolRequest(BaseModel):
    session_id: int
    tool_call_id: str
    tool_name: str
    arguments: Dict[str, Any]


class ToolResponse(BaseModel):
    success: bool
    result: str
    screenshot: Optional[str] = None


class ContinueRequest(BaseModel):
    session_id: int
    tool_name: str
    tool_call_id: str
    result: Dict[str, Any]


class PermissionRequest(BaseModel):
    session_id: int
    tool_call_id: str
    tool_name: str
    arguments: Dict[str, Any]
    description: str
    approved: bool = False


class PermissionResponse(BaseModel):
    approved: bool
    tool_call_id: str


class ScreenshotResponse(BaseModel):
    success: bool
    screenshot: Optional[str] = None
    error: Optional[str] = None
