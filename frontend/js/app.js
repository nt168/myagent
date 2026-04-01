class CheckPilotApp {
    constructor() {
        this.sessionId = null;
        this.permissionMode = false;
        this.directLLMMode = false;
        this.terminalConnected = false;
        this.abortController = null;
        this.isProcessing = false;
        this.machines = [];
        this.activeMachineIp = '';
        this.activeMachineId = null;
        this.ws = null;
        this.wsReconnectTimer = null;
        this.currentOutputBlock = null;
        this.init();
    }

    init() {
        this.loadMachines();
        this.bindEvents();
        this.updateTerminalStatus('未开始', false);
        this.addMessage('你好！我是 Agent AI 助手。请先连接远程机器，然后输入测试指令。', 'assistant');
    }

    connectAIWebSocket() {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            return;
        }
        const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        const wsUrl = protocol + '//' + window.location.host + '/api/ai/ws';
        this.ws = new WebSocket(wsUrl);

        this.ws.onopen = () => {
            console.log('[AI WS] Connected');
            if (this.sessionId) {
                this.ws.send(JSON.stringify({type: 'init', session_id: this.sessionId}));
            }
        };

        this.ws.onmessage = (event) => {
            try {
                const msg = JSON.parse(event.data);
                this.handleWSEvent(msg);
            } catch (e) {
                console.error('[AI WS] Parse error:', e);
            }
        };

        this.ws.onclose = () => {
            console.log('[AI WS] Disconnected');
            this.scheduleWSReconnect();
        };

        this.ws.onerror = (err) => {
            console.error('[AI WS] Error:', err);
        };
    }

    scheduleWSReconnect() {
        if (this.wsReconnectTimer) {
            clearTimeout(this.wsReconnectTimer);
        }
        this.wsReconnectTimer = setTimeout(() => {
            if (this.sessionId) {
                console.log('[AI WS] Reconnecting...');
                this.connectAIWebSocket();
            }
        }, 3000);
    }

    handleWSEvent(msg) {
        const type = msg.type;
        const data = msg.data || {};

        switch (type) {
            case 'connected':
                console.log('[AI WS] Session confirmed:', data.session_id);
                break;

            case 'thinking':
                this.startOutputBlock();
                this.appendOutput('思考中: ' + data.message + '\n');
                break;

            case 'reasoning':
                if (!this.currentOutputBlock) {
                    this.startOutputBlock();
                }
                this.appendOutput('[推理] ' + data.content + '\n');
                break;

            case 'agent_status':
                if (!this.currentOutputBlock) {
                    this.startOutputBlock();
                }
                this.appendOutput('[阶段] ' + data.message + '\n');
                break;

            case 'response':
                if (!this.currentOutputBlock) {
                    this.startOutputBlock();
                }
                this.appendOutput('[回复] ' + data.content + '\n');
                break;

            case 'llm_stream':
                if (!this.currentOutputBlock) {
                    this.startOutputBlock();
                }
                this.appendOutput(data.content);
                break;

            case 'llm_done':
                if (!this.currentOutputBlock) {
                    this.startOutputBlock();
                }
                this.appendOutput('\n[LLM] 推理已完成\n');
                this.finalizeOutputBlock();
                this.updateProcessingState(false);
                break;

            case 'tool_start':
                if (!this.currentOutputBlock) {
                    this.startOutputBlock();
                }
                this.appendOutput('[工具] ' + this.getToolDisplayName(data.tool_name) + ' 执行中...\n');
                break;

            case 'tool_result':
                this.appendOutput('[工具] ' + this.getToolDisplayName(data.tool_name) + ' → ' + (data.success ? '成功' : '失败') + ': ' + data.message + '\n');
                break;

            case 'execution_output':
                if (!this.currentOutputBlock) {
                    this.startOutputBlock();
                }
                this.appendOutput('[执行结果]\n' + data.content + '\n');
                break;

            case 'screenshot':
                if (data.screenshot) {
                    this.addScreenshot(data.screenshot, data.analysis || '');
                }
                break;

            case 'finished':
                this.appendOutput('\n[完成] ' + data.summary + '\n');
                this.finalizeOutputBlock();
                this.addMessage('任务完成: ' + data.summary, 'assistant');
                this.updateProcessingState(false);
                break;

            case 'error':
                this.appendOutput('[错误] ' + data.message + '\n');
                this.finalizeOutputBlock();
                this.addMessage('错误: ' + data.message, 'assistant');
                this.updateProcessingState(false);
                break;

            case 'done':
                this.finalizeOutputBlock();
                this.updateProcessingState(false);
                break;
        }
    }

    startOutputBlock() {
        if (this.currentOutputBlock) {
            this.finalizeOutputBlock();
        }
        const messages = document.getElementById('chat-messages');
        const div = document.createElement('div');
        div.className = 'message assistant ai-output-block';
        div.innerHTML = '<div class="ai-output-content"></div>';
        messages.appendChild(div);
        this.currentOutputBlock = div.querySelector('.ai-output-content');
        messages.scrollTop = messages.scrollHeight;
    }

    appendOutput(text) {
        if (!this.currentOutputBlock) {
            this.startOutputBlock();
        }
        this.currentOutputBlock.textContent += text;
        const messages = document.getElementById('chat-messages');
        messages.scrollTop = messages.scrollHeight;
    }

    finalizeOutputBlock() {
        this.currentOutputBlock = null;
    }

    bindEvents() {
        document.getElementById('add-machine-btn').addEventListener('click', () => this.promptAddMachine());
        document.getElementById('send-btn').addEventListener('click', () => this.sendMessage());
        document.getElementById('chat-input').addEventListener('keydown', (e) => {
            if (e.key === 'Enter' && !e.shiftKey) {
                e.preventDefault();
                this.sendMessage();
            }
        });
        document.getElementById('permission-mode').addEventListener('change', (e) => {
            this.permissionMode = e.target.checked;
        });
        document.getElementById('direct-llm-mode').addEventListener('change', (e) => {
            this.directLLMMode = e.target.checked;
        });
        document.getElementById('allow-btn').addEventListener('click', () => this.handlePermission(true));
        document.getElementById('deny-btn').addEventListener('click', () => this.handlePermission(false));
    }

    async loadMachines() {
        try {
            const response = await fetch('/api/terminal/machines');
            if (!response.ok) {
                throw new Error('获取机器列表失败');
            }
            const result = await response.json();
            this.machines = result.machines || [];
            this.renderMachineList();
        } catch (e) {
            console.error('Load machines error:', e);
            this.machines = [];
            this.renderMachineList();
        }
    }

    renderMachineList() {
        const listEl = document.getElementById('machine-list');
        listEl.innerHTML = '';

        this.machines.forEach((machine) => {
            const group = document.createElement('div');
            const activeClass = this.activeMachineIp === machine.ip ? ' is-active' : '';
            group.className = `machine-group${activeClass}`;
            group.innerHTML = `
                <div class="machine-row">
                    <span class="machine-cell machine-ip">${machine.ip}</span>
                    <span class="machine-cell machine-user">${machine.username}</span>
                    <span class="machine-cell machine-action-cell">
                        <button class="btn btn-connect" data-id="${machine.id}">连接</button>
                    </span>
                    <span class="machine-cell machine-action-cell">
                        <button class="btn btn-disconnect" data-id="${machine.id}">断开</button>
                    </span>
                    <span class="machine-cell machine-action-cell">
                        <button class="btn btn-delete" data-id="${machine.id}">删除</button>
                    </span>
                </div>
            `;
            listEl.appendChild(group);
        });

        listEl.querySelectorAll('.btn-connect').forEach((btn) => {
            btn.addEventListener('click', (e) => {
                const machineId = Number(e.target.dataset.id);
                this.connectMachine(machineId);
            });
        });

        listEl.querySelectorAll('.btn-disconnect').forEach((btn) => {
            btn.addEventListener('click', (e) => {
                const machineId = Number(e.target.dataset.id);
                this.disconnectMachine(machineId);
            });
        });

        listEl.querySelectorAll('.btn-delete').forEach((btn) => {
            btn.addEventListener('click', (e) => {
                const machineId = Number(e.target.dataset.id);
                this.deleteMachine(machineId);
            });
        });
    }

    async promptAddMachine() {
        const ip = window.prompt('请输入机器 IP 地址：');
        if (!ip) {
            return;
        }

        const username = window.prompt('请输入用户名：', 'root');
        if (!username) {
            return;
        }

        const password = window.prompt('请输入密码：');
        if (password === null) {
            return;
        }

        try {
            const response = await fetch('/api/terminal/machines', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ip: ip.trim(), username: username.trim(), password: password})
            });

            if (!response.ok) {
                let detail = '保存机器失败';
                try {
                    const err = await response.json();
                    detail = err.detail || detail;
                } catch (_) {
                    // ignore parse error
                }
                throw new Error(detail);
            }

            await this.loadMachines();
            this.addMessage(`已保存机器 ${ip.trim()}。`, 'assistant');
        } catch (e) {
            alert('保存失败: ' + e.message);
        }
    }

    connectMachine(machineId) {
        const machine = this.machines.find((item) => item.id === machineId);
        if (!machine) {
            return;
        }
        this.activeMachineIp = machine.ip;
        this.activeMachineId = machine.id;
        this.renderMachineList();
        this.startSession(machine);
    }

    async disconnectMachine(machineId) {
        const machine = this.machines.find((item) => item.id === machineId);
        if (!machine) {
            return;
        }

        if (this.activeMachineIp !== machine.ip) {
            return;
        }

        try {
            await fetch('/api/terminal/disconnect', {method: 'POST'});
        } catch (e) {
            console.error('Disconnect terminal error:', e);
        }

        this.sessionId = null;
        this.terminalConnected = false;
        this.activeMachineIp = '';
        this.activeMachineId = null;
        this.updateTerminalStatus('未连接', false);
        document.getElementById('terminal-iframe').src = 'about:blank';
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
        this.addMessage(`已断开远程机器 ${machine.ip}。`, 'assistant');
        this.renderMachineList();
    }


    async deleteMachine(machineId) {
        const machine = this.machines.find((item) => item.id === machineId);
        if (!machine) {
            return;
        }

        if (this.activeMachineIp === machine.ip) {
            await this.disconnectMachine(machineId);
        }

        try {
            const response = await fetch(`/api/terminal/machines/${machineId}`, {
                method: 'DELETE'
            });

            if (!response.ok) {
                let detail = '删除机器失败';
                try {
                    const err = await response.json();
                    detail = err.detail || detail;
                } catch (_) {
                    // ignore parse error
                }
                throw new Error(detail);
            }

            this.machines = this.machines.filter((item) => item.id !== machineId);
            this.renderMachineList();
            this.addMessage(`已删除远程机器 ${machine.ip}。`, 'assistant');
        } catch (e) {
            alert('删除失败: ' + e.message);
        }
    }

    stopExecution() {
        if (this.abortController) {
            this.abortController.abort();
            this.abortController = null;
        }
        this.isProcessing = false;
        this.updateProcessingState(false);
        this.addMessage('⏹️ 已停止执行', 'assistant');
    }

    updateProcessingState(processing) {
        this.isProcessing = processing;
        const sendBtn = document.getElementById('send-btn');
        const chatInput = document.getElementById('chat-input');

        sendBtn.disabled = processing;
        chatInput.disabled = processing;
    }

    async startSession(machine) {
        try {
            const configResponse = await fetch('/api/terminal/target');
            const config = await configResponse.json();
            if (!config.enabled) {
                throw new Error('终端功能未启用，请检查配置文件');
            }

            this.updateTerminalStatus('连接中...', false);
            this.showTerminalViewer(config);

            const sessionResponse = await fetch('/api/terminal/session', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({machine_id: machine.id})
            });
            if (!sessionResponse.ok) {
                let detail = '创建会话失败';
                try {
                    const err = await sessionResponse.json();
                    detail = err.detail || detail;
                } catch (_) {
                    // ignore parse error
                }
                throw new Error(detail);
            }
            const sessionData = await sessionResponse.json();
            this.sessionId = sessionData.session_id;
            console.log('Session created:', this.sessionId);

            this.terminalConnected = true;
            this.updateTerminalStatus('已连接', true);
            this.addMessage(`已连接远程机器 ${machine.ip}，现在可以开始执行任务。`, 'assistant');
            this.connectAIWebSocket();

        } catch (e) {
            this.activeMachineIp = '';
            this.activeMachineId = null;
            this.renderMachineList();
            this.updateTerminalStatus('连接失败', false);
            document.getElementById('terminal-iframe').src = 'about:blank';
            alert('连接失败: ' + e.message);
        }
    }

    showTerminalViewer(config) {
        const viewer = document.getElementById('terminal-iframe');
        const path = config.path && config.path.startsWith('/') ? config.path : '/' + (config.path || '');
        const host = config.host || window.location.hostname;
        viewer.src = config.protocol + '://' + host + ':' + config.port + path;
    }

    updateTerminalStatus(text, connected) {
        const status = document.getElementById('terminal-status');
        status.textContent = text;
        status.className = 'vnc-status' + (connected ? ' connected' : '');
    }

    async sendMessage() {
        const directLLM = this.directLLMMode;

        if (!this.terminalConnected && !directLLM) {
            alert('请先在右侧机器列表点击"连接"');
            return;
        }

        const input = document.getElementById('chat-input');
        const message = input.value.trim();

        if (!message) return;

        if (!directLLM && !this.activeMachineId) {
            alert('请先选择并连接一台机器');
            return;
        }

        this.addMessage(message, 'user');
        input.value = '';

        this.updateProcessingState(true);

        if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
            this.connectAIWebSocket();
            await new Promise((resolve) => {
                const check = setInterval(() => {
                    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
                        clearInterval(check);
                        resolve();
                    }
                }, 100);
                setTimeout(() => { clearInterval(check); resolve(); }, 5000);
            });
        }

        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify({
                type: directLLM ? 'llm_query' : 'agent_task',
                session_id: this.sessionId,
                machine_id: this.activeMachineId,
                message: message
            }));
        } else {
            this.addMessage('无法连接AI服务，请刷新页面重试', 'assistant');
            this.updateProcessingState(false);
        }
    }

    async processChat(message, signal) {
        try {
            const response = await fetch('/api/ai/chat', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({session_id: this.sessionId || 1, message: message}),
                signal: signal
            });

            if (!response.ok) {
                throw new Error('HTTP ' + response.status);
            }

            const result = await response.json();

            if (result.response) {
                this.addMessage(result.response, 'assistant');
            }

            if (result.tool_calls && result.tool_calls.length > 0) {
                await this.executeToolChain(result.tool_calls);
            }
            
            this.updateProcessingState(false);
        } catch (e) {
            if (e.name === 'AbortError') {
                console.log('Request aborted');
                return;
            }
            console.error('Chat error:', e);
            this.addMessage('抱歉，发生错误: ' + e.message, 'assistant');
        }
    }

    async executeToolChain(toolCalls) {
        for (const toolCall of toolCalls) {
            // 检查是否已停止
            if (!this.isProcessing) {
                return;
            }
            
            const toolResult = await this.executeTool(toolCall);
            if (!toolResult) {
                continue;
            }

            if (toolResult.data && toolResult.data.finished) {
                // 更新工具执行结果状态
                const resultEl = document.getElementById('result-' + toolResult.resultId);
                if (resultEl) {
                    resultEl.textContent = '完成';
                }
                this.addMessage('✅ 任务完成: ' + toolResult.data.summary, 'assistant');
                this.updateProcessingState(false);
                return;
            }

            if (toolResult.success) {
                const resultEl = document.getElementById('result-' + toolResult.resultId);
                console.log('Tool result:', toolResult);
                console.log('Looking for element: result-' + toolResult.resultId);
                console.log('Found element:', resultEl);
                if (resultEl) {
                    let resultText = toolResult.message || '成功';
                    resultEl.textContent = resultText;
                }
                if (toolResult.data && toolResult.data.screenshot) {
                    const analysis = toolResult.data.analysis || '';
                    this.addScreenshot(toolResult.data.screenshot, analysis);
                }
            } else {
                const resultEl = document.getElementById('result-' + toolResult.resultId);
                if (resultEl) {
                    resultEl.textContent = '失败: ' + (toolResult.error || toolResult.message);
                }
            }

            // 检查是否已停止
            if (!this.isProcessing) {
                return;
            }

            const continueResponse = await fetch('/api/ai/continue', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({
                    session_id: this.sessionId,
                    tool_name: toolCall.function.name,
                    tool_call_id: toolCall.id,
                    result: {
                        success: toolResult.success,
                        message: toolResult.message || '',
                        data: {
                            analysis: (toolResult.data?.analysis || "")
                        }
                    }
                })
            });

            if (!continueResponse.ok) {
                continue;
            }

            const continueData = await continueResponse.json();

            if (continueData.response) {
                this.addMessage(continueData.response, 'assistant');
            }

            if (!continueData.tool_calls || continueData.tool_calls.length === 0) {
                this.updateProcessingState(false);
                return;
            }

            await this.executeToolChain(continueData.tool_calls);
        }
        // 循环结束，更新状态
        this.updateProcessingState(false);
    }

    async executeTool(toolCall) {
        try {
            let args = {};
            try {
                if (toolCall.function.arguments) {
                    args = JSON.parse(toolCall.function.arguments);
                }
            } catch (e) {
                console.warn('Failed to parse arguments:', toolCall.function.arguments);
            }

            const desc = this.getToolDescription(toolCall.function.name, args);
            const resultId = this.addToolExecution(toolCall.function.name, desc, '执行中...');

            const response = await fetch('/api/ai/execute-tool', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({session_id: this.sessionId || 1, tool_call_id: toolCall.id, tool_name: toolCall.function.name, arguments: args})
            });

            if (!response.ok) {
                throw new Error('HTTP ' + response.status);
            }

            const result = await response.json();
            return { ...result, resultId: resultId };
        } catch (e) {
            console.error('Tool execution error:', e);
            this.addMessage('工具执行失败: ' + e.message, 'assistant');
            return null;
        }
    }

    getToolDescription(name, args) {
        const descriptions = {
            'take_screenshot': '截取屏幕并分析: ' + (args.prompt || '分析屏幕内容'),
            'click': '点击位置 (' + args.x + ', ' + args.y + ')',
            'double_click': '双击位置 (' + args.x + ', ' + args.y + ')',
            'right_click': '右键点击位置 (' + args.x + ', ' + args.y + ')',
            'move_cursor': '移动鼠标到 (' + args.x + ', ' + args.y + ')',
            'type_text': '输入文本: ' + args.text,
            'press_key': '按下按键: ' + args.key,
            'scroll': '滚动页面 ' + args.direction,
            'finish_session': '任务完成'
        };
        return descriptions[name] || '执行操作';
    }

    getToolDisplayName(name) {
        const displayNames = {
            'take_screenshot': '拍摄截屏',
            'click': '点击',
            'double_click': '双击',
            'right_click': '右键点击',
            'move_cursor': '移动鼠标',
            'type_text': '输入文本',
            'press_key': '按键',
            'press_key_combo': '组合键',
            'scroll': '滚动',
            'finish_session': '完成',
            'disk_usage': '磁盘巡检',
            'memory_usage': '内存巡检',
            'cpu_load': 'CPU 巡检',
            'network_status': '网络巡检',
            'process_status': '进程巡检',
            'generic_probe': '通用探测'
        };
        return displayNames[name] || name;
    }

    addMessage(content, role) {
        const messages = document.getElementById('chat-messages');
        const div = document.createElement('div');
        div.className = 'message ' + role;
        div.innerHTML = '<div class="message-content"><p>' + content + '</p></div>';
        messages.appendChild(div);
        messages.scrollTop = messages.scrollHeight;
    }

    addToolExecution(name, description, status) {
        const messages = document.getElementById('chat-messages');
        const div = document.createElement('div');
        div.className = 'message assistant';
        const uniqueId = name + '-' + Date.now();
        const displayName = this.getToolDisplayName(name);
        div.innerHTML = '<div class="tool-execution"><div class="tool-name">' + displayName + '</div><div>' + description + '</div><div class="tool-result" id="result-' + uniqueId + '">' + status + '</div></div>';
        messages.appendChild(div);
        messages.scrollTop = messages.scrollTop;
        return uniqueId;
    }

    updateToolResult(name, result) {
        const el = document.getElementById('result-' + name);
        if (el) el.textContent = result;
    }

    addScreenshot(screenshot, analysis = '') {
        const messages = document.getElementById('chat-messages');
        const div = document.createElement('div');
        div.className = 'message assistant';
        let analysisHtml = '';
        if (analysis) {
            const preview = analysis.length > 300 ? analysis.substring(0, 300) + '...' : analysis;
            analysisHtml = '<div style="margin-top:8px;font-size:13px;color:#666;line-height:1.5;">' + preview + '</div>';
        }
        div.innerHTML = '<div class="tool-execution"><div class="tool-name">截屏结果</div><img src="data:image/jpeg;base64,' + screenshot + '" style="max-width:100%;border-radius:4px;margin-top:8px;">' + analysisHtml + '</div>';
        messages.appendChild(div);
        messages.scrollTop = messages.scrollHeight;
    }

    async handlePermission(approved) {
        const panel = document.getElementById('permission-requests');
        panel.style.display = 'none';
    }
}

document.addEventListener('DOMContentLoaded', () => {
    window.app = new CheckPilotApp();
});
