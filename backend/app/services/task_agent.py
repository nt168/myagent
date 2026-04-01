import json
import re
from typing import Any, Awaitable, Callable, Dict, Optional

from ..database import add_message, get_machine
from .llm_service import LLMService
from .remote_executor import remote_executor
from .skills import get_skill_catalog, match_skill_by_keywords


class TaskAgentService:
    def __init__(self) -> None:
        self.llm_service = LLMService()

    def _is_safe_command(self, command: str) -> bool:
        lowered = command.lower()
        blocked_tokens = [
            " rm ",
            "reboot",
            "shutdown",
            "poweroff",
            "halt",
            "mkfs",
            " dd ",
            "kill ",
            "killall",
            "systemctl stop",
            "systemctl restart",
            "service stop",
            "service restart",
            "init 0",
            "init 6",
        ]
        padded = f" {lowered} "
        return not any(token in padded for token in blocked_tokens)

    def _extract_json(self, text: str) -> Optional[Dict[str, Any]]:
        fenced_match = re.search(r"```json\s*(\{.*?\})\s*```", text, re.DOTALL)
        if fenced_match:
            try:
                return json.loads(fenced_match.group(1))
            except json.JSONDecodeError:
                pass

        plain_match = re.search(r"(\{.*\})", text, re.DOTALL)
        if plain_match:
            try:
                return json.loads(plain_match.group(1))
            except json.JSONDecodeError:
                return None
        return None

    async def _select_skill(
        self,
        task_description: str,
        send_event: Callable[[str, Dict[str, Any]], Awaitable[None]],
    ) -> Dict[str, str]:
        keyword_skill = match_skill_by_keywords(task_description)
        if keyword_skill:
            await send_event(
                "agent_status",
                {
                    "message": f"已匹配技能: {keyword_skill.name}，准备生成远程执行方案",
                },
            )
            return {
                "skill_name": keyword_skill.name,
                "skill_description": keyword_skill.description,
                "command": keyword_skill.command_template,
                "source": "keyword",
            }

        planning_prompt = (
            "你是一个 Linux 运维任务规划助手。\n"
            "请根据用户任务，从给定 skills 中选择最合适的一项，并输出 JSON，不要输出多余解释。\n"
            "输出格式必须为："
            '{"skill_name":"...","skill_description":"...","command":"..."}\n'
            "要求：\n"
            "1. command 必须是单条可在 bash 中执行的只读命令。\n"
            "2. 禁止 rm、reboot、shutdown、mkfs、dd、kill、systemctl stop 等破坏性操作。\n"
            "3. 如果没有完全匹配，也要给出最接近的只读命令。\n\n"
            f"skills = {json.dumps(get_skill_catalog(), ensure_ascii=False)}\n"
            f"用户任务 = {task_description}"
        )

        await send_event("agent_status", {"message": "正在请求 llm_serv 进行技能规划..."})
        planning_text = await self.llm_service.query_text(planning_prompt)
        parsed = self._extract_json(planning_text or "")

        if parsed and parsed.get("command"):
            command = str(parsed.get("command", "")).strip()
            if not self._is_safe_command(command):
                await send_event(
                    "agent_status",
                    {"message": "模型规划出的命令存在风险，已切换到安全兜底方案"},
                )
            else:
                return {
                    "skill_name": str(parsed.get("skill_name", "custom_readonly_skill")),
                    "skill_description": str(parsed.get("skill_description", "模型规划技能")),
                    "command": command,
                    "source": "llm",
                }

        await send_event(
            "agent_status",
            {"message": "技能规划结果无法解析，改用通用只读命令兜底"},
        )
        return {
            "skill_name": "generic_probe",
            "skill_description": "通用系统信息探测",
            "command": "uname -a && echo '---' && uptime && echo '---' && df -h",
            "source": "fallback",
        }

    async def run_task(
        self,
        session_id: int,
        machine_id: int,
        task_description: str,
        send_event: Callable[[str, Dict[str, Any]], Awaitable[None]],
    ) -> None:
        try:
            if session_id:
                await add_message(session_id, "user", task_description)

            machine = await get_machine(machine_id)
            if not machine:
                await send_event("error", {"message": "未找到目标机器，请先在右侧连接机器"})
                return

            await send_event("thinking", {"message": task_description})
            await send_event(
                "agent_status",
                {
                    "message": f"目标机器: {machine['ip']}，开始分析任务并匹配 skills 模块",
                },
            )

            skill_plan = await self._select_skill(task_description, send_event)
            command = skill_plan["command"]

            await send_event(
                "agent_status",
                {
                    "message": f"技能选择完成: {skill_plan['skill_name']}，执行命令: {command}",
                },
            )
            await send_event(
                "tool_start",
                {
                    "tool_name": skill_plan["skill_name"],
                    "arguments": {"command": command, "machine_ip": machine["ip"]},
                },
            )

            exec_result = await remote_executor.execute(machine, command)

            exec_message = (
                f"返回码: {exec_result['returncode']}\n"
                f"标准输出:\n{exec_result['stdout'] or '(空)'}\n"
                f"标准错误:\n{exec_result['stderr'] or '(空)'}\n"
            )
            await send_event("execution_output", {"content": exec_message})
            await send_event(
                "tool_result",
                {
                    "tool_name": skill_plan["skill_name"],
                    "success": exec_result["success"],
                    "message": "远程命令执行完成" if exec_result["success"] else "远程命令执行失败",
                },
            )

            summary_prompt = (
                "你是一个 Linux 运维分析助手。请结合用户任务描述和命令输出，"
                "用中文给出简洁准确的结论。若执行失败，要明确失败原因。"
                "如果能直接回答用户问题，就先给结论，再给关键依据。\n\n"
                f"用户任务: {task_description}\n"
                f"技能名称: {skill_plan['skill_name']}\n"
                f"执行命令: {command}\n"
                f"命令返回码: {exec_result['returncode']}\n"
                f"标准输出:\n{exec_result['stdout'] or '(空)'}\n"
                f"标准错误:\n{exec_result['stderr'] or '(空)'}\n"
            )

            await send_event("agent_status", {"message": "正在把执行结果回送 llm_serv 生成最终结论..."})
            final_text = await self.llm_service.query_text(
                summary_prompt,
                lambda chunk: send_event("llm_stream", {"content": chunk}),
            )

            if session_id:
                await add_message(session_id, "assistant", final_text)

            await send_event("llm_done", {"summary": "任务分析完成"})
        except Exception as e:
            await send_event("error", {"message": f"任务执行失败: {e}"})


task_agent_service = TaskAgentService()
