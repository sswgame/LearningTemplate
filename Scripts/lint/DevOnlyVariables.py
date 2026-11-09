#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/DevOnlyVariables.py

테스트용 전역 변수(`SW_TEST_GLOBAL_VARIABLE` · `SW_EXTERN_TEST_GLOBAL_VARIABLE`)는 개발 전용 자리에서만 선언한다 — 기능 폴더의 `Dev/` · `Tests/`
(`SW_DEV_ONLY_SOURCE_PATTERNS`, 배포 구성이 짓지 않는다), `Source/DevTools/`(자동화 실행기 `DevTools/Client/Automation` 포함), `Source/Editor/`.

**왜 린트인가.** Shipping 은 이 매크로를 등록 없는 상수로 접어 읽는 분기가 빠지지만, 그것은 안전망이다. 런타임 클래스가 테스트 변수를 직접 읽으면
개발 기능이 런타임 코드 안에 남는다(인과가 뒤집힌다 — 사용자 결정 2026-11-08). 런타임은 일반 창구(`RenderDiagnostics` · `DeterministicRun` ·
프레임 관찰자 · 렌더 리스너 · 개발 부분 등록)만 두고 개발 쪽이 스스로 붙는다. 매크로 자체는 합법인 C++ 이라 형식으로 자리를 막지 못한다.
배포 바이너리에 이름이 남는지는 `shipping-check` 의 NoTestVariables 가 본다 — 이 검사는 커밋 때 자리를 먼저 잡는다.

예외 표는 없다 — 개발 기능을 개발 자리로 옮긴다.
`py -3 -m Scripts lint dev-only-variables [--self-check]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402

kExtensions = (".h", ".cpp", ".inl")
kDeclPattern = re.compile(r"^\s*SW_(?:EXTERN_)?TEST_GLOBAL_VARIABLE\(\s*[^,]+,\s*(\w+)", re.M)
# `SW_DEV_ONLY_SOURCE_PATTERNS`(cmake/Engine/UnbuiltSources.cmake)와 같은 폴더 이름 + 개발 전용 대상
kDevParts = ("/Dev/", "/Tests/")
kDevRoots = ("Source/DevTools/", "Source/Editor/")
kMacroHome = "Source/Core/GlobalVariable/GlobalVariableManager.h"


def isDevOnlyPath(relPath: str) -> bool:
    path = "/" + relPath
    return relPath == kMacroHome or relPath.startswith(kDevRoots) or any(part in path for part in kDevParts)


def findDeclarations(text: str) -> list[str]:
    return kDeclPattern.findall(text)


def iterSources(projectRoot: Path) -> Iterable[Path]:
    for path in (projectRoot / "Source").rglob("*"):
        if path.suffix in kExtensions and "ThirdParty" not in path.parts:
            yield path


def checkDevOnlyVariables(projectRoot: Path) -> list[str]:
    listProblem: list[str] = []
    for path in iterSources(projectRoot):
        relPath = path.relative_to(projectRoot).as_posix()
        if isDevOnlyPath(relPath):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for name in findDeclarations(text):
            listProblem.append(f"{relPath}: 테스트용 변수 {name} 이 런타임 코드에 있다 — 기능과 함께 Dev/ · DevTools 로 옮기고 런타임에는 일반 창구만 둔다")
    return listProblem


def selfCheck() -> bool:
    assert findDeclarations('    SW_TEST_GLOBAL_VARIABLE( bool, gv_x, false, "d" );') == ["gv_x"]
    assert findDeclarations('SW_EXTERN_TEST_GLOBAL_VARIABLE( int32, gv_y );') == ["gv_y"]
    assert findDeclarations('#define SW_TEST_GLOBAL_VARIABLE( type, name ) x') == []  # 매크로 정의 줄은 선언이 아니다
    assert findDeclarations('SW_GLOBAL_VARIABLE( bool, gv_z, false, "d" );') == []
    assert isDevOnlyPath("Source/DevTools/Client/Audio/AudioDumpListener.cpp")
    assert isDevOnlyPath("Source/Engine/Audio/Tests/TestAudio.cpp")
    assert isDevOnlyPath("Source/DevTools/Client/Render/RenderDiagnosticsSwitches.cpp")
    assert isDevOnlyPath("Source/DevTools/Client/Automation/Runner/AutomationSession.cpp")
    assert isDevOnlyPath(kMacroHome)
    assert not isDevOnlyPath("Source/Engine/Audio/IAudioService.cpp")
    assert not isDevOnlyPath("Source/Engine/Automation/Watch/AutomationWatch.cpp")
    assert not isDevOnlyPath("Source/Games/Shooter3D/DevilComponent.cpp")
    print("[dev-only-variables] self-check OK")
    return True


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[3] if __doc__ else None)
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args(argv)
    if args.self_check:
        return ExitCode.Passed if selfCheck() else ExitCode.Violation
    listProblem = checkDevOnlyVariables(getProjectRoot())
    for line in listProblem:
        print(f"  - {line}")
    print(f"[dev-only-variables] {'FAIL' if listProblem else 'OK'} ({len(listProblem)})")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
