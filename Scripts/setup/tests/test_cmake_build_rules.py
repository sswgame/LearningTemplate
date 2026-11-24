"""
CMake 빌드 규칙 함수 검사 — 구성 없이 `cmake -P` 로 함수만 부른다(타깃이 필요한 판정은 통합 configure 가 본다).

- `sw_is_platform_folder_built`(cmake/Modules/Build/PlatformFolders.cmake): 플랫폼 폴더 규칙 — 플랫폼 폴더 거르기와 구성 끝 훑기가 같은 판정을 쓴다.
- `sw_link_job_count`(cmake/Modules/Config/LinkJobPool.cmake): Ninja 링크 풀 크기 — ThinLTO 구성만 좁게 묶는다.
- `sw_resolve_memory_backend`(cmake/Modules/Config/MemoryBackend.cmake): 힙 백엔드 auto 풀기 — 새니타이저 · 웹만 crt, 명시한 mimalloc 이 걸리면 구성 실패.
- `sw_decide_module_activation`(cmake/Modules/Build/ModuleManifest.cmake): 정적 모듈 구성은 고른 RHI 백엔드 하나만 켠다. 키트의 클라이언트 모듈(`GF_Client_<X>`)은
  전용 서버 구성에서 꺼지고 공유 키트(`GF_<X>`)는 켜진다 — 실제 매니페스트의 `_listTarget` 으로 본다(키트 셋 가르기: 규칙 · 디렉터 · 클라이언트).
- `sw_collect_unused_kits`(같은 파일): 게임이 공유 키트 `GF_<X>` 만 의존해도 짝(`GF_Client_<X>` · `GF_Server_<X>`)은 쓰는 키트다 — 정적 구성(Shipping)이 짝을 빼던 회귀.

실행: SW_CMAKE_COMMAND=<cmake 실행 파일> python -X utf8 Scripts/setup/tests/test_cmake_build_rules.py (CTest `Scripts.CmakeBuildRules` 가 환경 변수를 준다).
"""

from __future__ import annotations

import json
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


def memory_backend(requested: str, system_name: str = "Windows", sanitizer: str = "OFF") -> str:
    return run_cmake_script(
        f'include("{(PROJECT_ROOT / "cmake/Modules/Config/MemoryBackend.cmake").as_posix()}")\n'
        f'sw_resolve_memory_backend("{requested}" "{system_name}" "{sanitizer}" backend)\n'
        'message(NOTICE "backend=[${backend}]")\n')["backend"]


class MemoryBackendTest(unittest.TestCase):
    def test_auto_is_mimalloc_except_sanitizer_and_web(self) -> None:
        for system_name in ("Windows", "Linux", "Android"):
            self.assertEqual("mimalloc", memory_backend("auto", system_name))
        self.assertEqual("crt", memory_backend("auto", "Linux", "ON"))
        self.assertEqual("crt", memory_backend("auto", "Emscripten"))

    def test_explicit_value_wins_where_it_can(self) -> None:
        self.assertEqual("crt", memory_backend("crt"))
        self.assertEqual("mimalloc", memory_backend("mimalloc", "Linux"))

    def test_unavailable_or_unknown_value_fails_configure(self) -> None:
        for arguments in (("mimalloc", "Windows", "ON"), ("mimalloc", "Emscripten", "OFF"), ("tcmalloc", "Windows", "OFF")):
            with self.assertRaises(AssertionError):
                memory_backend(*arguments)


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


def read_manifest_targets(manifest: Path) -> list[str]:
    return list(json.loads(manifest.read_text(encoding="utf-8"))["_listTarget"])


