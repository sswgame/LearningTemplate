# -*- coding: utf-8 -*-
"""
내보낸 glTF 를 엔진 메시로 임포트하는 명령 — `App --import-models`. **bpy 를 쓰지 않는다.**

App 은 작업 폴더에서 위로 올라가며 `Resource/` 를 찾으므로 `build/<프리셋>/Bin` 에서 띄운다(저장소 스크립트와 같은 규칙).
임포트는 `models_raw/` 의 `import.stamp` 와 원본 해시를 대조해 바뀐 원본만 다시 쓴다 — 방금 내보낸 파일이 그 하나다.
"""

from __future__ import annotations

import os
import subprocess
from pathlib import Path

#: App 이 있을 수 있는 빌드 폴더 — `Scripts/common/AppBinary.py` 의 `kAppBuildBinDir` 과 같은 순서다(Dev 빌드만: 임포트는 에디터 모듈의 일이다).
kAppBuildBinDir: tuple[str, ...] = (
    "build/Ninja-Debug/Bin",
    "build/Ninja-Release/Bin",
    "build/CI-Debug/Bin",
    "build/WSL-Debug/Bin",
    "build/WSL-Release/Bin",
)

kAppExecutableName: tuple[str, ...] = ("App.exe", "App")


def findAppExecutable(repositoryRoot: str, explicitPath: str = "") -> str:
    """App 실행 파일 경로입니다. 명시한 경로가 있으면 그것만 봅니다. 못 찾으면 빈 문자열입니다."""
    if explicitPath:
        return explicitPath if Path(explicitPath).is_file() else ""
    for binDir in kAppBuildBinDir:
        for name in kAppExecutableName:
            candidate = os.path.normpath(Path(repositoryRoot, binDir, name))
            if Path(candidate).is_file():
                return candidate
    return ""


def makeImportCommand(appPath: str) -> tuple[list[str], str]:
    """(명령 인자 목록, 작업 폴더) — 작업 폴더는 App 이 있는 `Bin` 이다."""
    return [appPath, "--import-models"], str(Path(appPath).parent)


def startImport(appPath: str) -> subprocess.Popen:
    """임포트를 띄웁니다(기다리지 않는다 — Blender UI 를 막지 않는다). 끝은 `pollImport` 로 본다."""
    listArgument, workingDirectory = makeImportCommand(appPath)
    return subprocess.Popen(listArgument, cwd=workingDirectory, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                            encoding="utf-8", errors="replace")


def summarizeImportOutput(output: str) -> tuple[bool, list[str]]:
    """App 출력에서 (오류가 없었나, 알릴 줄들) 을 고릅니다 — `[Error]` 줄과 임포트 요약 줄(`Model import: …`)."""
    listLine = [line.strip() for line in output.splitlines() if "[Error]" in line or "Model import" in line or "Model check" in line]
    bClean = not any("[Error]" in line for line in listLine)
    return bClean, listLine
