"""
빌드 폴더(`build/<프리셋>`) — 프리셋 이름 표(`CMakePresets.json` 이 정본)와 그 폴더의 `Bin` 자리.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

from common.Paths import getProjectRoot

_kConfigPresetRe = re.compile(r"Ninja-([A-Z][a-z]+)")
_kServerPresetRe = re.compile(r"Ninja-([A-Z][a-z]+)-Server")


def readPresetsByPattern(repositoryRoot: Path, pattern: re.Pattern[str], description: str) -> dict[str, str]:
    """`CMakePresets.json` 의 configure 프리셋 이름 중 @p pattern 에 전부 맞는 것의 (첫 묶음 → 이름) 표. 하나도 없으면 `sys.exit`."""
    listPreset = json.loads((repositoryRoot / "CMakePresets.json").read_text(encoding="utf-8"))["configurePresets"]
    mapPreset = {match.group(1): preset["name"] for preset in listPreset if (match := pattern.fullmatch(preset["name"])) is not None}
    if not mapPreset:
        sys.exit(f"{repositoryRoot / 'CMakePresets.json'}: `{description}` 모양의 configure 프리셋을 찾지 못했다 — 프리셋 이름이 바뀌었나")
    return mapPreset


#: 구성 이름 → 프리셋 표(Debug · Release · Shipping). 프리셋 이름을 코드에 적는 도구는 이 표에서 구성 이름으로 고른다.
kMapConfigPreset: dict[str, str] = readPresetsByPattern(getProjectRoot(), _kConfigPresetRe, "Ninja-<구성>")
#: 구성 이름 → 전용 서버 프리셋 표(Debug · Shipping). `Ninja-<구성>-Server` 변형이라 구성 표와 따로 읽는다 — 섞으면 변형 이름이 구성 표를 오염시킨다.
kMapServerPreset: dict[str, str] = readPresetsByPattern(getProjectRoot(), _kServerPresetRe, "Ninja-<구성>-Server")


class BuildTree:
    """빌드 폴더 하나. 만들기만 해서는 디스크를 보지 않는다."""

    def __init__(self, path: Path) -> None:
        self.path = Path(path).resolve()

    @classmethod
    def fromPreset(cls, preset: str, repositoryRoot: Path | None = None) -> BuildTree:
        return cls((repositoryRoot or getProjectRoot()) / "build" / preset)

    @property
    def binDir(self) -> Path:
        """실행 파일의 작업 폴더."""
        return self.path / "Bin"
