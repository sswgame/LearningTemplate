#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/ServerUIIncludes.py

전용 서버도 짓는 게임 코드(GameFramework 공유 모듈 · 공유 키트 · 게임)가 UI 헤더를 include 하는지 본다.

**왜 린트인가.** 전용 서버는 `Engine/UI` 와 클라이언트 전용 소스(`…/Client/…`)를 짓지 않는다(`SW_UNBUILT_ENGINE_FOLDERS` · `SW_CLIENT_ONLY_SOURCE_PATTERNS`).
서버 구성의 include 게이트(`TargetLayers.toml` 의 `serverbuild`)가 `Engine/UI` 헤더를 막지만, 같은 DLL(GameFramework · 게임 모듈) 안의 `Client/` 폴더는
타깃 단위 게이트가 가르지 못하고 클라이언트 구성에서는 둘 다 컴파일된다 — 서버를 짓기 전에 커밋에서 잡으려고 둔다. 게임 코드는 UI 대신 창구
(`IInputCaptureQuery` · `IPlayerNoticeSink` · `ISubtitleSink` · `IUILayerVisibility` · `IHUDViewModelBinder` · `IDamageNumberSink` · `IGameInstanceClientHook`)를 쓴다.

볼 곳: `Source/GameFramework/**`(에디터 플러그인 · `Client/` 폴더 제외), `Source/Games/**`. 클라이언트 전용 소스(`…/Client/…`)의 헤더도 같은 까닭으로 막는다 — 서버는 그 소스를 짓지 않는다.
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
kUIInclude = re.compile(r'^\s*#\s*include\s+"(Engine/UI/[^"]+|[^"]*/Client/[^"]+)"', re.M)
kScanRoots = ("Source/GameFramework/", "Source/Games/")
kSkipParts = ("/Plugin/Editor/", "/Client/", "/Editor/")  # 테스트도 서버 구성(Dev)이 짓는다 — 보지 않을 까닭이 없다


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
    assert len(findProblems([("Source/Games/X/A.cpp", '#include "Engine/UI/Widgets/TextWidget.h"\n')], table)) == 1, "게임"
    assert len(findProblems([("Source/GameFramework/Runtime/X/Tests/A.cpp", '#include "GameFramework/Client/HUD/HUDControllerComponent.h"\n')], table)) == 1, "테스트 · 클라이언트 헤더"
    clean = [("Source/GameFramework/Runtime/UI/A.cpp", bad), ("Source/GameFramework/Kits/Feature/Online/Chat/Client/A.cpp", bad),
             ("Source/GameFramework/Plugin/Editor/X/A.cpp", bad),
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
