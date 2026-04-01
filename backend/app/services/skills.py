from typing import Dict, List, Optional


class SkillDefinition:
    def __init__(
        self,
        name: str,
        description: str,
        command_template: str,
        keywords: List[str],
    ) -> None:
        self.name = name
        self.description = description
        self.command_template = command_template
        self.keywords = keywords

    def to_prompt_dict(self) -> Dict[str, str]:
        return {
            "name": self.name,
            "description": self.description,
            "command_template": self.command_template,
        }


SKILL_DEFINITIONS: List[SkillDefinition] = [
    SkillDefinition(
        name="disk_usage",
        description="查看磁盘空间、文件系统使用率和挂载情况",
        command_template="df -h",
        keywords=["磁盘", "磁盘使用率", "磁盘空间", "硬盘", "df", "挂载"],
    ),
    SkillDefinition(
        name="memory_usage",
        description="查看内存和交换分区使用情况",
        command_template="free -h",
        keywords=["内存", "memory", "swap", "free -h"],
    ),
    SkillDefinition(
        name="cpu_load",
        description="查看 CPU 负载、平均负载和当前最耗资源进程",
        command_template="uptime && echo '---' && ps -eo pid,ppid,cmd,%mem,%cpu --sort=-%cpu | head",
        keywords=["cpu", "负载", "load", "性能", "top"],
    ),
    SkillDefinition(
        name="network_status",
        description="查看网络地址、路由和网络连通状态",
        command_template="ip addr && echo '---' && ip route",
        keywords=["网络", "ip", "route", "路由", "网卡", "连通"],
    ),
    SkillDefinition(
        name="process_status",
        description="查看系统当前进程和资源占用情况",
        command_template="ps -ef | head -n 50",
        keywords=["进程", "process", "服务", "ps"],
    ),
]


def get_skill_catalog() -> List[Dict[str, str]]:
    return [skill.to_prompt_dict() for skill in SKILL_DEFINITIONS]


def match_skill_by_keywords(task_description: str) -> Optional[SkillDefinition]:
    text = task_description.lower()
    for skill in SKILL_DEFINITIONS:
        if any(keyword.lower() in text for keyword in skill.keywords):
            return skill
    return None
