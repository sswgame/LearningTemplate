"""
CMake 빌드 규칙 함수 검사 — 구성 없이 `cmake -P` 로 함수만 부른다(타깃이 필요한 판정은 통합 configure 가 본다).

- `sw_is_platform_folder_built`(cmake/Modules/Build/PlatformFolders.cmake): 플랫폼 폴더 규칙 — 플랫폼 폴더 거르기와 구성 끝 훑기가 같은 판정을 쓴다.
- `sw_link_job_count`(cmake/Modules/Config/LinkJobPool.cmake): Ninja 링크 풀 크기 — ThinLTO 구성만 좁게 묶는다.
- `sw_decide_module_activation`(cmake/Modules/Build/ModuleManifest.cmake): 정적 모듈 구성은 고른 RHI 백엔드 하나만 켠다.

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


def link_job_counts(cases: list[tuple[int, bool]]) -> dict[str, str]:
    lines = [f'include("{(PROJECT_ROOT / "cmake/Modules/Config/LinkJobPool.cmake").as_posix()}")']
    for cpu_count, lto in cases:
        lines.append(f'sw_link_job_count({cpu_count} {"ON" if lto else "OFF"} jobs)')
        lines.append(f'message(NOTICE "{cpu_count}-{lto}=[${{jobs}}]")')
    return run_cmake_script("\n".join(lines) + "\n")


class LinkJobPoolTest(unittest.TestCase):
    def test_lto_is_quarter_with_narrow_cap(self) -> None:
        jobs = link_job_counts([(4, True), (16, True), (64, True)])
        self.assertEqual((jobs["4-True"], jobs["16-True"], jobs["64-True"]), ("2", "4", "4"))

    def test_without_lto_is_half_with_wider_cap(self) -> None:
        jobs = link_job_counts([(2, False), (12, False), (32, False)])
        self.assertEqual((jobs["2-False"], jobs["12-False"], jobs["32-False"]), ("2", "6", "8"))


def module_activation(static_modules: bool, platform: str = "Windows") -> dict[str, str]:
    """매니페스트 셋(RHI 둘 + 키트)을 전역 속성으로 세우고 해석한 켜짐 · 까닭."""
    lines = [
        "cmake_minimum_required(VERSION 3.24)",
        f'include("{(PROJECT_ROOT / "cmake/Modules/Build/ModuleManifest.cmake").as_posix()}")',
        f'set(SW_STATIC_MODULES {"ON" if static_modules else "OFF"})',
        "set(SW_SHIPPING_RHI_BACKEND Vulkan)",
        "set(SW_RHI_BACKEND_Vulkan_MODULE RHI_Vulkan)",
        "set(SW_RHI_BACKEND_DirectX12_MODULE RHI_DX12)",
        "set(SW_RHI_BACKEND_NAMES Vulkan DirectX12)",
        f"set(SW_PLATFORM_NAME {platform})",
        "set(SW_CONFIGURATION_NAME Shipping)",
        "set(SW_TARGET_TYPE Client)",
        "set_property(GLOBAL PROPERTY SW_MODULE_NAMES RHI_Vulkan RHI_DX12 GF_Kit)",
    ]
    for name, kind in (("RHI_Vulkan", "RHI"), ("RHI_DX12", "RHI"), ("GF_Kit", "Kit")):
        lines += [
            f"set_property(GLOBAL PROPERTY SW_MODULE_{name}_KIND {kind})",
            f"set_property(GLOBAL PROPERTY SW_MODULE_{name}_ENABLED_BY_DEFAULT ON)",
            f"set_property(GLOBAL PROPERTY SW_MODULE_{name}_PLATFORMS Windows Linux)",
            f"set_property(GLOBAL PROPERTY SW_MODULE_{name}_CONFIGURATIONS Dev Shipping)",
            f"set_property(GLOBAL PROPERTY SW_MODULE_{name}_TARGETS Client)",
        ]
    lines += ["sw_decide_module_activation(active)"]
    for name in ("RHI_Vulkan", "RHI_DX12", "GF_Kit"):
        lines += [
            f"get_property(on GLOBAL PROPERTY SW_MODULE_{name}_ACTIVE)",
            f"get_property(why GLOBAL PROPERTY SW_MODULE_{name}_REASON)",
            f'message(NOTICE "{name}=[${{on}}|${{why}}]")',
        ]
    return run_cmake_script("\n".join(lines) + "\n")


class StaticRHIActivationTest(unittest.TestCase):
    def test_static_keeps_only_the_linked_backend(self) -> None:
        state = module_activation(static_modules=True)
        self.assertEqual(state["RHI_Vulkan"], "ON|")
        self.assertTrue(state["RHI_DX12"].startswith("OFF|not the linked RHI backend"), state["RHI_DX12"])
        self.assertEqual(state["GF_Kit"], "ON|")

    def test_dynamic_keeps_every_backend(self) -> None:
        state = module_activation(static_modules=False)
        self.assertEqual((state["RHI_Vulkan"], state["RHI_DX12"]), ("ON|", "ON|"))


if __name__ == "__main__":
    CMAKE = os.environ.get(CMAKE_ENVIRONMENT, "")
    if not CMAKE:
        sys.exit(f"{CMAKE_ENVIRONMENT} 가 없다 — cmake 실행 파일 경로를 준다")
    unittest.main()
