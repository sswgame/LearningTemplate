#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
빌드된 `App.exe` 를 찾고 헤드리스로 셰이더를 쿠킹하는 자리.

**후보 목록은 여기 한 자리다**(쿠커 `CookAssets` 와 커밋 훅 `PreCommitLint` 가 같이 쓴다). 목록이 둘이면 갈라진다 —
한쪽만 Ninja-Shipping 을 빼먹으면 **Shipping 만 빌드해 둔 사람은** 셰이더를 고쳐 커밋할 때 검증을 건너뛴다.
Shipping 이 후보에 있는 이유: Shipping App 도 쿠커를 링크하므로(로그만 안 남는다) Dev 빌드 없이도 스스로 다시 쿠킹한다.

**찾는 순서.** 부르는 쪽이 경로를 알면(CMake 는 `$<TARGET_FILE:App>` 을 안다) 그것을 쓴다 — 빌드 폴더를
뒤지는 것은 사람이 손으로 스크립트를 돌릴 때의 편의다. 후보에는 리눅스 이름(확장자 없는 `App`)과
CI 프리셋 폴더도 있다 — `App.exe` 만 찾으면 리눅스 CI 가 씬 쿠킹에서 "App 을 먼저 빌드하세요" 로 죽는다.
"""

from __future__ import annotations

import subprocess
from pathlib import Path

#: App 이 있을 수 있는 빌드 폴더 — 앞에서부터 본다 (저장소 루트 기준).
#: Shipping App 도 쿠커·쿠커를 링크한다(로그만 안 남는다). 두 번째 Shipping 빌드부터는
#: 그 경로가 살아 있어서 Dev 빌드 없이도 스스로 다시 쿠킹한다.
kAppBuildBinDir: tuple[str, ...] = (
    "build/Ninja-Debug/Bin",
    "build/Ninja-Release/Bin",
    "build/Ninja-Shipping/Bin",
    "build/CI-Debug/Bin",
    "build/CI-Shipping/Bin",
    "build/WSL-Debug/Bin",
    "build/WSL-Release/Bin",
    "build/WSL-Shipping/Bin",
    "Bin",
)

#: 플랫폼별 실행 파일 이름. 리눅스는 확장자가 없다.
kAppExecutableName: tuple[str, ...] = ( "App.exe", "App" )

#: 빌드된 App 을 찾는 자리 — 폴더 × 이름, 앞에서부터.
kAppExecutableRelPath: tuple[str, ...] = tuple(
    f"{binDir}/{name}" for binDir in kAppBuildBinDir for name in kAppExecutableName
)

#: 쿠커가 셰이더 컴파일 실패를 알릴 때 쓰는 문구. 이 줄이 있으면 종료 코드와 무관하게 실패다.
kShaderCompileFailureMark = "Failed to compile shader"


def findAppExecutable(projectRoot: Path, explicitPath: Path | None = None) -> Path | None:
    """빌드된 App 실행 파일을 찾습니다 (없으면 None — 아직 빌드하지 않았다는 뜻이다).

    `explicitPath` 가 있으면 그것만 본다 — CMake 가 `--app $<TARGET_FILE:App>` 로 넘기는 자리라,
    있어야 할 것이 없으면 다른 프리셋의 낡은 App 으로 조용히 대신하지 않는다.
    """
    if explicitPath is not None:
        return explicitPath if explicitPath.is_file() else None
    for relPath in kAppExecutableRelPath:
        candidate = projectRoot / relPath
        if candidate.is_file():
            return candidate
    return None


def runShaderCook(
    appExe: Path,
    *,
    cwd: Path | None = None,
    bCapture: bool = False,
) -> subprocess.CompletedProcess:
    """
    `App.exe --cook-shaders` 를 돌립니다.

    `bCapture` 가 False 면 출력이 그대로 콘솔로 흐른다(쿠킹처럼 오래 걸리는 자리에서 진행이 보인다).
    True 면 붙잡아 돌려준다 — 커밋 훅이 그 안에서 컴파일 실패 줄을 찾아야 하기 때문이다.
    """
    return runHeadlessTask(appExe, ["--cook-shaders"], cwd=cwd, bCapture=bCapture)


def runSceneCook(
    appExe: Path,
    cookedDir: Path,
    *,
    cwd: Path | None = None,
    bCapture: bool = False,
) -> subprocess.CompletedProcess:
    """
    `App.exe --cook-scenes --cooked-dir=<dir>` 를 돌립니다.

    씬 쿠킹이 엔진 안에 있는 이유는 **리플렉션** 하나다 — 엔티티 상태를 바이너리로 구우려면
    `TypeInfo` 와 프로퍼티 표가 필요하고, 파이썬에는 그것이 없다. 셰이더 쿠킹과 같은 자리다.
    """
    return runHeadlessTask(appExe, ["--cook-scenes", f"--cooked-dir={cookedDir}"], cwd=cwd, bCapture=bCapture)


def runHeadlessTask(
    appExe: Path,
    arguments: list[str],
    *,
    cwd: Path | None = None,
    bCapture: bool = False,
) -> subprocess.CompletedProcess:
    """App.exe 를 헤드리스 작업 인자로 돌립니다 (쿠킹·쿠킹이 같은 모양이라 한 자리에 둡니다)."""
    return subprocess.run(
        [str(appExe), *arguments],
        cwd=str(cwd) if cwd else None,
        capture_output=bCapture,
        text=bCapture,
        encoding="utf-8" if bCapture else None,
        errors="replace" if bCapture else None,
    )


def findShaderCompileFailures(cookOutput: str) -> list[str]:
    """쿠커 출력에서 셰이더 컴파일 실패 줄만 골라 돌려줍니다."""
    return [line for line in cookOutput.splitlines() if kShaderCompileFailureMark in line]
