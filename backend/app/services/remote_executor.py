import asyncio
import shlex
import shutil
from typing import Any, Dict, List

from ..config import get_config


class RemoteExecutor:
    def _build_ssh_prefix(self, machine: Dict[str, Any]) -> List[str]:
        config = get_config().terminal

        ssh_host = str(machine.get("ip", "")).strip() or config.ssh_host.strip() or "localhost"
        ssh_user = str(machine.get("username", "")).strip() or config.ssh_user.strip()
        ssh_password = str(machine.get("password", ""))
        ssh_port = config.ssh_port

        ssh_target = f"{ssh_user}@{ssh_host}" if ssh_user else ssh_host
        ssh_cmd = [
            "ssh",
            "-4",
            "-o",
            "StrictHostKeyChecking=no",
            "-o",
            "UserKnownHostsFile=/dev/null",
            "-o",
            "GSSAPIAuthentication=no",
            "-o",
            "GSSAPIKeyExchange=no",
            "-o",
            "ConnectTimeout=10",
            "-o",
            "PreferredAuthentications=password,keyboard-interactive,publickey",
            "-p",
            str(ssh_port),
            ssh_target,
        ]

        if ssh_password:
            sshpass_bin = shutil.which("sshpass")
            if not sshpass_bin:
                raise RuntimeError("检测到机器配置了密码，但当前环境未安装 sshpass")
            return [sshpass_bin, "-p", ssh_password, *ssh_cmd]

        return ssh_cmd

    async def execute(
        self,
        machine: Dict[str, Any],
        command: str,
        timeout_sec: int = 30,
    ) -> Dict[str, Any]:
        ssh_cmd = self._build_ssh_prefix(machine)
        remote_command = f"bash -lc {shlex.quote(command)}"

        process = await asyncio.create_subprocess_exec(
            *ssh_cmd,
            remote_command,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )

        try:
            stdout, stderr = await asyncio.wait_for(process.communicate(), timeout=timeout_sec)
        except asyncio.TimeoutError:
            process.kill()
            await process.wait()
            return {
                "success": False,
                "command": command,
                "stdout": "",
                "stderr": f"命令执行超时（>{timeout_sec} 秒）",
                "returncode": -1,
            }

        return {
            "success": process.returncode == 0,
            "command": command,
            "stdout": (stdout or b"").decode("utf-8", errors="ignore"),
            "stderr": (stderr or b"").decode("utf-8", errors="ignore"),
            "returncode": process.returncode,
        }


remote_executor = RemoteExecutor()
