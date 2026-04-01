import json
from pathlib import Path
from typing import Optional, List, Dict, Any
import aiosqlite
from datetime import datetime


DB_PATH = Path(__file__).parent.parent / "checkpilot.db"


async def init_db():
    db = await aiosqlite.connect(DB_PATH)
    await db.execute("""
        CREATE TABLE IF NOT EXISTS sessions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP,
            updated_at TEXT DEFAULT CURRENT_TIMESTAMP,
            vnc_host TEXT,
            vnc_port INTEGER,
            status TEXT DEFAULT 'disconnected',
            permission_mode INTEGER DEFAULT 0
        )
    """)
    await db.execute("""
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id INTEGER,
            role TEXT,
            content TEXT,
            reasoning_content TEXT,
            tool_calls TEXT,
            tool_call_id TEXT,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (session_id) REFERENCES sessions(id)
        )
    """)
    await db.execute("""
        CREATE TABLE IF NOT EXISTS tool_executions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            message_id INTEGER,
            tool_name TEXT,
            arguments TEXT,
            result TEXT,
            status TEXT,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (message_id) REFERENCES messages(id)
        )
    """)
    await db.execute("""
        CREATE TABLE IF NOT EXISTS machines (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            ip TEXT NOT NULL UNIQUE,
            username TEXT NOT NULL,
            password TEXT NOT NULL,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        )
    """)
    await db.commit()
    await db.close()


async def get_session(session_id: int) -> Optional[Dict[str, Any]]:
    db = await aiosqlite.connect(DB_PATH)
    cursor = await db.execute("SELECT * FROM sessions WHERE id = ?", (session_id,))
    row = await cursor.fetchone()
    await db.close()
    if row:
        return {
            "id": row[0],
            "created_at": row[1],
            "updated_at": row[2],
            "vnc_host": row[3],
            "vnc_port": row[4],
            "status": row[5],
            "permission_mode": bool(row[6]),
        }
    return None


async def create_session(vnc_host: str, vnc_port: int) -> int:
    db = await aiosqlite.connect(DB_PATH)
    cursor = await db.execute(
        "INSERT INTO sessions (vnc_host, vnc_port, status) VALUES (?, ?, 'disconnected')",
        (vnc_host, vnc_port),
    )
    await db.commit()
    session_id = cursor.lastrowid
    await db.close()
    return session_id


async def update_session_status(session_id: int, status: str):
    db = await aiosqlite.connect(DB_PATH)
    await db.execute(
        "UPDATE sessions SET status = ?, updated_at = ? WHERE id = ?",
        (status, datetime.now().isoformat(), session_id),
    )
    await db.commit()
    await db.close()


async def set_permission_mode(session_id: int, enabled: bool):
    db = await aiosqlite.connect(DB_PATH)
    await db.execute(
        "UPDATE sessions SET permission_mode = ? WHERE id = ?",
        (int(enabled), session_id),
    )
    await db.commit()
    await db.close()


async def add_message(
    session_id: int,
    role: str,
    content: str,
    tool_calls: Optional[str] = None,
    reasoning_content: Optional[str] = None,
    tool_call_id: Optional[str] = None,
) -> int:
    db = await aiosqlite.connect(DB_PATH)
    cursor = await db.execute(
        "INSERT INTO messages (session_id, role, content, tool_calls, reasoning_content, tool_call_id) VALUES (?, ?, ?, ?, ?, ?)",
        (session_id, role, content, tool_calls, reasoning_content, tool_call_id),
    )
    await db.commit()
    msg_id = cursor.lastrowid
    await db.close()
    return msg_id


async def get_messages(session_id: int, limit: int = 20) -> List[Dict[str, Any]]:
    db = await aiosqlite.connect(DB_PATH)
    cursor = await db.execute(
        "SELECT * FROM messages WHERE session_id = ? ORDER BY created_at DESC LIMIT ?",
        (session_id, limit),
    )
    rows = await cursor.fetchall()
    await db.close()
    return [
        {
            "id": row[0],
            "session_id": row[1],
            "role": row[2],
            "content": row[3],
            "reasoning_content": row[4],
            "tool_calls": json.loads(row[5]) if row[5] else None,
            "tool_call_id": row[6],
            "created_at": row[7],
        }
        for row in rows
    ][::-1]


async def add_tool_execution(
    message_id: int, tool_name: str, arguments: str, result: str, status: str
) -> int:
    db = await aiosqlite.connect(DB_PATH)
    cursor = await db.execute(
        "INSERT INTO tool_executions (message_id, tool_name, arguments, result, status) VALUES (?, ?, ?, ?, ?)",
        (message_id, tool_name, arguments, result, status),
    )
    await db.commit()
    exec_id = cursor.lastrowid
    await db.close()
    return exec_id


async def get_tool_executions(message_id: int) -> List[Dict[str, Any]]:
    db = await aiosqlite.connect(DB_PATH)
    cursor = await db.execute(
        "SELECT * FROM tool_executions WHERE message_id = ? ORDER BY created_at",
        (message_id,),
    )
    rows = await cursor.fetchall()
    await db.close()
    return [
        {
            "id": row[0],
            "message_id": row[1],
            "tool_name": row[2],
            "arguments": json.loads(row[3]) if row[3] else {},
            "result": row[4],
            "status": row[5],
            "created_at": row[6],
        }
        for row in rows
    ]


async def create_machine(ip: str, username: str, password: str) -> int:
    db = await aiosqlite.connect(DB_PATH)
    cursor = await db.execute(
        """
        INSERT INTO machines (ip, username, password)
        VALUES (?, ?, ?)
        ON CONFLICT(ip) DO UPDATE SET
            username = excluded.username,
            password = excluded.password
        """,
        (ip, username, password),
    )
    await db.commit()
    machine_id = cursor.lastrowid
    if not machine_id:
        query_cursor = await db.execute("SELECT id FROM machines WHERE ip = ?", (ip,))
        row = await query_cursor.fetchone()
        machine_id = row[0] if row else 0
    await db.close()
    return machine_id


async def list_machines() -> List[Dict[str, Any]]:
    db = await aiosqlite.connect(DB_PATH)
    cursor = await db.execute(
        "SELECT id, ip, username, password, created_at FROM machines ORDER BY id DESC"
    )
    rows = await cursor.fetchall()
    await db.close()
    return [
        {
            "id": row[0],
            "ip": row[1],
            "username": row[2],
            "password": row[3],
            "created_at": row[4],
        }
        for row in rows
    ]


async def get_machine(machine_id: int) -> Optional[Dict[str, Any]]:
    db = await aiosqlite.connect(DB_PATH)
    cursor = await db.execute(
        "SELECT id, ip, username, password, created_at FROM machines WHERE id = ?",
        (machine_id,),
    )
    row = await cursor.fetchone()
    await db.close()
    if not row:
        return None

    return {
        "id": row[0],
        "ip": row[1],
        "username": row[2],
        "password": row[3],
        "created_at": row[4],
    }


async def delete_machine(machine_id: int) -> bool:
    db = await aiosqlite.connect(DB_PATH)
    cursor = await db.execute("DELETE FROM machines WHERE id = ?", (machine_id,))
    await db.commit()
    deleted = cursor.rowcount > 0
    await db.close()
    return deleted
