"""
Scripts/common/verify_config.py

`Config/Dev/Verify.json`(검증 · 테스트 도구의 정책 값) 읽기 한 곳 — preflight · 테스트 기록 · 바뀐 테스트 고르기 · 린트 · 세션 스크립트가 함께 쓴다.
값이 없거나 꼴이 다르면 멈춘다(폴백 없음).
"""

from __future__ import annotations

import json
from pathlib import Path

from .paths import get_project_root

VERIFY_CONFIG_PATH = Path("Config") / "Dev" / "Verify.json"


def _read_verify_config(project_root: Path) -> dict:
    path = project_root / VERIFY_CONFIG_PATH
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exception:
        raise SystemExit(f"[Verify.json] {path} 를 읽지 못했다 ({exception})") from exception
    if not isinstance(data, dict):
        raise SystemExit(f"[Verify.json] {path} 는 JSON 객체여야 한다")
    return data


def read_verify_number(key: str, project_root: Path | None = None) -> float:
    value = _read_verify_config(project_root or get_project_root()).get(key)
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise SystemExit(f"[Verify.json] {VERIFY_CONFIG_PATH} 의 {key} 가 수가 아니다")
    return float(value)


def read_verify_list(key: str, project_root: Path | None = None) -> list[str]:
    value = _read_verify_config(project_root or get_project_root()).get(key)
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise SystemExit(f"[Verify.json] {VERIFY_CONFIG_PATH} 의 {key} 가 글 목록이 아니다")
    return value
