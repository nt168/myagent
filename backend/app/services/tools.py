import asyncio
from typing import Dict, Any
from ..config import get_config
from ..services.vnc_client import vnc_manager


tool_system_prompt = """你是一个远程测试助手，帮助用户操作远程计算机完成任务。

## 当前任务状态

用户给你一个任务后，你应该：
1. 理解任务目标
2. 执行必要的操作步骤
3. **根据工具执行结果决定下一步**，而不是重复相同的操作

## 可用工具

1. **take_screenshot** - 截取屏幕并分析 (参数: prompt - 分析提示词)
2. **click** - 点击鼠标左键 (参数: x, y)
3. **double_click** - 双击鼠标左键 (参数: x, y)
4. **right_click** - 点击鼠标右键 (参数: x, y)
5. **move_cursor** - 移动鼠标 (参数: x, y)
6. **type_text** - 输入文本 (参数: text)
7. **press_key** - 按单个按键 (参数: key)
8. **press_key_combo** - 按组合键 (参数: keys - 按键列表)
9. **scroll** - 滚动页面 (参数: direction, amount)
10. **finish_session** - 完成任务 (参数: summary)

## 重要规则

1. **工具结果会自动发送给你**
    - 你不需要在回复中重复工具的结果
    - 你只需要根据结果决定下一步操作
    - 如果需要继续操作，直接调用下一个工具

2. **看到工具结果后，要基于结果决定下一步**
    - 如果看到"登录界面" → 输入密码，然后按 **shift+enter** 登录（不是只按 enter）
    - 如果看到"桌面" → 打开浏览器
    - 如果看到"浏览器打开" → 输入网址
    - 如果看到"百度首页" → 任务完成，调用 finish_session

3. **登录操作必须用 shift+enter**
    - 输入密码后，必须使用 press_key_combo(keys=["shift", "enter"]) 登录
    - 不能只用 press_key("enter")

4. **禁用按键**
    - F1-F12、ESC、Insert、PrintScreen、PageUp/Down、Pause/Break 会触发 Gnome 远程桌面通知，导致连接超时
    - **绝对不要使用这些按键**
    - 如果需要这些功能，使用鼠标操作替代

5. **不要重复调用相同的工具**
    - 如果刚执行过截图，不要又说"让我截取屏幕"
    - 如果工具结果显示了信息，直接基于信息行动

6. **用中文思考和回复**
    - 执行操作时直接行动，不需要过多解释
    - 只在任务开始或完成时说话

7. **工具调用格式**
    - 如果需要调用工具，返回空的 content
    - 工具调用会通过 tool_calls 字段传递
    - 不要在 content 中描述工具结果

## 工作流程示例

用户: "用浏览器打开百度首页"
行动: 调用take_screenshot(prompt="查看桌面是否有浏览器")

工具执行后，结果会自动发送给你。
如果结果显示"登录界面"，下一步行动: 调用type_text(text="密码") 然后调用press_key_combo(keys=["shift", "enter"])
如果结果显示"桌面，有Chrome浏览器"，下一步行动: 调用click(x坐标, y坐标) 双击Chrome图标
如果结果显示"浏览器已打开"，下一步行动: 调用type_text(text="www.baidu.com") 调用press_key_combo(keys=["enter"])
如果结果显示"百度首页已显示"，下一步行动: 调用finish_session(summary="已成功打开百度首页")"""


