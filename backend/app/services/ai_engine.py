import json
import base64
import asyncio
import subprocess
import os
import tempfile
from typing import Dict, Any, List, Optional, Callable
from openai import OpenAI
from ..config import get_config
from .tools import get_tool_definitions, tool_system_prompt, execute_tool


class MCPClient:
    def __init__(self, server_name: str, config: Dict[str, Any]):
        self.server_name = server_name
        self.command = config.get("command", "")
        self.args = config.get("args", [])
        self.env = config.get("env", {})
        self.process: Optional[subprocess.Popen] = None
        self._connected = False

    async def start(self):
        if self.process:
            return
        try:
            env = {**self.env, **os.environ}
            self.process = subprocess.Popen(
                [self.command] + self.args,
                env=env,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            await asyncio.sleep(1)
            self._connected = True
            print(f"[MCP] Server {self.server_name} started")
        except Exception as e:
            print(f"[MCP] Failed to start {self.server_name}: {e}")
            self._connected = False

    async def stop(self):
        if self.process:
            try:
                self.process.stdin.close()
                self.process.terminate()
                self.process.wait(timeout=5)
            except Exception:
                self.process.kill()
                self.process.wait()
            self.process = None
            self._connected = False
            print(f"[MCP] Server {self.server_name} stopped")

    async def analyze_image(
        self, screenshot_b64: str, prompt: str = ""
    ) -> Dict[str, Any]:
        if not self._connected or not self.process:
            return {"success": False, "error": "MCP server not connected"}

        try:
            with tempfile.NamedTemporaryFile(suffix=".jpg", delete=False) as f:
                f.write(base64.b64decode(screenshot_b64))
                temp_path = f.name

            message = {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "tools/call",
                "params": {
                    "name": "ui_to_artifact",
                    "arguments": {
                        "image_source": temp_path,
                        "output_type": "description",
                        "prompt": prompt
                        or "分析当前屏幕：1. 描述界面状态 2. 列出所有可见图标和按钮的精确中心坐标（X,Y），坐标以屏幕左上角为原点(0,0)，右下角为(1920,1080) 3. 如果需要点击某个元素，给出其精确坐标",
                    },
                },
            }

            self.process.stdin.write(json.dumps(message) + "\n")
            self.process.stdin.flush()

            response_line = self.process.stdout.readline()
            response = json.loads(response_line)

            os.unlink(temp_path)

            if "result" in response:
                content = response["result"].get("content", "")
                if isinstance(content, list):
                    content = "".join(
                        item.get("text", "")
                        for item in content
                        if isinstance(item, dict)
                    )
                return {"success": True, "analysis": content}
            else:
                error = response.get("error", {})
                return {
                    "success": False,
                    "error": error.get("message", "Unknown error"),
                }

        except Exception as e:
            return {"success": False, "error": str(e)}

    @property
    def connected(self):
        return self._connected


class AIEngine:
    def __init__(self):
        config = get_config()
        self.chat_client = OpenAI(
            base_url=config.chat.api_base, api_key=config.chat.api_key
        )
        self.vision_client = OpenAI(
            base_url=config.vision.api_base, api_key=config.vision.api_key
        )
        self.chat_model = config.chat.model
        self.vision_model = config.vision.model
        self.chat_temperature = config.chat.temperature
        self.use_mcp = config.vision.use_mcp
        self.permission_callback: Optional[Callable] = None

        self.mcp_client: Optional[MCPClient] = None
        if config.mcp.enabled and config.mcp.servers:
            server_name = list(config.mcp.servers.keys())[0]
            server_config = config.mcp.servers[server_name].model_dump()
            self.mcp_client = MCPClient(server_name, server_config)

    def set_permission_callback(self, callback: Callable):
        self.permission_callback = callback

    async def start_mcp(self):
        if self.mcp_client:
            await self.mcp_client.start()

    async def stop_mcp(self):
        if self.mcp_client:
            await self.mcp_client.stop()

    async def analyze_screenshot(
        self,
        screenshot_b64: str,
        user_message: str = "",
        screen_width: int = 1920,
        screen_height: int = 1080,
    ) -> Dict[str, Any]:
        if self.use_mcp and self.mcp_client and self.mcp_client.connected:
            return await self.mcp_client.analyze_image(screenshot_b64, user_message)

        try:
            vision_prompt = """你是一个精确的坐标识别助手。

【任务】分析这张屏幕截图，找出所有可点击元素的中心点坐标。

【输出格式】对于每个元素，给出: (X, Y) - 元素描述

【重要】只输出你能在图像中明确看到的元素，根据图像内容直接估计位置。"""

            response = self.vision_client.chat.completions.create(
                model=self.vision_model,
                messages=[
                    {"role": "system", "content": vision_prompt},
                    {
                        "role": "user",
                        "content": [
                            {"type": "text", "text": user_message or "分析当前屏幕"},
                            {
                                "type": "image_url",
                                "image_url": {
                                    "url": f"data:image/jpeg;base64,{screenshot_b64}"
                                },
                            },
                        ],
                    },
                ],
                temperature=0.1,
            )

            analysis = response.choices[0].message.content or ""
            # 过滤 GLM 模型返回的特殊 token
            analysis = (
                analysis.replace("<|begin_of_box|>", "")
                .replace("<|end_of_box|>", "")
                .strip()
            )

            return {"success": True, "analysis": analysis}
        except Exception as e:
            return {"success": False, "error": str(e)}

    async def chat(
        self, messages: List[Dict[str, str]], allow_execution: bool = True
    ) -> Dict[str, Any]:
        tools = get_tool_definitions()

        full_messages = [{"role": "system", "content": tool_system_prompt}] + messages

        print(f"\n[AI Chat] Calling model with {len(full_messages)} messages")
        print(f"[AI Chat] Messages:")
        for i, m in enumerate(full_messages):
            role = m.get("role", "unknown")
            content = m.get("content", "") or ""
            has_reasoning = "reasoning_content" in m
            has_tool_calls = "tool_calls" in m
            print(
                f"  {i + 1}. [{role}] reasoning={has_reasoning} tool_calls={has_tool_calls}: {content[:80]}..."
            )

        try:
            print(
                f"[AI Chat] Request: model={self.chat_model}, tools={[t['function']['name'] for t in tools]}"
            )
            response = self.chat_client.chat.completions.create(
                model=self.chat_model,
                messages=full_messages,
                tools=tools,
                tool_choice="auto",
                temperature=self.chat_temperature,
            )

            response_message = response.choices[0].message
            tool_calls = response_message.tool_calls
            content = response_message.content or ""
            reasoning_content = (
                getattr(response_message, "reasoning_content", None) or ""
            )

            print(f"\n=== AI Response ===")
            print(f"Content: {content[:200] if content else '(empty)'}")
            print(
                f"Reasoning content: {len(reasoning_content)} chars"
                if reasoning_content
                else "Reasoning content: (empty)"
            )
            print(f"Tool calls: {len(tool_calls) if tool_calls else 0}")
            if tool_calls:
                for tc in tool_calls:
                    print(f"  -> {tc.function.name}: {tc.function.arguments}")

            if tool_calls:
                return {
                    "success": True,
                    "role": "assistant",
                    "content": content,
                    "reasoning_content": reasoning_content,
                    "tool_calls": [
                        {
                            "id": tc.id,
                            "type": tc.type,
                            "function": {
                                "name": tc.function.name,
                                "arguments": tc.function.arguments,
                            },
                        }
                        for tc in tool_calls
                    ],
                }
            else:
                return {
                    "success": True,
                    "role": "assistant",
                    "content": content,
                    "reasoning_content": reasoning_content,
                    "tool_calls": None,
                }

        except Exception as e:
            print(f"[AI Chat] Error: {e}")
            return {
                "success": False,
                "error": str(e),
                "role": "assistant",
                "content": "",
                "reasoning_content": "",
                "tool_calls": None,
            }

    async def execute_tool_call(
        self, tool_call_id: str, tool_name: str, arguments: Dict[str, Any]
    ) -> Dict[str, Any]:
        config = get_config()
        import datetime

        now = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        print(f"[{now}] [AI Engine] execute_tool_call start: {tool_name}")

        result = await execute_tool(
            tool_name,
            arguments,
            config.tools.screenshot.quality,
        )

        now = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        print(f"[{now}] [AI Engine] execute_tool finished")

        # take_screenshot 的分析已经在 execute_tool 中完成，直接使用结果
        if result["success"] and tool_name == "take_screenshot":
            if result.get("data", {}).get("analysis"):
                result["analysis"] = result["data"]["analysis"]

        now = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        print(f"[{now}] [AI Engine] execute_tool_call returning")

        return {
            "tool_call_id": tool_call_id,
            "tool_name": tool_name,
            "arguments": arguments,
            "result": result,
        }

    async def continue_chat(
        self,
        messages: List[Dict[str, str]],
        tool_results: List[Dict[str, Any]],
        allow_execution: bool = True,
    ) -> Dict[str, Any]:
        tool_messages = []
        for tr in tool_results:
            tool_messages.append(
                {
                    "role": "tool",
                    "tool_call_id": tr["tool_call_id"],
                    "content": json.dumps(tr["result"], ensure_ascii=False),
                }
            )

        full_messages = (
            [{"role": "system", "content": tool_system_prompt}]
            + messages
            + tool_messages
        )

        print(f"\n[AI Continue] Sending {len(full_messages)} messages to model")
        for i, m in enumerate(full_messages):
            role = m.get("role", "unknown")
            content = m.get("content", "") or ""
            has_reasoning = "reasoning_content" in m
            has_tool_calls = "tool_calls" in m
            tc_id = (
                f" tc_id={m.get('tool_call_id', '')}" if m.get("tool_call_id") else ""
            )
            print(
                f"  {i + 1}. [{role}]{tc_id} reasoning={has_reasoning} tool_calls={has_tool_calls}: {content[:60]}..."
            )

        try:
            response = self.chat_client.chat.completions.create(
                model=self.chat_model,
                messages=full_messages,
                tools=get_tool_definitions(),
                tool_choice="auto",
                temperature=self.chat_temperature,
            )

            response_message = response.choices[0].message
            tool_calls = response_message.tool_calls
            content = response_message.content or ""
            reasoning_content = (
                getattr(response_message, "reasoning_content", None) or ""
            )

            print(f"\n=== AI Continue Response ===")
            print(f"Content: {content[:200] if content else '(empty)'}")
            print(
                f"Reasoning content: {len(reasoning_content)} chars"
                if reasoning_content
                else "Reasoning content: (empty)"
            )
            print(f"Tool calls: {len(tool_calls) if tool_calls else 0}")

            if tool_calls:
                for tc in tool_calls:
                    print(f"  -> {tc.function.name}: {tc.function.arguments}")

                return {
                    "success": True,
                    "role": "assistant",
                    "content": content,
                    "reasoning_content": reasoning_content,
                    "tool_calls": [
                        {
                            "id": tc.id,
                            "type": tc.type,
                            "function": {
                                "name": tc.function.name,
                                "arguments": tc.function.arguments,
                            },
                        }
                        for tc in tool_calls
                    ],
                }
            else:
                return {
                    "success": True,
                    "role": "assistant",
                    "content": content,
                    "reasoning_content": reasoning_content,
                    "tool_calls": None,
                }

        except Exception as e:
            print(f"[AI Continue] Error: {e}")
            return {
                "success": False,
                "error": str(e),
                "role": "assistant",
                "content": "",
                "reasoning_content": "",
                "tool_calls": None,
            }


ai_engine = AIEngine()
