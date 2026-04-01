"""
VNC 客户端管理器 - 使用 vncdotool Python API
在独立线程中运行 Twisted reactor，方法是同步的
"""

import base64
import os
import sys
import time
import threading
from typing import Optional, Set
from PIL import Image
from io import BytesIO

sys.path.insert(0, "/home/xphi/reference/vncdotool")
from vncdotool import api
from twisted.internet import reactor

DISABLED_KEYS: Set[str] = {
    "f1",
    "f2",
    "f3",
    "f4",
    "f5",
    "f6",
    "f7",
    "f8",
    "f9",
    "f10",
    "f11",
    "f12",
    "escape",
    "esc",
    "insert",
    "print",
    "printscreen",
    "prtsc",
    "pause",
    "break",
    "pageup",
    "page_up",
    "pagedown",
    "page_down",
}


class VNCClientManager:
    def __init__(self):
        self.client = None
        self.connected = False
        self.reactor_thread = None

    def _start_reactor(self):
        if self.reactor_thread and self.reactor_thread.is_alive():
            return
        self.reactor_thread = threading.Thread(target=self._run_reactor, daemon=True)
        self.reactor_thread.start()
        time.sleep(0.5)

    def _run_reactor(self):
        reactor.run(installSignalHandlers=False)

    def _stop_reactor(self):
        if reactor.running:
            reactor.callFromThread(reactor.stop)
        time.sleep(0.2)

    def connect(self, host: str, port: int, password: str = "") -> bool:
        try:
            self._start_reactor()
            server = f"{host}::{port}" if port != 5900 else f"{host}"
            print(f"[VNC] 连接到 {server}...")
            self.client = api.connect(server, password=password)
            self.connected = True
            print("[VNC] 连接成功！")
            return True
        except Exception as e:
            print(f"[VNC] 连接失败: {e}")
            self.client = None
            self.connected = False
            return False

    def disconnect(self):
        if self.client:
            print("[VNC] 断开连接...")
            try:
                self.client.disconnect()
            except:
                pass
            self.client = None
            self.connected = False
        self._stop_reactor()

    def mouse_move(self, x: int, y: int) -> bool:
        if not self.connected:
            return False
        try:
            self.client.mouseMove(x, y)
            return True
        except Exception as e:
            print(f"[VNC] 移动失败: {e}")
            self.connected = False
            return False

    def move_cursor(self, x: int, y: int) -> bool:
        return self.mouse_move(x, y)

    def click(self, x: int, y: int, button: int = 1) -> bool:
        if not self.connected:
            return False
        try:
            self.client.mouseMove(x, y)
            self.client.mousePress(button)
            return True
        except Exception as e:
            print(f"[VNC] 点击失败: {e}")
            self.connected = False
            return False

    def double_click(self, x: int, y: int) -> bool:
        if not self.connected:
            return False
        try:
            self.client.mouseMove(x, y)
            self.client.mousePress(1)
            self.client.mousePress(1)
            return True
        except Exception as e:
            print(f"[VNC] 双击失败: {e}")
            self.connected = False
            return False

    def right_click(self, x: int, y: int) -> bool:
        if not self.connected:
            return False
        try:
            self.client.mouseMove(x, y)
            self.client.mousePress(3)
            return True
        except Exception as e:
            print(f"[VNC] 右击失败: {e}")
            self.connected = False
            return False

    def type_text(self, text: str) -> bool:
        if not self.connected:
            return False
        try:
            for char in text:
                self.client.keyPress(char)
            return True
        except Exception as e:
            print(f"[VNC] 输入失败: {e}")
            self.connected = False
            return False

    def press_key(self, key: str) -> bool:
        if not self.connected:
            return False
        key_lower = key.lower()
        if key_lower in DISABLED_KEYS:
            print(f"[VNC] 按键 {key} 被禁用")
            return False
        try:
            self.client.keyPress(key)
            return True
        except Exception as e:
            print(f"[VNC] 按键失败: {e}")
            self.connected = False
            return False

    def key_down(self, key: str) -> bool:
        if not self.connected:
            return False
        key_lower = key.lower()
        if key_lower in DISABLED_KEYS:
            return False
        try:
            self.client.keyDown(key)
            return True
        except Exception as e:
            self.connected = False
            return False

    def key_up(self, key: str) -> bool:
        if not self.connected:
            return False
        key_lower = key.lower()
        if key_lower in DISABLED_KEYS:
            return False
        try:
            self.client.keyUp(key)
            return True
        except Exception as e:
            self.connected = False
            return False

    def press_key_combo(self, keys: list) -> bool:
        if not self.connected:
            return False
        for key in keys:
            if not self.key_down(key):
                return False
        for key in reversed(keys):
            if not self.key_up(key):
                return False
        return True

    def scroll(self, direction: str = "down", amount: int = 3) -> bool:
        if not self.connected:
            return False
        button = 5 if direction.lower() == "down" else 4
        try:
            for _ in range(amount):
                self.client.mousePress(button)
            return True
        except Exception as e:
            print(f"[VNC] 滚动失败: {e}")
            self.connected = False
            return False

    def capture_screen(
        self, quality: int = 85
    ) -> tuple[Optional[str], Optional[tuple[int, int]]]:
        """
        截取屏幕，返回 (base64字符串, (宽度, 高度)) 或 (None, None)
        保持原图尺寸，不压缩
        """
        if not self.connected:
            return None, None
        temp_file = "/tmp/checkpilot_screenshot.png"
        try:
            self.client.captureScreen(temp_file)
            time.sleep(0.3)
            if not os.path.exists(temp_file):
                print("[VNC] 截图文件不存在")
                return None, None
            img = Image.open(temp_file)
            actual_width, actual_height = img.size  # 获取实际分辨率
            buffer = BytesIO()
            img.save(buffer, format="JPEG", quality=quality)
            os.remove(temp_file)
            screenshot_b64 = base64.b64encode(buffer.getvalue()).decode("utf-8")
            return screenshot_b64, (actual_width, actual_height)
        except Exception as e:
            print(f"[VNC] 截图失败: {e}")
            return None, None


vnc_manager = VNCClientManager()
