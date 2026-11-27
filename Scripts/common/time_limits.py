"""
Scripts/common/time_limits.py

`Config/Dev/TimeLimits.json`(자동화 · 검증 시한의 정본) 읽기 — C++ 상수 헤더 생성기(`generate/time_limits.py`)와 세션 · 검증 스크립트가 함께 쓴다.
칸이 없거나 음수가 아닌 수가 아니면 멈춘다(폴백 없음).
"""

from __future__ import annotations

import json
from pathlib import Path

from .paths import get_project_root

TIME_LIMITS_PATH = Path("Config") / "Dev" / "TimeLimits.json"
SECTIONS = ("_scenario", "_devTools", "_scripts")


def read_time_limit_sections(project_root: Path | None = None) -> dict[str, dict[str, int | float]]:
    path = (project_root or get_project_root()) / TIME_LIMITS_PATH
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exception:
        raise SystemExit(f"[TimeLimits] {path} 를 읽지 못했다 ({exception})") from exception
    sections: dict[str, dict[str, int | float]] = {}
    for section in SECTIONS:
        rows = data.get(section) if isinstance(data, dict) else None
        if not isinstance(rows, dict) or not rows:
            raise SystemExit(f"[TimeLimits] {TIME_LIMITS_PATH} 의 {section} 가 비었거나 객체가 아니다")
        for key, value in rows.items():
            if isinstance(value, bool) or not isinstance(value, (int, float)) or value < 0:
                raise SystemExit(f"[TimeLimits] {TIME_LIMITS_PATH} 의 {section}.{key} 가 0 이상의 수가 아니다")
        sections[section] = rows
    return sections


def read_time_limit(section: str, key: str, project_root: Path | None = None) -> float:
    value = read_time_limit_sections(project_root)[section].get(key)
    if value is None:
        raise SystemExit(f"[TimeLimits] {TIME_LIMITS_PATH} 에 {section}.{key} 가 없다")
    return float(value)


def read_script_time_limit(key: str) -> float:
    """`_scripts` 칸 하나(초)."""
    return read_time_limit("_scripts", key)


def get_chain_kill_seconds(stage_count: int, project_root: Path | None = None) -> float:
    """단계가 @p stage_count 개인 시나리오 체인의 프로세스 시한(초) — C++ `AutomationTimeLimits::getChainKillSeconds` 와 같은 식이다."""
    scenario = read_time_limit_sections(project_root)["_scenario"]
    return float(stage_count * scenario["_chainStageSeconds"] + scenario["_processKillSeconds"])
