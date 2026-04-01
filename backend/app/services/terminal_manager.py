import asyncio
import shlex
import shutil
from pathlib import Path
from typing import Any, Dict, List, Optional

from ..config import get_config


class TerminalManager:
    def __init__(self) -> None:
        self.process: Optional[asyncio.subprocess.Process] = None

    async def is_running(self) -> bool:
        config = get_config().terminal
        try:
            _, writer = await asyncio.wait_for(
                asyncio.open_connection("127.0.0.1", config.port),
                timeout=2.0,
            )
            writer.close()
            await writer.wait_closed()
            return True
        except Exception:
            return False

    async def _build_ttyd(self, ttyd_binary: Path) -> None:
        repo_root = Path(__file__).resolve().parents[3]
        build_dir = ttyd_binary.parent
        build_dir.mkdir(parents=True, exist_ok=True)

        configure = await asyncio.create_subprocess_exec(
            "cmake",
            "-S",
            str(repo_root / "webtty"),
            "-B",
            str(build_dir),
            cwd=str(repo_root),
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT,
        )
        configure_output, _ = await configure.communicate()
        if configure.returncode != 0:
            output = (configure_output or b"").decode("utf-8", errors="ignore")
            raise RuntimeError(f"cmake配置失败: {output}")

        build = await asyncio.create_subprocess_exec(
            "cmake",
            "--build",
            str(build_dir),
            "-j",
            cwd=str(repo_root),
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT,
        )
        build_output, _ = await build.communicate()
        if build.returncode != 0:
            output = (build_output or b"").decode("utf-8", errors="ignore")
            raise RuntimeError(f"cmake编译失败: {output}")

    def _resolve_shell_command(
        self, machine: Optional[Dict[str, Any]] = None
    ) -> List[str]:
        config = get_config().terminal

        if config.command.strip():
            return shlex.split(config.command)

        ssh_host = config.ssh_host.strip() or config.host.strip() or "localhost"
        ssh_user = config.ssh_user.strip()
        ssh_port = config.ssh_port
        ssh_password = ""

        if machine:
            ssh_host = str(machine.get("ip", "")).strip() or ssh_host
            ssh_user = str(machine.get("username", "")).strip() or ssh_user
            ssh_password = str(machine.get("password", ""))

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
            if sshpass_bin:
                return [sshpass_bin, "-p", ssh_password, *ssh_cmd]
            raise RuntimeError(
                "检测到机器已配置密码，但服务器未安装 sshpass，"
                "无法自动输入 SSH 密码。请安装 sshpass 后重试。"
            )

        return ssh_cmd

    async def start(self, machine: Optional[Dict[str, Any]] = None) -> Dict[str, str]:
        config = get_config().terminal
        try:
            if await asyncio.wait_for(self.is_running(), timeout=3.0):
                await self.stop()
        except asyncio.TimeoutError:
            await self._force_kill_port(config.port)

        ttyd_binary = Path(config.ttyd_binary)
        if not ttyd_binary.is_absolute():
            repo_root = Path(__file__).resolve().parents[3]
            ttyd_binary = repo_root / ttyd_binary

        if not ttyd_binary.exists():
            if not config.auto_build:
                raise RuntimeError(f"未找到ttyd二进制文件: {ttyd_binary}")
            await self._build_ttyd(ttyd_binary)

        shell_command = self._resolve_shell_command(machine)
        cmd = [
            str(ttyd_binary),
            "-i",
            config.ttyd_interface,
            "-p",
            str(config.port),
            *shell_command,
        ]

        self.process = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT,
        )

        startup_timeout = max(config.startup_timeout_sec, 1)
        deadline = asyncio.get_event_loop().time() + startup_timeout
        while asyncio.get_event_loop().time() < deadline:
            if await self.is_running():
                return {"success": "true", "message": "终端服务启动成功"}
            if self.process and self.process.returncode is not None:
                if self.process.stdout:
                    stdout_data = await self.process.stdout.read()
                    output = stdout_data.decode("utf-8", errors="ignore")
                else:
                    output = ""
                raise RuntimeError(f"终端服务启动失败: {output}")
            await asyncio.sleep(0.1)

        await self.stop()
        raise RuntimeError("终端服务启动超时")

    async def _force_kill_port(self, port: int) -> None:
        try:
            proc = await asyncio.create_subprocess_exec(
                "fuser",
                "-k",
                f"{port}/tcp",
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
            await asyncio.wait_for(proc.wait(), timeout=5.0)
        except Exception:
            pass

    async def stop(self) -> None:
        if self.process and self.process.returncode is None:
            self.process.terminate()
            try:
                await asyncio.wait_for(self.process.wait(), timeout=3)
            except asyncio.TimeoutError:
                self.process.kill()
                await self.process.wait()

        self.process = None


terminal_manager = TerminalManager()
