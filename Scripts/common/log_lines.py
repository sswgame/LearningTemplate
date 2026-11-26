"""
App · Server 로그 줄의 읽기 하나 — 줄 모양 `[시각] [태그] [수준] … - [id:영역.사건] 본문`(C++ 의 정본 `Core/Log/Sink/AsyncLogSink` · `LogIDTag` · `ParsedLogLine`).

판정은 로그 id 로만 한다. 본문 글은 사람이 읽는 것이라 문구 · 수 표기가 바뀐다(좌표가 `-1.4305115e-06` 으로 찍히자 판정 정규식이 졌다).
값 판정은 시나리오의 `<Expect>` · `<Record>` (보고 JSON)이고, 로그로 값을 나를 수밖에 없는 자리는 본문이 JSON 인 줄(`payloads`)이다.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator

_LEVEL = re.compile(r"\[(Error|Warning|Info|Trace)\] (.*)$")  # 수준 앞(시각 · 태그)은 보지 않는다 — 로거가 서기 전의 stderr 단언 줄도 `[Error] [id:…] …` 다
_ID = re.compile(r"\[id:([^\]]+)\] (.*)$")


@dataclass(frozen=True)
class LogLine:
    level: str
    id: str       # 없으면 빈 글
    message: str

    @property
    def is_problem(self) -> bool:
        return self.level in ("Error", "Warning")


def read_log(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def parse_log(text: str) -> Iterator[LogLine]:
    """로그 줄을 차례로 — 여러 줄 본문의 이어진 줄과 ` -> 파일:줄` 은 로그 줄이 아니라 건너뛴다."""
    for raw in text.splitlines():
        level = _LEVEL.search(raw)
        if level is None:
            continue
        tagged = _ID.search(level.group(2))
        if tagged is not None:
            yield LogLine(level.group(1), tagged.group(1), tagged.group(2))
        else:
            yield LogLine(level.group(1), "", level.group(2).partition("- ")[2] or level.group(2))


def count_id(text: str, log_id: str) -> int:
    """id 가 @p log_id 인 줄 수."""
    return sum(line.id == log_id for line in parse_log(text))


def payloads(text: str, log_id: str) -> list[dict]:
    """id 가 @p log_id 인 줄의 JSON 본문들. JSON 이 아니면 ValueError — 값을 글로 나르는 줄로 바뀌었다는 뜻이다."""
    return [json.loads(line.message) for line in parse_log(text) if line.id == log_id]


def problem_lines(name: str, text: str, allowed_ids: frozenset[str] = frozenset()) -> list[str]:
    """@p text 의 Error · Warning 줄(id 가 @p allowed_ids 인 줄은 뺀다)을 `<name> log: [수준] [id] <본문>` 으로."""
    return [f"{name} log: [{line.level}] {line.id or '(no id)'}: {line.message}" for line in parse_log(text) if line.is_problem and line.id not in allowed_ids]
