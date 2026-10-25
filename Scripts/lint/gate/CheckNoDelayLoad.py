#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Windows 지연 로드(`/DELAYLOAD` · `delayimp`)를 막는다 — 이 저장소의 이미지는 지연 import 를 갖지 않는다.

lld 20 의 x64 지연 로드 썽크(`__tailMerge_<dll>`)는 xmm0~3 을 `__delayLoadHelper2` 호출의 홈 공간에 저장해, 첫 호출이 묶으면 그 호출의
**float 인자가 망가진다**(LLVM 21 에서 고쳐졌다 — `docs/09_Decisions.md`). 그래서

- 모듈끼리(키트 → GameFramework · 공유 키트, SWGame → 키트, 에디터 확장 → EditorModule · 키트)는 일반 import 다. 핫 리로드는 섀도 복사본에
  세대 이름을 주고 의존하는 복사본의 import 이름을 바꿔 맞춘다(`ModuleShadowCopy::writeShadowCopy`).
- 없을 수 있는 시스템 DLL(Media Foundation · XAudio2 · TracyClient)은 쓰는 쪽이 LoadLibrary + 함수 표로 올린다.

런타임 그물은 `LiveReloadManager` 가 지연 import 가 있는 모듈 이미지를 올리기 전에 거절하는 것이고, 빌드된 이미지 전체는
`ArchitectureTest.NoBuiltImageHasDelayLoadedImports` 가 본다.

  python Scripts/lint/gate/CheckNoDelayLoad.py [--root <repo>]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.Constants import kLintTargetRelDirs  # noqa: E402
from lint.LintGate import LintGate  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile, SourceScanResult, SourceScanSet  # noqa: E402

#: 지연 로드를 켜는 CMake 낱말 — 링커 옵션과 그 헬퍼 라이브러리.
_kDelayLoadRe = re.compile(r"DELAYLOAD\s*:|\bdelayimp\b", re.IGNORECASE)


class CheckNoDelayLoadGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "지연 로드(/DELAYLOAD · delayimp) 금지 검사"
    buildComment = "Checking that no target delay-loads a DLL..."
    timeoutSeconds = 30
    preCommitPattern = ("*CMakeLists.txt", "*.cmake")
    preCommitFileArgument = ""
    violationHeader = "지연 로드를 켜는 CMake"
    hint = ("  지연 로드를 쓰지 않습니다 — lld 20 의 지연 로드 썽크는 첫 호출의 float 인자를 망가뜨립니다(docs/09_Decisions.md).\n"
            "  모듈끼리는 일반 import(핫 리로드가 이름을 맞춘다), 없을 수 있는 시스템 DLL 은 LoadLibrary + 함수 표로 올립니다.")
    selfTestCases = [
        {
            "name": "타깃 CMake 가 /DELAYLOAD 를 적음",
            "expectedViolation": "Source/Probe/CMakeLists.txt:1: target_link_options(Probe PRIVATE /DELAYLOAD:probe.dll)",
            "files": {
                "Source/Probe/CMakeLists.txt": "target_link_options(Probe PRIVATE /DELAYLOAD:probe.dll)\n",
            },
        },
        {
            "name": "링커 래퍼 철자로 지연 로드",
            "expectedViolation": "cmake/Probe.cmake:1: target_link_options(Probe PRIVATE \"LINKER:/DELAYLOAD:probe.dll\")",
            "files": {
                "cmake/Probe.cmake": "target_link_options(Probe PRIVATE \"LINKER:/DELAYLOAD:probe.dll\")\n",
            },
        },
        {
            "name": "delayimp 링크",
            "expectedViolation": "Source/Probe/CMakeLists.txt:1: target_link_libraries(Probe PRIVATE delayimp)",
            "files": {
                "Source/Probe/CMakeLists.txt": "target_link_libraries(Probe PRIVATE delayimp)\n",
            },
        },
        {
            # 저장소 루트의 CMakeLists.txt 는 폴더 집합이 아니라 이름으로 고르는 집합(`relativeFiles`)이 읽는다.
            "name": "저장소 루트 CMakeLists.txt",
            "expectedViolation": "CMakeLists.txt:2: target_link_options(Probe PRIVATE /DELAYLOAD:probe.dll)",
            "files": {
                "CMakeLists.txt": "# /DELAYLOAD:commented.dll 는 주석이라 보지 않는다\ntarget_link_options(Probe PRIVATE /DELAYLOAD:probe.dll)\n",
            },
        },
    ]

    # 저장소 루트의 `CMakeLists.txt` 는 폴더를 걸을 까닭이 없다 — 이름으로 고르는 집합(`relativeFiles`)이다.
    sourceScanSets = (SourceScanSet("tree", kLintTargetRelDirs + ("cmake",), (".cmake",), ("CMakeLists.txt",)),
                      SourceScanSet("top", (), relativeFiles=("CMakeLists.txt",)))

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        listViolation: list[str] = []
        for lineNumber, line in enumerate(source.listLine, start=1):
            if line.lstrip().startswith("#"):
                continue
            if _kDelayLoadRe.search(line):
                listViolation.append(f"{source.relative}:{lineNumber}: {line.strip()}")
        return FileFindings(listViolation=listViolation)

    def describeSourceScan(self, result: SourceScanResult) -> str:
        return f"CMake 파일 {result.fileCount}개"


main = CheckNoDelayLoadGate.run


if __name__ == "__main__":
    sys.exit(main())
