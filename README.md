# CheckPilot - VNC远程测试平台

基于VNC和AI的远程测试平台PoC，可以通过自然语言指令让AI自动操作远程计算机。

## 功能特性

- **VNC连接**: 连接到远程VNC服务器进行显示和控制
- **AI助手**: 通过自然语言描述测试任务，AI自动执行操作
- **工具系统**: AI可使用截图、点击、输入等工具操作远程桌面
- **截图分析**: 支持截图并用视觉模型分析当前屏幕状态
- **坐标归一化**: Vision模型返回0-1000归一化坐标，自动换算到实际像素

## 快速开始

### 1. 安装依赖

```bash
uv sync
```

### 2. 配置

编辑 `config.yaml`：

```yaml
vnc:
  host: "192.168.1.100"
  port: 5900
  password: ""

chat:
  api_base: "http://localhost:4000/v1"
  api_key: "your-api-key"
  model: "glm-4"
  temperature: 0.1

vision:
  api_base: "http://localhost:4000/v1"
  api_key: "your-api-key"
  model: "glm-4.6v"

screenshot:
  enabled: true
  quality: 85
```

### 3. 启动服务

```bash
cd backend
uv run uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
```

服务将在 `http://localhost:8000` 启动。

### 4. 使用

1. 打开浏览器访问 `http://localhost:8000`
2. 点击"开始测试"后，后端会自动检查/编译并启动 `ttyd`，再连接到配置的远程Linux终端
3. 在右侧聊天框输入测试指令，例如：
   - "用浏览器打开百度首页"
   - "在桌面搜索框输入hello"
   - "点击屏幕上的Chrome浏览器"

## 项目结构

```
checkpilot/
├── backend/
│   ├── app/
│   │   ├── main.py           # FastAPI入口
│   │   ├── config.py          # 配置加载 (Pydantic)
│   │   ├── database.py        # SQLite数据库 (aiosqlite)
│   │   ├── api/
│   │   │   ├── vnc.py        # VNC连接API
│   │   │   ├── vnc_websocket.py # VNC WebSocket代理
│   │   │   ├── ai.py         # AI对话和工具执行API
│   │   │   └── chat.py       # 聊天历史API
│   │   ├── services/
│   │   │   ├── vnc_client.py # VNC客户端 (vncdotool)
│   │   │   ├── tools.py      # 工具定义和执行
│   │   │   └── ai_engine.py  # AI调用封装
│   │   └── models/
│   │       └── schemas.py    # 数据模型
│   ├── pyproject.toml
│   └── checkpilot.db        # SQLite数据库
├── frontend/
│   ├── index.html           # 主页面
│   ├── css/style.css        # 样式
│   ├── js/app.js            # 前端逻辑
│   └── noVNC/              # noVNC库
├── config.yaml               # 配置文件
├── AGENTS.md               # AI助手开发指南
└── README.md
```

## 可用工具

| 工具 | 描述 | 参数 |
|------|------|------|
| take_screenshot | 截取屏幕并分析 | prompt |
| click | 左键点击 | x, y |
| double_click | 双击 | x, y |
| right_click | 右键点击 | x, y |
| move_cursor | 移动鼠标 | x, y |
| type_text | 输入文本 | text |
| press_key | 按单个按键 | key |
| press_key_combo | 按组合键 | keys (列表) |
| scroll | 滚动 | direction, amount |
| finish_session | 完成任务 | summary |

**注意**: Vision模型返回0-1000归一化坐标，系统会自动换算到实际屏幕像素。

**禁用按键**: F1-F12、ESC、Insert、PrintScreen、PageUp/Down、Pause/Break会触发Gnome远程桌面通知，导致连接超时，绝对不要使用。

## 重要规则

1. **截图不存储在数据库**: 截图base64仅用于前端预览和视觉模型分析，不写入数据库
2. **不要发送base64给chat模型**: 工具执行层会过滤screenshot字段
3. **使用shift+enter登录**: 不是只按enter
4. **异步VNC操作**: 所有VNC操作通过`asyncio.to_thread()`执行，避免阻塞

## 开发命令

```bash
# 安装依赖
uv sync

# 运行服务器
cd backend && uv run uvicorn app.main:app --reload --host 0.0.0.0 --port 8000

# 代码检查
uv run ruff check .

# 自动修复
uv run ruff check --fix
```

## 依赖

- Python 3.10+
- FastAPI + Uvicorn
- vncdotool (VNC客户端)
- OpenAI SDK (与LiteLLM兼容)
- aiosqlite (异步数据库)
- Pillow (图像处理)
- noVNC (前端VNC查看器)

## WebTTY终端配置

在 `config.yaml` 中配置 `terminal` 段，前端点击“开始测试”后会读取该配置并在左侧 iframe 中打开远程终端：

```yaml
terminal:
  enabled: true
  protocol: "http"
  host: ""                # WebTTY页面访问主机，留空默认当前页面主机
  port: 7681
  path: "/"
  auto_start: true
  auto_build: true
  ttyd_binary: "webtty/build/ttyd"
  ttyd_interface: "0.0.0.0"
  browser_host: ""
  ssh_host: "172.23.192.238" # ttyd通过ssh连接该主机（本机填localhost）
  ssh_user: "root"
  ssh_port: 22
  startup_timeout_sec: 10
```

### WebTTY编译说明（手动）

如果你不希望自动编译，也可以手动执行：

```bash
cmake -S webtty -B webtty/build
cmake --build webtty/build -j
```

编译后会生成 `webtty/build/ttyd`，再启动后端服务即可。

## 注意事项

- 确保VNC服务器已启动并可访问
- LiteLLM服务需配置支持工具调用的模型
- 禁用F1-F12等按键以避免Gnome通知弹窗
