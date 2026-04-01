# AGENTS.md - CheckPilot Development Guide

This document provides guidelines for AI agents working on the CheckPilot codebase.

## Project Overview

CheckPilot is a VNC-based remote testing platform with AI assistance. It consists of:
- **Backend**: Python/FastAPI server with AI integration (OpenAI GPT models)
- **Frontend**: HTML/JavaScript with noVNC for VNC viewing
- **VNC Control**: vncdotool for programmatic control

## Build/Lint/Test Commands

### Package Management (uv)

```bash
# Install dependencies
uv sync

# Add a new dependency
uv add <package_name>

# Remove a dependency
uv remove <package_name>

# Run a Python script with dependencies
uv run python3 <script.py>

# Activate virtual environment
source .venv/bin/activate
```

### Running the Server

```bash
cd backend
uv run uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
```

### Running a Single Test

```bash
uv run python3 -m pytest <test_file.py>::<test_function> -v
uv run python3 -c "import asyncio; from pathlib import Path; exec(open('test_file.py').read())"
```

### Linting (ruff)

```bash
# Check linting
uv run ruff check .

# Auto-fix
uv run ruff check --fix .
```

## Code Style Guidelines

### Python Style

**Imports**:
- Standard library imports first, then third-party, then local
- Use `from typing import ...` for type hints
- Avoid wildcard imports (`from x import *`)

```python
import asyncio
import json
from pathlib import Path
from typing import Dict, Any, Optional

import aiosqlite
from pydantic import BaseModel

from app.config import get_config
from app.services.vnc_client import vnc_manager
```

**Naming Conventions**:
- `snake_case` for variables, functions, methods
- `PascalCase` for classes
- `UPPER_SNAKE_CASE` for constants
- Private methods prefixed with `_` (e.g., `_internal_method`)

**Type Hints**:
- Use type hints for function parameters and return values
- Prefer `Optional[X]` over `X | None`
- Use `Dict[K, V]` for dict types

```python
async def execute_tool(
    tool_name: str,
    arguments: Dict[str, Any],
    quality: int = 85
) -> Dict[str, Any]:
    """Execute a tool and return the result."""
    result: Dict[str, Any] = {"success": False}
    return result
```

**Error Handling**:
- Use try/except with specific exception types
- Log errors with descriptive messages
- Return structured error results for tool execution

```python
try:
    result = await some_async_operation()
except Exception as e:
    print(f"[Module] Operation failed: {e}")
    return {"success": False, "error": str(e)}
```

**Async/Await**:
- Use `async def` for functions that await
- Use `asyncio.to_thread()` for CPU-bound operations
- Never block the event loop with synchronous calls

**File Organization**:
- API routes in `app/api/`
- Business logic in `app/services/`
- Models/schemas in `app/models/`
- Database operations in `app/database.py`

### Frontend (JavaScript)

**Style**:
- ES6+ syntax (const, arrow functions, template literals)
- Chinese comments and UI text
- Console logging for debugging

```javascript
class CheckPilotApp {
    constructor() {
        this.sessionId = null;
        this.isProcessing = false;
    }

    async sendMessage() {
        if (!this.sessionId) return;
        const response = await fetch('/api/ai/chat', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({session_id: this.sessionId, message: text})
        });
    }
}
```

## Key Architecture Patterns

### Tool Execution Flow

```
Chat Model → tool_calls → /api/ai/execute-tool → execute_tool()
                                              ↓
                                      vnc_manager operations
                                              ↓
                                      return result
                                              ↓
                                /api/ai/continue → Chat Model
```

### VNC Operations

- `vnc_manager` is a singleton in `app/services/vnc_client.py`
- All VNC operations go through `asyncio.to_thread()` to avoid blocking
- Connection is established when session starts (`/api/vnc/session`)

### Database

- Uses `aiosqlite` for async SQLite operations
- Database file: `backend/checkpilot.db`
- Tables: `sessions`, `messages`, `tool_executions`

### Coordinate System

- Vision model returns normalized coordinates (0-1000)
- `execute_tool()` converts to actual pixel coordinates
- Formula: `actual_x = norm_x * screen_width / 1000`

## Important Rules

1. **Never send screenshot base64 to chat model** - Only use for frontend preview and vision model analysis
2. **Database should not store screenshots** - Keep only text metadata
3. **Use asyncio.to_thread()** for all synchronous VNC operations
4. **Disable problematic keys** - F1-F12, ESC, Insert, PrintScreen, PageUp/Down, Pause/Break trigger Gnome notifications
5. **Use shift+enter for login** - Not just enter key
6. **Chinese UI text** - All user-facing text should be in Chinese

## Configuration

- Main config: `backend/app/config.py` (Pydantic models)
- Runtime config: `config.yaml`
- Environment variables supported via `python-dotenv`

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/vnc/target` | GET | Get VNC connection target |
| `/api/vnc/session` | POST | Create session + connect VNC |
| `/api/vnc/ws` | WS | VNC WebSocket proxy |
| `/api/ai/chat` | POST | Initial chat message |
| `/api/ai/execute-tool` | POST | Execute a tool |
| `/api/ai/continue` | POST | Continue after tool result |