def get_tool_definitions():
    return [
        {
            "type": "function",
            "function": {
                "name": "take_screenshot",
                "display_name": "拍摄截屏",
                "description": "截取当前屏幕并分析画面内容。返回屏幕状态描述和元素位置坐标。",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "prompt": {
                            "type": "string",
                            "description": "分析提示词，告诉工具需要关注什么",
                        }
                    },
                    "required": ["prompt"],
                },
            },
        },
        {
            "type": "function",
            "function": {
                "name": "click",
                "display_name": "点击",
                "description": "在指定坐标点击鼠标左键",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "x": {"type": "integer", "description": "X坐标"},
                        "y": {"type": "integer", "description": "Y坐标"},
                    },
                    "required": ["x", "y"],
                },
            },
        },
        {
            "type": "function",
            "function": {
                "name": "double_click",
                "display_name": "双击",
                "description": "在指定坐标双击鼠标左键",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "x": {"type": "integer", "description": "X坐标"},
                        "y": {"type": "integer", "description": "Y坐标"},
                    },
                    "required": ["x", "y"],
                },
            },
        },
        {
            "type": "function",
            "function": {
                "name": "right_click",
                "display_name": "右键点击",
                "description": "在指定坐标点击鼠标右键",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "x": {"type": "integer", "description": "X坐标"},
                        "y": {"type": "integer", "description": "Y坐标"},
                    },
                    "required": ["x", "y"],
                },
            },
        },
        {
            "type": "function",
            "function": {
                "name": "move_cursor",
                "display_name": "移动鼠标",
                "description": "移动鼠标到指定位置",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "x": {"type": "integer", "description": "X坐标"},
                        "y": {"type": "integer", "description": "Y坐标"},
                    },
                    "required": ["x", "y"],
                },
            },
        },
        {
            "type": "function",
            "function": {
                "name": "type_text",
                "display_name": "输入文本",
                "description": "输入文本内容",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "text": {"type": "string", "description": "要输入的文本"}
                    },
                    "required": ["text"],
                },
            },
        },
        {
            "type": "function",
            "function": {
                "name": "press_key",
                "display_name": "按键",
                "description": "按下单个按键（如 enter, a, 1 等）。注意：F1-F12、ESC、Insert、PrintScreen、PageUp/Down、Pause/Break 被禁用，会导致连接超时。",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "key": {"type": "string", "description": "按键名称"}
                    },
                    "required": ["key"],
                },
            },
        },
        {
            "type": "function",
            "function": {
                "name": "press_key_combo",
                "display_name": "组合键",
                "description": "按下组合键（如 shift+enter）。需要同时按下的按键列表，依次按下再依次释放。常用于登录时输入密码后按 shift+enter。",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "keys": {
                            "type": "array",
                            "items": {"type": "string"},
                            "description": "要按下的按键列表，依次按下再依次释放",
                        }
                    },
                    "required": ["keys"],
                },
            },
        },
        {
            "type": "function",
            "function": {
                "name": "scroll",
                "display_name": "滚动",
                "description": "滚动页面",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "direction": {
                            "type": "string",
                            "enum": ["up", "down"],
                            "description": "滚动方向",
                        },
                        "amount": {
                            "type": "integer",
                            "description": "滚动行数",
                            "default": 3,
                        },
                    },
                    "required": ["direction"],
                },
            },
        },
        {
            "type": "function",
            "function": {
                "name": "finish_session",
                "display_name": "完成",
                "description": "任务完成，生成总结报告并结束会话",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "summary": {"type": "string", "description": "任务完成总结"},
                    },
                    "required": ["summary"],
                },
            },
        },
    ]


