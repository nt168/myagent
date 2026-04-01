import asyncio
from typing import Any, Awaitable, Callable, Dict, Optional

from ..config import get_config
from .llm_process_manager import llm_process_manager


class LLMService:
    def __init__(self):
        self.config = get_config()

    async def query_text(
        self,
        prompt: str,
        on_chunk: Optional[Callable[[str], Awaitable[None]]] = None,
    ) -> str:
        host = self.config.llm.host
        port = self.config.llm.port

        if not self.config.llm.enabled:
            raise RuntimeError("llm_serv 功能未启用")

        if not prompt:
            raise ValueError("LLM 查询文本不能为空")

        await llm_process_manager.ensure_running()

        try:
            reader, writer = await asyncio.open_connection(host, port)
        except Exception as e:
            raise RuntimeError(f"无法连接 llm_serv: {e}") from e

        try:
            text = prompt.strip()
            if not text.endswith("\n"):
                text += "\n"
            writer.write(text.encode("utf-8"))
            await writer.drain()

            got_data = False
            last_data_time = asyncio.get_event_loop().time()
            buffer = ""

            while True:
                try:
                    chunk = await asyncio.wait_for(reader.read(1024), timeout=0.8)
                except asyncio.TimeoutError:
                    if got_data and asyncio.get_event_loop().time() - last_data_time > 0.8:
                        break
                    continue

                if not chunk:
                    break

                got_data = True
                last_data_time = asyncio.get_event_loop().time()
                text_chunk = chunk.decode("utf-8", errors="ignore")
                buffer += text_chunk
                if on_chunk is not None:
                    await on_chunk(text_chunk)

            if not got_data:
                raise RuntimeError("llm_serv 未返回数据")

            return buffer

        finally:
            try:
                writer.close()
                await writer.wait_closed()
            except Exception:
                pass

    async def query_llm(
        self,
        prompt: str,
        send_event: Callable[[str, Dict[str, Any]], Awaitable[None]],
    ) -> None:
        try:
            await send_event("llm_stream", {"content": "正在发送请求至 llm_serv...\n"})
            await self.query_text(
                prompt,
                lambda chunk: send_event("llm_stream", {"content": chunk}),
            )
            await send_event("llm_done", {"summary": "LLM 推理完成"})
        except Exception as e:
            await send_event("error", {"message": f"llm_serv 处理失败: {e}"})
