#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/GameGPUIncludes.py

게임 코드(GameFramework 공유 모듈 · 키트 · 게임)가 GPU 칸 배치 헤더(`Engine/Graphics/RHI` · `Engine/Graphics/Shader` — 정점 · 인스턴스 · 빛 종류 칸)를
**직접** include 하는지 본다. 게임은 게임 쪽 형식(`MeshBuildVertex` · `InstanceHighlight` · `LightKind`)을 쓰고 GPU 칸으로 옮기는 일은 엔진 · 렌더러가 한다
(`refactor_layers.md` 5 · 16 번).

**왜 린트인가.** 타깃 게이트의 `#error` 껍데기는 전이 include 도 막는데, 게임이 쓰는 엔진 공개 헤더(`MeshComponent.h` · `Texture2D.h` · `Engine.h`)가
RHI 헤더를 include 한다 — 게이트로 막으면 그 엔진 헤더까지 막힌다. 직접 include 만 가르는 것은 빌드가 하지 못한다.
서버 구성이 짓지 않는 것(오디오 · 렌더러 · 런타임 UI · `Client/` 폴더)은 여기서 보지 않는다 — 서버 게이트(`TargetLayers.toml` 의 `serverbuild` · `[gatefolder]`)가 컴파일에서 막는다.

볼 곳: `Source/GameFramework/**` · `Source/Games/**`(에디터 · `Client/` 폴더 제외). 예외 표는 없다.
`py -3 -m Scripts lint game-gpu-includes [--self-check]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402

kExtensions = (".h", ".cpp", ".inl")
kGPUInclude = re.compile(r'^\s*#\s*include\s+"(Engine/Graphics/(?:RHI|Shader)/[^"]+)"', re.M)
kScanRoots = ("Source/GameFramework/", "Source/Games/")
kSkipParts = ("/Plugin/Editor/", "/Client/", "/Editor/")


def isScanned(path: str) -> bool:
    return path.startswith(kScanRoots) and not any(part in path for part in kSkipParts)


def findProblems(listSource: Iterable[tuple[str, str]]) -> list[str]:
    listProblem: list[str] = []
    for path, text in listSource:
        if not isScanned(path):
            continue
        for match in kGPUInclude.finditer(text):
            listProblem.append(f"{path}: 게임 코드가 GPU 칸 배치 헤더 `{match.group(1)}` 를 직접 include 한다 — 게임 쪽 형식(MeshBuildVertex · InstanceHighlight · LightKind)을 쓰고 GPU 칸은 엔진 · 렌더러가 채운다")
    return listProblem


def readSources(projectRoot: Path) -> list[tuple[str, str]]:
    listSource: list[tuple[str, str]] = []
    for root in kScanRoots:
        for path in (projectRoot / root).rglob("*"):
            if path.suffix in kExtensions:
                listSource.append((path.relative_to(projectRoot).as_posix(), path.read_text(encoding="utf-8")))
    return listSource


def checkGameGPUIncludes(projectRoot: Path) -> list[str]:
    return findProblems(readSources(projectRoot))


def runSelfCheck() -> None:
    assert len(findProblems([("Source/Games/X/A.cpp", '#include "Engine/Graphics/RHI/RHITypes.h"\n')])) == 1, "게임 → RHI"
    assert len(findProblems([("Source/GameFramework/Runtime/X/Tests/A.cpp", '#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"\n')])) == 1, "테스트 → 셰이더"
    clean = [("Source/Games/X/Client/A.cpp", '#include "Engine/Graphics/RHI/RHITypes.h"\n'),
             ("Source/GameFramework/Plugin/Editor/X/A.cpp", '#include "Engine/Graphics/RHI/RHITypes.h"\n'),
             ("Source/Games/X/A.cpp", '#include "Engine/Visual/Component/MeshComponent.h"\n'),
             ("Source/Engine/X/A.cpp", '#include "Engine/Graphics/RHI/RHITypes.h"\n')]
    assert findProblems(clean) == [], "클라이언트 · 에디터 · 엔진 공개 헤더 · 엔진은 통과한다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="게임 코드가 GPU 칸 배치 헤더(Engine/Graphics/RHI · Shader)를 직접 include 하는지 본다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[game-gpu-includes] self-check OK")
        return ExitCode.Passed
    listProblem = checkGameGPUIncludes(getProjectRoot())
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[game-gpu-includes] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
