#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/ServerUIIncludes.py

전용 서버도 짓는 게임 코드(GameFramework 공유 모듈 · 공유 키트 · 게임)가 UI 헤더를 include 하는지 본다.

**왜 린트인가.** 서버에서 UI 폴더를 빼려면(`refactor_layers.md` 층 7 · 11) 서버 대상 TU 가 `Engine/UI` · `GameFramework/Runtime/UI` 를 보지 않아야 한다.
include 게이트(`TargetLayers.toml` 의 `#error` 껍데기)는 타깃 단위라 한 DLL(GameFramework) 안의 폴더를 가르지 못한다 — 클라이언트 전용 모듈
(`GF_Client_<X>`)로 옮기기 전까지는 이 린트가 새 자리를 막는다. 게임 코드는 UI 대신 창구(`IInputCaptureQuery` · `IPlayerNoticeSink` · `IUILayerVisibility`)를 쓴다.

볼 곳: `Source/GameFramework/**`(에디터 플러그인 · `Client/` 폴더 · 테스트 제외), `Source/Games/**`.
아직 옮기지 않은 파일은 `Config/Dev/ServerUIIncludes.json` 의 `_pendingMove` 에 까닭과 함께 있다 — 옮기면 줄을 지운다(남은 줄이 곧 남은 일이다).

`py -3 -m Scripts server-ui [--self-check]` 로 돌리고, 커밋 훅(`PreCommit.py`)이 돌린다.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402

kConfigPath = "Config/Dev/ServerUIIncludes.json"
kExtensions = (".h", ".cpp", ".inl")
kUIInclude = re.compile(r'^\s*#\s*include\s+"((?:Engine/UI|GameFramework/Runtime/UI)/[^"]+)"', re.M)
kScanRoots = ("Source/GameFramework/", "Source/Games/")
kSkipParts = ("/Plugin/Editor/", "/Client/", "/Tests/", "/Editor/")


def isScanned(path: str) -> bool:
    return path.startswith(kScanRoots) and not any(part in path for part in kSkipParts)


def findProblems(listSource: Iterable[tuple[str, str]], table: dict) -> list[str]:
    pending = table["_pendingMove"]
    listProblem: list[str] = []
    for path, text in listSource:
        if not isScanned(path) or any(path.startswith(prefix) for prefix in pending):
            continue
        for match in kUIInclude.finditer(text):
            listProblem.append(f"{path}: 서버도 짓는 게임 코드가 UI 헤더 `{match.group(1)}` 를 include 한다 — 창구(IInputCaptureQuery · IPlayerNoticeSink · IUILayerVisibility)를 쓰거나 클라이언트 전용 모듈(`Client/`)로 옮긴다")
    return listProblem


def findStalePending(projectRoot: Path, table: dict) -> list[str]:
    """옮겨서 더는 없는 자리를 표에서 지우게 한다(남은 일 목록이 낡지 않게)."""
    return [f"{kConfigPath}: `_pendingMove` 의 '{prefix}' 가 저장소에 없다 — 줄을 지운다"
            for prefix in table["_pendingMove"] if not (projectRoot / prefix).exists()]


def readSources(projectRoot: Path) -> list[tuple[str, str]]:
    listSource: list[tuple[str, str]] = []
    for root in kScanRoots:
        for path in (projectRoot / root).rglob("*"):
            if path.suffix in kExtensions and "graphify-out" not in path.parts:
                try:
                    listSource.append((path.relative_to(projectRoot).as_posix(), path.read_text(encoding="utf-8")))
                except (UnicodeDecodeError, OSError):
                    continue
    return listSource


def checkServerUIIncludes(projectRoot: Path) -> list[str]:
    table = json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))
    return findProblems(readSources(projectRoot), table) + findStalePending(projectRoot, table)


def runSelfCheck() -> None:
    table = {"_pendingMove": {"Source/GameFramework/Runtime/UI/": "이유"}}
    bad = '#include "Engine/UI/UISystem.h"\n'
    assert len(findProblems([("Source/GameFramework/Runtime/Character/A.cpp", bad)], table)) == 1, "공유 런타임"
    assert len(findProblems([("Source/Games/X/A.cpp", '#include "GameFramework/Runtime/UI/HUD/HUDControllerComponent.h"\n')], table)) == 1, "게임"
    clean = [("Source/GameFramework/Runtime/UI/A.cpp", bad), ("Source/GameFramework/Kits/Feature/Online/Chat/Client/A.cpp", bad),
             ("Source/GameFramework/Plugin/Editor/X/A.cpp", bad), ("Source/GameFramework/Runtime/X/Tests/A.cpp", bad),
             ("Source/GameFramework/Runtime/X/A.cpp", '#include "Engine/Presentation/UILayer.h"\n')]
    assert findProblems(clean, table) == [], "대기 · 클라이언트 · 에디터 · 테스트 · 창구는 통과한다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="서버도 짓는 게임 코드가 UI 헤더를 include 하는지 본다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[ServerUIIncludes] self-check OK")
        return ExitCode.Passed
    listProblem = checkServerUIIncludes(getProjectRoot())
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[ServerUIIncludes] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
