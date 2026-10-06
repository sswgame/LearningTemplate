"""
생성 파일을 쓰는 자리 — configure · 빌드가 부르는 생성기(`Scripts/generate/`)가 같이 쓴다.

- **바뀌었을 때만 쓴다.** 같은 내용으로 다시 쓰면 파일 시각이 바뀌어 Ninja 가 그 헤더에 기대는 TU 를 다시 짓고(restat 이 막는 것은 같은 시각뿐이다),
  CMake 가 include 하는 파일이면 configure 가 다시 돈다. 비교는 읽은 글끼리(줄끝을 맞춘 뒤)다.
- **줄끝은 생성기가 고른다.** 기본은 플랫폼 것(텍스트 모드 — Windows CRLF), `newline="\\n"` 이면 어디서나 LF(바이트를 해시하는 도장 · 고지).
- **CMake 값은 이스케이프한다.** 윈도우 경로(`C:\\Program Files`)를 그대로 넣으면 CMake 가 `\\P` 를 이스케이프로 읽어 조용히 다른 경로가 된다.
- 진입점 모양 하나 — `runGenerator` 가 인자 파싱 · 오류 한 줄 · 종료 코드를 든다.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any, Callable, Sequence


def writeGeneratedFile(path: Path, content: str, *, tag: str, summary: str = "", newline: str | None = None,
                       bReportUnchanged: bool = True, bQuiet: bool = False) -> bool:
    """
    `content`(줄끝 `\\n`)가 지금 파일과 다를 때만 씁니다. 썼으면 True.

    한 줄 찍는다: `[tag] Wrote|Up to date: <경로> (summary)` — `bReportUnchanged` 가 거짓이면 쓴 때만, `bQuiet` 면 찍지 않는다.
    """
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    previous = path.read_text(encoding="utf-8") if path.is_file() else None
    bChanged = previous != content
    if bChanged:
        with path.open("w", encoding="utf-8", newline=newline) as file:
            file.write(content)
    if not bQuiet and (bChanged or bReportUnchanged):
        suffix = f" ({summary})" if summary else ""
        print(f"[{tag}] {'Wrote' if bChanged else 'Up to date'}: {path}{suffix}")
    return bChanged


def toCMakeValue(value: Any) -> str:
    """파이썬 값을 따옴표 안에 넣을 CMake 문자열로. bool → TRUE/FALSE, 집합(정렬) · 목록 → `;` 리스트, 역슬래시 · 따옴표 이스케이프."""
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, (set, frozenset)):
        return ";".join(toCMakeValue(item) for item in sorted(value))
    if isinstance(value, (list, tuple)):
        return ";".join(toCMakeValue(item) for item in value)
    return str(value).replace("\\", "\\\\").replace('"', '\\"')


def formatCMakeSet(name: str, value: Any) -> str:
    """`set(NAME "값")` 한 줄."""
    return f'set({name} "{toCMakeValue(value)}")'


class GeneratorError(Exception):
    """생성기가 입력(JSON · 상수)을 읽지 못했다 — 트레이스백 대신 한 줄로 찍고 1 로 끝난다(configure 가 무엇을 고칠지 먼저 보게)."""


def runGenerator(argv: Sequence[str] | None, *, tag: str, description: str,
                 addArguments: Callable[[argparse.ArgumentParser], None],
                 generate: Callable[[argparse.Namespace], None]) -> int:
    """생성기 진입점. `generate(args)` 가 `GeneratorError` 를 던지면 `[tag] 메시지` 한 줄 + 1. 인자가 틀리면 argparse 가 2."""
    parser = argparse.ArgumentParser(description=description)
    addArguments(parser)
    args = parser.parse_args(argv)
    try:
        generate(args)
    except GeneratorError as error:
        print(f"[{tag}] {error}", file=sys.stderr)
        return 1
    return 0
