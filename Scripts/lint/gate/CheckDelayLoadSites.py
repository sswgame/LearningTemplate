#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
지연 로드(`/DELAYLOAD`)를 정하는 자리를 `cmake/Engine/ModuleTargets.cmake` 의 두 함수로 막는다.

- `sw_addDelayloadHook` — 모듈 그래프(GameFramework · 키트)를 지연 로드하는 키트 · 게임. 알림 훅과 **미리 묶기**
  (`bindDelayLoadImports`)가 같이 들어가, 모듈을 올린 자리 · 엔진 기동이 그 코드가 돌기 전에 import 를 묶는다.
- `sw_addDelayloadSystemDlls` — Engine 이 필요할 때만 올리는 시스템 DLL(D3DCompiler · Media Foundation · XAudio2 · Tracy).

lld 의 x64 지연 로드 썽크는 xmm0 을 헬퍼 호출의 홈 공간에 저장해, 첫 호출이 묶으면 그 호출의 **첫 float 인자가 망가진다**
(`Source/Engine/Module/DelayLoadNotifyHook.cpp`). 타깃 CMake 에 `/DELAYLOAD` 를 직접 적으면 그 판단(미리 묶는가, 첫 인자가 float 인
함수가 없는가)을 거치지 않으므로 막는다.

  python Scripts/lint/gate/CheckDelayLoadSites.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import kLintTargetRelDirs  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

#: 지연 로드를 정해도 되는 유일한 파일(저장소 상대).
_kDelayLoadHome = "cmake/Engine/ModuleTargets.cmake"
_kDelayLoadRe = re.compile(r"DELAYLOAD\s*:", re.IGNORECASE)


class CheckDelayLoadSitesGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "지연 로드(/DELAYLOAD)를 정하는 자리 검사"
    buildComment = "Checking that delay-loaded DLLs are declared only through the ModuleTargets.cmake functions..."
    timeoutSeconds = 30
    preCommitPattern = ("*CMakeLists.txt", "*.cmake")
    preCommitFileArgument = ""
    violationHeader = "지연 로드를 직접 정한 CMake"
    hint = ("  `/DELAYLOAD` 는 cmake/Engine/ModuleTargets.cmake 의 sw_addDelayloadHook(모듈 그래프 — 미리 묶기 포함) ·\n"
            "  sw_addDelayloadSystemDlls(시스템 DLL) 로만 정합니다. 지연 로드한 함수의 첫 호출은 첫 float 인자를 망가뜨립니다.")
    selfTestCases = [
        {
            "name": "타깃 CMake 가 /DELAYLOAD 를 직접 적음",
            "files": {
                "Source/Probe/CMakeLists.txt": "target_link_options(Probe PRIVATE /DELAYLOAD:probe.dll)\n",
            },
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listPath = [path for path in self.selectTargetFiles(repositoryRoot, None, listScanRoot=kLintTargetRelDirs + ("cmake",),
                                                           suffixes=(".txt", ".cmake"))
                    if path.name == "CMakeLists.txt" or path.suffix == ".cmake"]
        topLevel = repositoryRoot / "CMakeLists.txt"
        if topLevel.is_file():
            listPath.append(topLevel)
        listViolation: list[str] = []
        for path in listPath:
            relativePath = path.relative_to(repositoryRoot).as_posix()
            if relativePath == _kDelayLoadHome:
                continue
            for lineNumber, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
                if line.lstrip().startswith("#"):
                    continue
                if _kDelayLoadRe.search(line):
                    listViolation.append(f"{relativePath}:{lineNumber}: {line.strip()}")
        return GateResult(listViolation=listViolation, summary=f"CMake 파일 {len(listPath)}개")


main = CheckDelayLoadSitesGate.run


if __name__ == "__main__":
    sys.exit(main())
