"""
VNC 客户端管理器 - 使用 vncdotool Python API
使用单独的线程运行 reactor
"""

import sys
import os
import time
import threading
from vncdotool import api
from twisted.internet import reactor


class VNCClientManager:
    """VNC 客户端管理器 - 维护持久连接"""

    def __init__(self):
        self.client = None
        self.connected = False
        self.reactor_thread = None
        self.reactor_running = False

    def _start_reactor(self):
        """在独立线程中启动 reactor"""
        if self.reactor_thread and self.reactor_thread.is_alive():
            return

        self.reactor_running = True
        self.reactor_thread = threading.Thread(target=self._run_reactor, daemon=True)
        self.reactor_thread.start()
        time.sleep(0.5)  # 等待 reactor 启动

    def _run_reactor(self):
        """运行 reactor"""
        self.reactor_running = True
        reactor.run(installSignalHandlers=False)
        self.reactor_running = False

    def _stop_reactor(self):
        """停止 reactor"""
        if reactor.running:
            reactor.callFromThread(reactor.stop)
        time.sleep(0.2)
        self.reactor_running = False

    def connect(self, host: str, port: int, password: str = "") -> bool:
        """建立 VNC 连接"""
        try:
            # 先启动 reactor
            self._start_reactor()

            server = f"{host}::{port}" if port != 5900 else f"{host}"
            print(f"[VNC] 连接到 {server}...")

            # 使用 api.connect 创建客户端
            self.client = api.connect(server, password=password)
            self.connected = True
            print(f"[VNC] 连接成功！")
            return True
        except Exception as e:
            print(f"[VNC] 连接失败: {e}")
            self.client = None
            self.connected = False
            return False

    def disconnect(self):
        """断开 VNC 连接"""
        if self.client:
            print("[VNC] 断开连接...")
            try:
                self.client.disconnect()
            except:
                pass
            self.client = None
            self.connected = False

        # 停止 reactor
        self._stop_reactor()

    def mouse_move(self, x: int, y: int) -> bool:
        """移动鼠标"""
        if not self.connected:
            return False
        try:
            self.client.mouseMove(x, y)
            return True
        except Exception as e:
            print(f"[VNC] 移动失败: {e}")
            self.connected = False
            return False

    def click(self, x: int, y: int, button: int = 1) -> bool:
        """点击鼠标"""
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
        """双击鼠标"""
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
        """右击鼠标"""
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
        """输入文本"""
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
        """按键"""
        if not self.connected:
            return False
        try:
            self.client.keyPress(key)
            return True
        except Exception as e:
            print(f"[VNC] 按键失败: {e}")
            self.connected = False
            return False

    def capture_screen(self, filename: str = "/tmp/checkpilot_capture.png") -> bytes:
        """截取屏幕"""
        if not self.connected:
            return b""
        try:
            self.client.captureScreen(filename)
            time.sleep(0.5)  # 等待截图完成
            if os.path.exists(filename):
                with open(filename, "rb") as f:
                    data = f.read()
                return data
            return b""
        except Exception as e:
            print(f"[VNC] 截图失败: {e}")
            return b""


# 全局实例
vnc_manager = VNCClientManager()


# 测试代码
if __name__ == "__main__":
    print("=" * 60)
    print("测试 VNC 持久连接")
    print("=" * 60)

    try:
        # 连接
        if not vnc_manager.connect("10.31.94.173", 5900, "123456abc"):
            print("❌ 连接失败")
            exit(1)

        print("✅ 连接成功")

        # 测试鼠标操作
        print("\n测试鼠标操作...")
        vnc_manager.mouse_move(500, 600)
        print("  ✅ 移动")
        vnc_manager.click(500, 600)
        print("  ✅ 单击")
        vnc_manager.double_click(500, 600)
        print("  ✅ 双击")
        vnc_manager.right_click(500, 600)
        print("  ✅ 右击")

        # 测试输入
        print("\n测试输入...")
        vnc_manager.type_text("hello")
        print("  ✅ 输入 hello")

        # 测试截图
        print("\n测试截图...")
        data = vnc_manager.capture_screen()
        if data:
            print(f"  ✅ 截图成功 ({len(data)} bytes)")
        else:
            print("  ❌ 截图失败")

    finally:
        # 断开连接
        vnc_manager.disconnect()
        print("\n" + "=" * 60)
        print("测试完成！")