def kit_pair_activation(target_type: str, shared: Path, client: Path) -> dict[str, str]:
    """공유 키트 · 클라이언트 키트 매니페스트의 실제 대상으로 `sw_decide_module_activation` 을 돌린 켜짐 · 까닭."""
    lines = [
        "cmake_minimum_required(VERSION 3.24)",
        f'include("{(PROJECT_ROOT / "cmake/Modules/Build/ModuleManifest.cmake").as_posix()}")',
        "set(SW_STATIC_MODULES OFF)",
        "set(SW_PLATFORM_NAME Windows)",
        "set(SW_CONFIGURATION_NAME Dev)",
        f"set(SW_TARGET_TYPE {target_type})",
        "set_property(GLOBAL PROPERTY SW_MODULE_NAMES GF_Shared GF_Client_Shared)",
    ]
    for name, manifest in (("GF_Shared", shared), ("GF_Client_Shared", client)):
        lines += [
            f"set_property(GLOBAL PROPERTY SW_MODULE_{name}_KIND Kit)",
            f"set_property(GLOBAL PROPERTY SW_MODULE_{name}_ENABLED_BY_DEFAULT ON)",
            f"set_property(GLOBAL PROPERTY SW_MODULE_{name}_PLATFORMS Windows Linux)",
            f"set_property(GLOBAL PROPERTY SW_MODULE_{name}_CONFIGURATIONS Dev Shipping)",
            f"set_property(GLOBAL PROPERTY SW_MODULE_{name}_TARGETS {' '.join(read_manifest_targets(manifest))})",
        ]
    lines += ["sw_decide_module_activation(active)"]
    for name in ("GF_Shared", "GF_Client_Shared"):
        lines += [f"get_property(on GLOBAL PROPERTY SW_MODULE_{name}_ACTIVE)", f'message(NOTICE "{name}=[${{on}}]")']
    return run_cmake_script("\n".join(lines) + "\n")


class ClientKitModuleTest(unittest.TestCase):
    KITS = PROJECT_ROOT / "Source/GameFramework/Kits"

    def test_every_client_kit_targets_only_the_client(self) -> None:
        list_manifest = sorted(self.KITS.rglob("GF_Client_*.module.json"))
        self.assertTrue(list_manifest)
        for manifest in list_manifest:
            with self.subTest(manifest=manifest.name):
                self.assertEqual(read_manifest_targets(manifest), ["Client"])

    def test_dedicated_server_skips_the_tactics_client_module(self) -> None:
        kit = self.KITS / "Genre/Strategy/TacticsSRPG"
        shared, client = kit / "GF_TacticsSRPG.module.json", kit / "Client/GF_Client_TacticsSRPG.module.json"
        self.assertEqual(kit_pair_activation("Server", shared, client), {"GF_Shared": "ON", "GF_Client_Shared": "OFF"})
        self.assertEqual(kit_pair_activation("Client", shared, client), {"GF_Shared": "ON", "GF_Client_Shared": "ON"})


def unused_kits(active: list[str], dependencies: dict[str, list[str]]) -> list[str]:
    """`active` 모듈(이름이 GF_ 로 시작하면 키트, 아니면 게임)을 세우고 `sw_collect_unused_kits` 가 돌려준 쓰지 않는 키트."""
    lines = [
        "cmake_minimum_required(VERSION 3.24)",
        f'include("{(PROJECT_ROOT / "cmake/Modules/Build/ModuleManifest.cmake").as_posix()}")',
        f"set(active {' '.join(active)})",
    ]
    for name in active:
        lines.append(f"set_property(GLOBAL PROPERTY SW_MODULE_{name}_KIND {'Kit' if name.startswith('GF_') else 'Game'})")
    for name, list_dependency in dependencies.items():
        lines.append(f"set_property(GLOBAL PROPERTY SW_MODULE_{name}_DEPENDENCIES {' '.join(list_dependency)})")
    lines += ["sw_collect_unused_kits(active unused)", 'message(NOTICE "unused=[${unused}]")']
    value = run_cmake_script("\n".join(lines) + "\n")["unused"]
    return value.split(";") if value else []


class UsedKitPairTest(unittest.TestCase):
    def test_shared_kit_dependency_keeps_its_pair_modules(self) -> None:
        active = ["Game", "GF_Tactics", "GF_Client_Tactics", "GF_Server_Tactics"]
        self.assertEqual(unused_kits(active, {"Game": ["GF_Tactics"]}), [])

    def test_missing_pair_adds_nothing_and_unused_kit_keeps_its_pair_unused(self) -> None:
        # GF_Solo 는 짝 매니페스트가 없다 — 없는 짝 이름이 결과에 끼지 않는다. GF_Idle 은 쓰지 않으니 짝도 쓰지 않는다.
        active = ["Game", "GF_Solo", "GF_Idle", "GF_Client_Idle"]
        self.assertEqual(unused_kits(active, {"Game": ["GF_Solo"]}), ["GF_Client_Idle", "GF_Idle"])


if __name__ == "__main__":
    CMAKE = os.environ.get(CMAKE_ENVIRONMENT, "")
    if not CMAKE:
        sys.exit(f"{CMAKE_ENVIRONMENT} 가 없다 — cmake 실행 파일 경로를 준다")
    unittest.main()
