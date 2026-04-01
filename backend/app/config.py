from pathlib import Path
import yaml
from typing import Dict, List
from pydantic import BaseModel


class VNCConfig(BaseModel):
    host: str = "localhost"
    port: int = 5900
    password: str = ""


class TerminalConfig(BaseModel):
    enabled: bool = True
    protocol: str = "http"
    host: str = ""
    port: int = 7681
    path: str = "/"
    auto_start: bool = True
    auto_build: bool = True
    ttyd_binary: str = "webtty/build/ttyd"
    ttyd_interface: str = "0.0.0.0"
    browser_host: str = ""
    ssh_host: str = "localhost"
    ssh_user: str = "root"
    ssh_port: int = 22
    startup_timeout_sec: int = 8
    command: str = ""


class ChatConfig(BaseModel):
    api_base: str = "http://localhost:4000/v1"
    api_key: str = ""
    model: str = "gpt-4o"
    temperature: float = 0.1


class VisionConfig(BaseModel):
    use_mcp: bool = False
    api_base: str = "http://localhost:4000/v1"
    api_key: str = ""
    model: str = "gpt-4o"


class MCPServerConfig(BaseModel):
    type: str = "stdio"
    command: str = ""
    args: List[str] = []
    env: Dict[str, str] = {}


class MCPConfig(BaseModel):
    enabled: bool = False
    servers: Dict[str, MCPServerConfig] = {}


class ScreenshotConfig(BaseModel):
    enabled: bool = True
    quality: int = 85


class ToolsConfig(BaseModel):
    screenshot: ScreenshotConfig = ScreenshotConfig()


class ServerConfig(BaseModel):
    host: str = "0.0.0.0"
    port: int = 8000
    debug: bool = False


class LLMConfig(BaseModel):
    enabled: bool = True
    host: str = "127.0.0.1"
    listen_host: str = "0.0.0.0"
    port: int = 18001
    auto_start: bool = True
    startup_timeout_sec: int = 20
    connect_timeout_sec: float = 2.0
    binary: str = "llm/serv/llm_serv"
    engine_binary: str = "llm/engine/mnn/llm_demo"
    model_config_path: str = "llm/models/Qwen35-08b_mnn/config.json"
    workdir: str = "llm"
    verbose: bool = False


class Config(BaseModel):
    vnc: VNCConfig = VNCConfig()
    terminal: TerminalConfig = TerminalConfig()
    chat: ChatConfig = ChatConfig()
    vision: VisionConfig = VisionConfig()
    llm: LLMConfig = LLMConfig()
    mcp: MCPConfig = MCPConfig()
    tools: ToolsConfig = ToolsConfig()
    server: ServerConfig = ServerConfig()
    permission_mode: bool = False


def load_config(config_path: str | None = None) -> Config:
    if config_path is None:
        config_path = str(Path(__file__).parent.parent.parent / "config.yaml")

    config_data = {}
    if Path(config_path).exists():
        with open(config_path, "r", encoding="utf-8") as f:
            config_data = yaml.safe_load(f) or {}

    return Config(**config_data)


_config: Config | None = None


def get_config() -> Config:
    global _config
    if _config is None:
        _config = load_config()
    return _config


def reload_config(config_path: str | None = None) -> Config:
    global _config
    _config = load_config(config_path)
    return _config
