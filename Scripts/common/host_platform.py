#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/common/host_platform.py

스크립트가 도는 호스트 OS 의 한 자리다. 실행 파일 · 공유 라이브러리 이름, 표의 플랫폼 열쇠는 여기 표에서 읽는다.

**왜 한 자리인가.** 스크립트마다 `sys.platform == "win32"` · `os.name` · `platform.system()` 을 따로 물으면 `else` 가 "Windows 가 아니면 Linux" 를
가정해, 표에 없는 호스트(macOS 등)에서 Linux 가지를 조용히 탔다. 호스트는 이 표의 줄만 될 수 있고 없으면 들여올 때 멈춘다 —
그래서 `is_windows_host()` 의 `else` 는 표의 나머지 줄(지금 Linux)이다. 다른 곳에서 OS 를 직접 물으면 린트(`platform_chains.py`)가 막는다.
CMake 쪽의 같은 자리는 `cmake/Modules/Build/BuildLayout.cmake` 의 `SW_PLATFORM_NAME` 이다(대상 플랫폼).
"""

from __future__ import annotations

import platform
from dataclasses import dataclass


@dataclass(frozen=True)
class HostTraits:
    """호스트 OS 한 줄."""
    name: str                   # platform.system() 의 이름("Windows" · "Linux")
    key: str                    # 설정 표(search_paths.json 등)의 열쇠("windows" · "linux")
    executable_suffix: str       # 실행 파일 접미사
    shared_library_prefix: str    # 공유 라이브러리 접두사(POSIX 의 lib)
    shared_library_suffix: str    # 공유 라이브러리 확장자
    path_separator: str          # 환경 변수 PATH 의 구분자


MAP_HOST_TRAITS: dict[str, HostTraits] = {
    "Windows": HostTraits("Windows", "windows", ".exe", "", ".dll", ";"),
    "Linux": HostTraits("Linux", "linux", "", "lib", ".so", ":"),
}


def resolve_host_traits() -> HostTraits:
    """지금 호스트의 줄이다. 표에 없으면 멈춘다(폴백하지 않는다)."""
    system_name = platform.system()
    if system_name not in MAP_HOST_TRAITS:
        raise RuntimeError(f"[HostPlatform] 지원하지 않는 호스트 OS '{system_name}' — Scripts/common/host_platform.py 표에 줄을 더한다")
    return MAP_HOST_TRAITS[system_name]


HOST: HostTraits = resolve_host_traits()


def is_windows_host() -> bool:
    """Windows 호스트면 True 다. 아니면 표의 나머지 줄(Linux)이다."""
    return HOST.name == "Windows"


def executable_name(stem: str) -> str:
    """호스트의 실행 파일 이름(`App` → `App.exe`)."""
    return f"{stem}{HOST.executable_suffix}"
