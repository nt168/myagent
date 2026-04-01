import asyncio
from pathlib import Path
from typing import Dict, Optional

from ..config import get_config


class LLMProcessManager:
    def __init__(self) -> None:
        self.process: Optional[asyncio.subprocess.Process] = None

    async def is_port_open(self) -> bool:
        config = get_config().llm
        try:
            reader, writer = await asyncio.wait_for(
                asyncio.open_connection(config.host, config.port),
                timeout=config.connect_timeout_sec,
            )
            writer.close()
            await writer.wait_closed()
            return True
        except Exception:
            return False

    def _build_command(self) -> list[str]:
        config = get_config().llm
        command = [
            str(Path(config.binary).resolve()),
            "-h",
            config.listen_host,
            "-p",
            str(config.port),
            "-e",
            str(Path(config.engine_binary).resolve()),
            "-c",
            str(Path(config.model_config_path).resolve()),
        ]
        if config.workdir.strip():
            command.extend(["-w", str(Path(config.workdir).resolve())])
        if config.verbose:
            command.append("-v")
        return command

    async def ensure_running(self) -> None:
        config = get_config().llm
        if await self.is_port_open():
            return

        if not config.auto_start:
            raise RuntimeError("llm_serv 未启动，请先手动启动 llm/serv/llm_serv")

        await self.start()

    async def start(self) -> Dict[str, str]:
        config = get_config().llm

        if await self.is_port_open():
            return {"success": "true", "message": "llm_serv 已在运行"}

        binary = Path(config.binary).resolve()
        engine_binary = Path(config.engine_binary).resolve()
        model_config = Path(config.model_config_path).resolve()
        workdir = Path(config.workdir).resolve() if config.workdir.strip() else binary.parent

        if not binary.exists():
            raise RuntimeError(f"未找到 llm_serv 可执行文件: {binary}")
        if not engine_binary.exists():
            raise RuntimeError(f"未找到 llm_demo 可执行文件: {engine_binary}")
        if not model_config.exists():
            raise RuntimeError(f"未找到模型配置文件: {model_config}")

        command = self._build_command()
        self.process = await asyncio.create_subprocess_exec(
            *command,
            cwd=str(workdir),
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT,
        )

        deadline = asyncio.get_event_loop().time() + max(config.startup_timeout_sec, 1)
        output = ""
        while asyncio.get_event_loop().time() < deadline:
            if await self.is_port_open():
                return {"success": "true", "message": "llm_serv 启动成功"}

            if self.process and self.process.returncode is not None:
                if self.process.stdout:
                    data = await self.process.stdout.read()
                    output = (data or b"").decode("utf-8", errors="ignore")
                raise RuntimeError(f"llm_serv 启动失败: {output}")

            await asyncio.sleep(0.2)

        await self.stop()
        raise RuntimeError("llm_serv 启动超时")

    async def stop(self) -> None:
        if self.process and self.process.returncode is None:
            self.process.terminate()
            try:
                await asyncio.wait_for(self.process.wait(), timeout=3)
            except asyncio.TimeoutError:
                self.process.kill()
                await self.process.wait()
        self.process = None

    async def status(self) -> Dict[str, object]:
        config = get_config().llm
        running = await self.is_port_open()
        return {
            "enabled": config.enabled,
            "auto_start": config.auto_start,
            "running": running,
            "host": config.host,
            "port": config.port,
            "binary": config.binary,
            "model_config_path": config.model_config_path,
        }


llm_process_manager = LLMProcessManager()
