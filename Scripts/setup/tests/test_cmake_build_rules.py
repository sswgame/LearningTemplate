"""
CMake 빌드 규칙 함수 검사 — 구성 없이 `cmake -P` 로 함수만 부른다(타깃이 필요한 판정은 통합 configure 가 본다).

- `sw_is_platform_folder_built`(cmake/Modules/Build/PlatformFolders.cmake): 플랫폼 폴더 규칙 — 플랫폼 폴더 거르기와 구성 끝 훑기가 같은 판정을 쓴다.

실행: SW_CMAKE_COMMAND=<cmake 실행 파일> python -X utf8 Scripts/setup/tests/test_cmake_build_rules.py (CTest `Scripts.CmakeBuildRules` 가 환경 변수를 준다).
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[3]
CMAKE_ENVIRONMENT = "SW_CMAKE_COMMAND"  # Test/CMakeLists.txt 가 스크립트 테스트 모두에 ${CMAKE_COMMAND} 를 준다
CMAKE = ""


def run_cmake_script(body: str) -> dict[str, str]:
    """`body` 를 저장소 뿌리에서 `cmake -P` 로 돌리고 `message(NOTICE "이름=[값]")` 줄을 사전으로 돌려준다."""
    with tempfile.TemporaryDirectory() as folder:
        script = Path(folder) / "Probe.cmake"
        script.write_text(body, encoding="utf-8")
        result = subprocess.run([CMAKE, "-P", str(script)], capture_output=True, text=True, cwd=PROJECT_ROOT)
    if result.returncode != 0:
        raise AssertionError(f"cmake -P failed ({result.returncode}):\n{result.stderr}")
    values: dict[str, str] = {}
    for line in result.stderr.splitlines():
        name, separator, rest = line.partition("=[")
        if separator and rest.endswith("]"):
            values[name] = rest[:-1]
    return values


def platform_folder_built(system_name: str, win32: bool, paths: list[str]) -> dict[str, str]:
    lines = [
        f'set(CMAKE_SYSTEM_NAME "{system_name}")',
        f'set(WIN32 {"ON" if win32 else "OFF"})',
        f'include("{(PROJECT_ROOT / "cmake/Modules/Build/PlatformFolders.cmake").as_posix()}")',
    ]
    for path in paths:
        lines.append(f'sw_is_platform_folder_built("{path}" built)')
        lines.append(f'message(NOTICE "{path}=[${{built}}]")')
    return run_cmake_script("\n".join(lines) + "\n")


class PlatformFolderTest(unittest.TestCase):
    PATHS = [
        "Source/Core/File/Windows/WindowsFile.cpp",
        "Source/Core/File/Linux/LinuxFile.cpp",
        "Source/Core/File/LinuxKernel/Inotify.cpp",
        "Source/Core/File/Posix/PosixFile.cpp",
        "Source/Engine/Window/Android/AndroidWindow.cpp",
        "Source/Engine/Window/Web/WebWindow.cpp",
        "Source/Engine/Scene/Scene.cpp",
    ]

    def test_windows(self) -> None:
        built = platform_folder_built("Windows", True, self.PATHS)
        self.assertEqual([path for path in self.PATHS if built[path] == "ON"],
                         ["Source/Core/File/Windows/WindowsFile.cpp", "Source/Engine/Scene/Scene.cpp"])

    def test_linux(self) -> None:
        built = platform_folder_built("Linux", False, self.PATHS)
        self.assertEqual([path for path in self.PATHS if built[path] == "ON"],
                         ["Source/Core/File/Linux/LinuxFile.cpp", "Source/Core/File/LinuxKernel/Inotify.cpp",
                          "Source/Core/File/Posix/PosixFile.cpp", "Source/Engine/Scene/Scene.cpp"])

    def test_android_shares_linux_kernel_not_desktop_linux(self) -> None:
        built = platform_folder_built("Android", False, self.PATHS)
        self.assertEqual([path for path in self.PATHS if built[path] == "ON"],
                         ["Source/Core/File/LinuxKernel/Inotify.cpp", "Source/Core/File/Posix/PosixFile.cpp",
                          "Source/Engine/Window/Android/AndroidWindow.cpp", "Source/Engine/Scene/Scene.cpp"])

    def test_web(self) -> None:
        built = platform_folder_built("Emscripten", False, self.PATHS)
        self.assertEqual([path for path in self.PATHS if built[path] == "ON"],
                         ["Source/Core/File/Posix/PosixFile.cpp", "Source/Engine/Window/Web/WebWindow.cpp",
                          "Source/Engine/Scene/Scene.cpp"])


if __name__ == "__main__":
    CMAKE = os.environ.get(CMAKE_ENVIRONMENT, "")
    if not CMAKE:
        sys.exit(f"{CMAKE_ENVIRONMENT} 가 없다 — cmake 실행 파일 경로를 준다")
    unittest.main()
