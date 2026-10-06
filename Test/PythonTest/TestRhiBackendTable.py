#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
RHI 백엔드 표(Config/Engine/CookContract.json rhi_backends)의 CMake 쪽 — `cmake/Engine/RhiBackends.cmake`.

결함 고정: 배포 빌드가 고른 백엔드가 그 플랫폼에 없으면(리눅스 DirectX12) 예전 Engine CMakeLists 는 어느 갈래도 타지 않고 **백엔드 없는 Engine** 을
링크했다(구성 · 빌드 통과, 실행에서 "백엔드 없음"). 이제 `sw_resolveShippingRhiBackend` 가 구성을 세운다. 표의 이름만 받는다 —
명령줄 이름(dx12 · vk)이나 대소문자가 다른 이름은 별칭 없이 구성 실패(옛 캐시 값을 고치라는 안내와 함께). 표의 줄마다 모듈 폴더 · 매니페스트 · 장치 소스 폴더가 실제로 있는지도 본다.
"""

from __future__ import annotations

import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]


def readCmakeMinimumLineInternal() -> str:
    """루트 CMakeLists 의 `cmake_minimum_required` 줄 — `cmake -P` 스크립트도 프로젝트와 같은 정책으로 돈다(없으면 정책이 OLD 라 모듈이 구성을 세운다)."""
    text = (kRepositoryRoot / "CMakeLists.txt").read_text(encoding="utf-8")
    return re.search(r"^cmake_minimum_required\([^)]*\)", text, re.MULTILINE).group(0)


def runResolveInternal(folder: Path, backendText: str, platform: str, listPlatformOfDx12: list[str]) -> subprocess.CompletedProcess:
    """생성기로 표를 만들고, 매니페스트 플랫폼을 꾸며 `sw_resolveShippingRhiBackend` 를 부르는 CMake 스크립트."""
    headerPath, cmakePath = folder / "CookContract.gen.h", folder / "CookContract.cmake"
    subprocess.run([sys.executable, str(kRepositoryRoot / "Scripts/generate/GenerateCookContract.py"), str(headerPath), str(cmakePath)],
                   check=True, capture_output=True)
    listLine = [readCmakeMinimumLineInternal(),
                f'include("{cmakePath.as_posix()}")',
                f'include("{(kRepositoryRoot / "cmake/Engine/RhiBackends.cmake").as_posix()}")',
                f"set(sw_platform_name {platform})"]
    for name in ("RHI_DX11", "RHI_DX12", "RHI_Vulkan", "RHI_GL"):
        listPlatform = listPlatformOfDx12 if name in ("RHI_DX11", "RHI_DX12") else ["Windows", "Linux"]
        listLine.append(f"set_property(GLOBAL PROPERTY SW_MODULE_{name}_PLATFORMS {' '.join(listPlatform)})")
    listLine += [f'sw_resolveShippingRhiBackend("{backendText}" backend)', 'message(NOTICE "resolved=${backend}")']
    scriptPath = folder / "resolve.cmake"
    scriptPath.write_text("\n".join(listLine) + "\n", encoding="utf-8")
    return subprocess.run([shutil.which("cmake") or "cmake", "-P", str(scriptPath)], capture_output=True, encoding="utf-8",
                          errors="replace", check=False)


@unittest.skipIf(shutil.which("cmake") is None, "cmake 가 PATH 에 없다")
class RhiBackendTableTest(unittest.TestCase):
    def resolveInternal(self, backendText: str, platform: str = "Windows",
                        listPlatformOfDx12: list[str] | None = None) -> subprocess.CompletedProcess:
        with tempfile.TemporaryDirectory() as folder:
            return runResolveInternal(Path(folder), backendText, platform, listPlatformOfDx12 or ["Windows"])

    def testTableNamesResolve(self) -> None:
        for text in ("DirectX11", "DirectX12", "Vulkan", "OpenGL"):
            completed = self.resolveInternal(text)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertIn(f"resolved={text}", completed.stderr)

    def testAliasesAndUnknownNamesStopTheConfigure(self) -> None:
        for text in ("dx12", "vk", "OPENGL", "DX13"):
            completed = self.resolveInternal(text)
            self.assertNotEqual(completed.returncode, 0, f"{text}: 별칭 · 모르는 이름이 구성을 통과했다")
            message = " ".join(completed.stderr.split())
            self.assertIn(f"unknown backend '{text}'", message)
            self.assertIn("DirectX11, DirectX12, Vulkan, OpenGL", message)
            self.assertIn("CMakeCache.txt", message)

    def testBackendMissingOnThisPlatformStopsTheConfigure(self) -> None:
        completed = self.resolveInternal("DirectX12", platform="Linux")
        self.assertNotEqual(completed.returncode, 0, "리눅스에서 DirectX12 를 고른 배포 구성이 통과했다 — 백엔드 없는 Engine 이 링크된다")
        self.assertIn("is not available on Linux", " ".join(completed.stderr.split()))  # CMake 가 메시지를 줄바꿈한다
        self.assertEqual(self.resolveInternal("Vulkan", platform="Linux").returncode, 0)

    def testEveryTableRowHasItsModuleAndSources(self) -> None:
        table = json.loads((kRepositoryRoot / "Config/Engine/CookContract.json").read_text(encoding="utf-8"))
        rhiRoot = kRepositoryRoot / "Source/Engine/Graphics/RHI"
        for row in table["rhi_backends"]:
            moduleFolder = rhiRoot / "Modules" / row["source_folder"]
            self.assertTrue((moduleFolder / f"{row['module']}.module.json").is_file(), row["name"])
            self.assertIn(f"sw_addRhiBackendModule({row['name']})", (moduleFolder / "CMakeLists.txt").read_text(encoding="utf-8"))
            self.assertTrue(any((rhiRoot / row["source_folder"]).rglob("*.cpp")), f"{row['name']}: 장치 소스가 없다")


if __name__ == "__main__":
    unittest.main()
