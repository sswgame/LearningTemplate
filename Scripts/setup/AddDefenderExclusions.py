#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Windows Defender 실시간 감시 제외 경로 및 프로세스 자동 등록 스크립트.

관리자 권한(UAC)을 자동으로 요청하여 프로젝트 디렉터리 및 빌드 도구를
Windows Defender 제외 목록에 등록하고 파일 잠금을 해제합니다.
"""

from __future__ import annotations

import ctypes
import subprocess
import sys
from pathlib import Path


def isUserAdmin() -> bool:
    """현재 프로세스가 관리자 권한으로 실행 중인지 확인합니다."""
    try:
        return ctypes.windll.shell32.IsUserAnAdmin() != 0
    except Exception:
        return False


def requestAdminElevation() -> None:
    """Windows UAC 관리자 승인 창을 띄워 현재 스크립트를 관리자 권한으로 재실행합니다."""
    scriptPath = str(Path(__file__).resolve())
    ret = ctypes.windll.shell32.ShellExecuteW(
        None, "runas", sys.executable, f'"{scriptPath}"', None, 1
    )
    if ret <= 32:
        print("[오류] 관리자 권한이 승인되지 않았습니다.")
        input("\n엔터 키를 누르면 종료합니다...")
    sys.exit(0)


def addDefenderExclusions() -> None:
    """Windows Defender 제외 경로/프로세스를 등록하고 Unblock-File을 수행합니다."""
    projectRoot = Path(__file__).resolve().parents[2]
    projectDir = str(projectRoot)

    print("=" * 60)
    print("  SW Engine - Windows Defender 예외 등록")
    print("=" * 60)
    print(f"\n[*] 프로젝트 경로: {projectDir}")

    # 1. 제외 경로 등록
    print("[*] 1/4. 프로젝트 및 빌드 출력 디렉터리 실시간 감시 제외 등록 중...")
    cmdPath = f'Add-MpPreference -ExclusionPath @("{projectDir}", "{projectDir}\\build")'
    subprocess.run(["powershell", "-NoProfile", "-Command", cmdPath], check=False)

    # 2. 프로세스 제외 등록
    print("[*] 2/4. 빌드 및 엔진 도구 프로세스 제외 등록 중...")
    processes = [
        "ninja.exe",
        "clang-cl.exe",
        "lld-link.exe",
        "sccache.exe",
        "App.exe",
        "ReflectionParser.exe",
        "CoreTest.exe",
        "EngineTest.exe",
        "SmokeTest.exe",
        "EditorTest.exe",
    ]
    procListStr = "@(" + ", ".join(f"'{p}'" for p in processes) + ")"
    cmdProc = f"Add-MpPreference -ExclusionProcess {procListStr}"
    subprocess.run(["powershell", "-NoProfile", "-Command", cmdProc], check=False)

    # 3. DLL 및 바이너리 확장자 제외 등록 (RHI 모듈 / 엔진 DLL 등)
    print("[*] 3/4. RHI 백엔드 및 엔진 DLL / 아카이브 확장자 제외 등록 중...")
    cmdExt = 'Add-MpPreference -ExclusionExtension @("dll", "pdb", "pack", "pak", "rhi")'
    subprocess.run(["powershell", "-NoProfile", "-Command", cmdExt], check=False)

    # 4. 다운로드 및 빌드된 바이너리 잠금 해제
    print("[*] 4/4. 프로젝트 내부 파일 및 DLL 잠금 해제 (Unblock-File) 중...")
    cmdUnblock = f'Get-ChildItem -Path "{projectDir}" -Recurse -ErrorAction SilentlyContinue | Unblock-File'
    subprocess.run(["powershell", "-NoProfile", "-Command", cmdUnblock], check=False)

    print("\n" + "=" * 60)
    print("  [성공] Windows Defender 예외 등록이 모두 완료되었습니다!")
    print("=" * 60)
    print("\n이제 빌드 및 실행 시 Windows 보안 차단 없이 원활하게 실행됩니다.")
    input("\n엔터 키를 누르면 창이 닫힙니다...")


def main() -> int:
    if sys.platform != "win32":
        print("[안내] 이 스크립트는 Windows 전용입니다.")
        return 0

    if not isUserAdmin():
        print("[안내] 관리자 권한(UAC)을 요청합니다...")
        requestAdminElevation()
        return 0

    addDefenderExclusions()
    return 0


if __name__ == "__main__":
    sys.exit(main())
