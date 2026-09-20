#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
빌드된 `App.exe` 를 찾고 헤드리스로 셰이더를 굽는 자리.

**두 곳이 같은 목록을 각자 들고 있었고, 이미 갈라져 있었다.**

| | `CookAssets.bakeShadersInternal` | `PreCommitLint.checkStagedShadersInternal` |
| --- | --- | --- |
| Ninja-Debug/Bin | 본다 | 본다 |
| Ninja-Release/Bin | 본다 | 본다 |
| **Ninja-Shipping/Bin** | **본다** | **안 본다** |
| Bin (배포 레이아웃) | 본다 | 본다 |

Shipping 이 목록에 있는 이유는 쿠커 쪽 주석에 적혀 있었다 — *"Shipping App 도 베이커를 링크한다
(로그만 안 남는다). 두 번째 Shipping 빌드부터는 이 경로가 살아 있어서 Dev 빌드 없이도 스스로 다시
굽는다."* 그런데 커밋 훅은 그 줄을 못 봤으므로, **Shipping 만 빌드해 둔 사람은** 셰이더를 고쳐
커밋할 때 "App.exe를 찾을 수 없어 검증을 건너뜁니다" 를 받고 지나갔다.

목록이 둘이면 이렇게 갈라진다. 여기가 그 목록의 한 자리다.

@note 이름이 `App.exe` 로 고정이라 지금은 Windows 에서만 찾아진다. 리눅스에서는 두 호출부 모두
      "못 찾았으니 건너뛴다" 로 끝난다 — 예전 동작 그대로다. 리눅스에서도 굽게 하려면 후보에
      확장자 없는 `App` 을 더하면 되지만, 그건 동작을 바꾸는 일이라 따로 확인하고 한다.
"""

from __future__ import annotations

import subprocess
from pathlib import Path

#: 빌드된 App 을 찾는 자리 — 앞에서부터 본다 (저장소 루트 기준).
kAppExecutableRelPath: tuple[str, ...] = (
    "build/Ninja-Debug/Bin/App.exe",
    "build/Ninja-Release/Bin/App.exe",
    # Shipping App 도 베이커를 링크한다(로그만 안 남는다). 두 번째 Shipping 빌드부터는
    # 이 경로가 살아 있어서 Dev 빌드 없이도 스스로 다시 굽는다.
    "build/Ninja-Shipping/Bin/App.exe",
    "Bin/App.exe",
)

#: 베이커가 셰이더 컴파일 실패를 알릴 때 쓰는 문구. 이 줄이 있으면 종료 코드와 무관하게 실패다.
kShaderCompileFailureMark = "Failed to compile shader"


def findAppExecutable(projectRoot: Path) -> Path | None:
    """빌드된 App 실행 파일을 찾습니다 (없으면 None — 아직 빌드하지 않았다는 뜻이다)."""
    for relPath in kAppExecutableRelPath:
        candidate = projectRoot / relPath
        if candidate.is_file():
            return candidate
    return None


def runShaderBake(
    appExe: Path,
    *,
    cwd: Path | None = None,
    bCapture: bool = False,
) -> subprocess.CompletedProcess:
    """
    `App.exe --bake-shaders` 를 돌립니다.

    `bCapture` 가 False 면 출력이 그대로 콘솔로 흐른다(쿠킹처럼 오래 걸리는 자리에서 진행이 보인다).
    True 면 붙잡아 돌려준다 — 커밋 훅이 그 안에서 컴파일 실패 줄을 찾아야 하기 때문이다.
    """
    return runHeadlessTask(appExe, ["--bake-shaders"], cwd=cwd, bCapture=bCapture)


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
    `TypeInfo` 와 프로퍼티 표가 필요하고, 파이썬에는 그것이 없다. 셰이더 베이크와 같은 자리다.
    """
    return runHeadlessTask(appExe, ["--cook-scenes", f"--cooked-dir={cookedDir}"], cwd=cwd, bCapture=bCapture)


def runHeadlessTask(
    appExe: Path,
    arguments: list[str],
    *,
    cwd: Path | None = None,
    bCapture: bool = False,
) -> subprocess.CompletedProcess:
    """App.exe 를 헤드리스 작업 인자로 돌립니다 (베이크·쿠킹이 같은 모양이라 한 자리에 둡니다)."""
    return subprocess.run(
        [str(appExe), *arguments],
        cwd=str(cwd) if cwd else None,
        capture_output=bCapture,
        text=bCapture,
        encoding="utf-8" if bCapture else None,
        errors="replace" if bCapture else None,
    )


def findShaderCompileFailures(bakeOutput: str) -> list[str]:
    """베이커 출력에서 셰이더 컴파일 실패 줄만 골라 돌려줍니다."""
    return [line for line in bakeOutput.splitlines() if kShaderCompileFailureMark in line]
