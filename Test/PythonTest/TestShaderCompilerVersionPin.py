#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
셰이더 컴파일러 버전 고정 — `ShaderCooker::kCompilerVersion` 이 vcpkg 에 설치된 `directx-dxc` 포트 버전과 같은지 봅니다.

쿠킹 스탬프의 도구 지문은 컴파일러 파일 해시가 아니라 이 버전 글로 만든다(Windows 에서 쿠킹해 커밋한 산출물을 리눅스도 쓴다).
그래서 DXC 를 올리고 이 글을 고치지 않으면 옛 컴파일러로 만든 바이너리가 "최신" 으로 남는다 — 여기서 막는다.
설치 기록(`build/vcpkg_installed/vcpkg/status`)이 없는 환경은 건너뛴다.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
kCookerHeader = kRepositoryRoot / "Source/Engine/Graphics/Shader/Compile/ShaderCooker.h"
kVcpkgStatus = kRepositoryRoot / "build/vcpkg_installed/vcpkg/status"
kPortName = "directx-dxc"


def readPinnedVersionInternal() -> str:
    """`kCompilerVersion = "directx-dxc <버전>"` 의 버전 부분입니다."""
    match = re.search(r'kCompilerVersion\s*=\s*"' + re.escape(kPortName) + r' ([^"]+)"', kCookerHeader.read_text(encoding="utf-8"))
    return match.group(1) if match else ""


def readInstalledVersionsInternal() -> set[str]:
    """설치 기록의 `directx-dxc` 항목 버전들입니다(트리플릿마다 한 항목)."""
    setVersion: set[str] = set()
    for block in kVcpkgStatus.read_text(encoding="utf-8", errors="replace").split("\n\n"):
        fields = dict(line.split(": ", 1) for line in block.splitlines() if ": " in line)
        if fields.get("Package") == kPortName and "install ok installed" in fields.get("Status", ""):
            setVersion.add(fields.get("Version", ""))
    return setVersion


class ShaderCompilerVersionPinTest(unittest.TestCase):
    def testPinnedVersionMatchesInstalledPort(self) -> None:
        pinned = readPinnedVersionInternal()
        self.assertTrue(pinned, f"{kCookerHeader.name} 에 kCompilerVersion = \"{kPortName} <버전>\" 이 없다")
        if not kVcpkgStatus.is_file():
            self.skipTest("vcpkg 설치 기록이 없다(구성 전)")
        setInstalled = readInstalledVersionsInternal()
        if not setInstalled:
            self.skipTest(f"{kPortName} 가 설치되지 않았다")
        self.assertEqual(setInstalled, {pinned},
                         f"설치된 {kPortName} 버전 {sorted(setInstalled)} 이 kCompilerVersion({pinned})과 다르다 — 글을 고치고 셰이더를 다시 쿠킹한다(App --cook-shaders)")


if __name__ == "__main__":
    unittest.main()