async def execute_tool(
    tool_name: str, arguments: Dict[str, Any], quality: int = 85
) -> Dict[str, Any]:
    result = {"success": False, "message": "", "data": None}

    print(f"\n[Tool Execute] {tool_name}")
    print(f"[Tool Execute] Arguments: {arguments}")

    # 获取当前屏幕分辨率
    screen_width, screen_height = 1920, 1080  # 默认值
    if vnc_manager.connected:
        screenshot_result = vnc_manager.capture_screen(quality=10)  # 快速获取分辨率
        if screenshot_result and screenshot_result[1]:
            screen_width, screen_height = screenshot_result[1]

    try:
        if tool_name == "take_screenshot":
            prompt = arguments.get("prompt", "分析当前屏幕")

            print(f"[Tool Execute] Capturing screenshot...")
            screenshot_result = await asyncio.to_thread(
                vnc_manager.capture_screen, quality
            )
            if not screenshot_result or not screenshot_result[0]:
                return {"success": False, "message": "截图失败：VNC未连接"}

            screenshot_b64, resolution = screenshot_result
            screen_width, screen_height = resolution

            print(
                f"[Tool Execute] Screenshot captured: {len(screenshot_b64)} bytes, resolution: {screen_width}x{screen_height}"
            )
            print(f"[Tool Execute] Calling vision model with prompt: {prompt}")

            from .ai_engine import ai_engine

            analysis = await ai_engine.analyze_screenshot(screenshot_b64, prompt)

            import datetime

            now = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            print(
                f"[{now}] [Tool Execute] Vision analysis result: {analysis['success']}",
                flush=True,
            )
            if analysis["success"]:
                print(
                    f"[{now}] [Tool Execute] Analysis preview:\n{analysis['analysis']}\n[End Analysis]",
                    flush=True,
                )
                result["success"] = True
                result["message"] = f"截图成功"
                result["data"] = {
                    "resolution": {"width": screen_width, "height": screen_height},
                    "screenshot": screenshot_b64,  # 返回给前端显示，不存储到数据库
                    "analysis": analysis["analysis"],
                }
            else:
                result["success"] = False
                result["message"] = (
                    f"截图成功但分析失败: {analysis.get('error', '未知错误')}"
                )
                result["data"] = {
                    "resolution": {"width": screen_width, "height": screen_height}
                }

        elif tool_name == "click":
            norm_x, norm_y = arguments.get("x", 0), arguments.get("y", 0)
            # 归一化坐标换算到实际像素
            x = int(norm_x * screen_width / 1000)
            y = int(norm_y * screen_height / 1000)
            success = await asyncio.to_thread(vnc_manager.click, x, y)
            result["success"] = success
            result["message"] = (
                f"点击 ({norm_x}, {norm_y}) → ({x}, {y}) {'成功' if success else '失败'}"
            )

        elif tool_name == "double_click":
            norm_x, norm_y = arguments.get("x", 0), arguments.get("y", 0)
            x = int(norm_x * screen_width / 1000)
            y = int(norm_y * screen_height / 1000)
            success = await asyncio.to_thread(vnc_manager.double_click, x, y)
            result["success"] = success
            result["message"] = (
                f"双击 ({norm_x}, {norm_y}) → ({x}, {y}) {'成功' if success else '失败'}"
            )

        elif tool_name == "right_click":
            norm_x, norm_y = arguments.get("x", 0), arguments.get("y", 0)
            x = int(norm_x * screen_width / 1000)
            y = int(norm_y * screen_height / 1000)
            success = await asyncio.to_thread(vnc_manager.right_click, x, y)
            result["success"] = success
            result["message"] = (
                f"右键点击 ({norm_x}, {norm_y}) → ({x}, {y}) {'成功' if success else '失败'}"
            )

        elif tool_name == "move_cursor":
            norm_x, norm_y = arguments.get("x", 0), arguments.get("y", 0)
            x = int(norm_x * screen_width / 1000)
            y = int(norm_y * screen_height / 1000)
            success = await asyncio.to_thread(vnc_manager.move_cursor, x, y)
            result["success"] = success
            result["message"] = (
                f"移动鼠标到 ({norm_x}, {norm_y}) → ({x}, {y}) {'成功' if success else '失败'}"
            )

        elif tool_name == "type_text":
            text = arguments.get("text", "")
            success = await asyncio.to_thread(vnc_manager.type_text, text)
            result["success"] = success
            result["message"] = (
                f"输入文本: {text[:50]}{'...' if len(text) > 50 else ''} {'成功' if success else '失败'}"
            )

        elif tool_name == "press_key":
            key = arguments.get("key", "")
            success = await asyncio.to_thread(vnc_manager.press_key, key)
            result["success"] = success
            result["message"] = f"按键 {key} {'成功' if success else '失败'}"

        elif tool_name == "press_key_combo":
            keys = arguments.get("keys", [])
            if not keys:
                result["message"] = "组合键列表不能为空"
            else:
                success = await asyncio.to_thread(vnc_manager.press_key_combo, keys)
                result["success"] = success
                result["message"] = (
                    f"组合键 {'+'.join(keys)} {'成功' if success else '失败'}"
                )

        elif tool_name == "scroll":
            direction = arguments.get("direction", "down")
            amount = arguments.get("amount", 3)
            success = await asyncio.to_thread(vnc_manager.scroll, direction, amount)
            result["success"] = success
            result["message"] = (
                f"滚动 {direction} {amount} 行 {'成功' if success else '失败'}"
            )

        elif tool_name == "finish_session":
            summary = arguments.get("summary", "任务完成")
            result["success"] = True
            result["message"] = "任务完成"
            result["data"] = {"finished": True, "summary": summary}

        else:
            result["message"] = f"未知工具: {tool_name}"

    except Exception as e:
        result["message"] = f"执行工具失败: {str(e)}"

    return result
