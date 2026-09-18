#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
엔진 서비스 표와 그것을 **채우는 호스트**가 어긋나지 않는지 검사.

`Source/Engine/Common/EngineServiceList.xxx` 한 줄이 구조체 멤버 · getter · `areEngineServicesBound()` ·
`ModuleServiceId` 를 전부 만든다. 그런데 **그 표를 실제로 채우는 것은 손으로 적은 대입 22줄**이고,
그것이 호스트마다 한 벌씩 있다(`EngineLoop::initialize`, `Test/TestFramework/main.cpp`). 목록이 하나여도
채우는 곳이 둘이면 한쪽만 늘어난다.

빠뜨렸을 때 무슨 일이 나는지가 이 검사의 이유다. `required=1` 인 서비스가 하나라도 비면
`areEngineServicesBound()` 가 **영영 false** 가 되고, 그 함수로 게이팅되는 경로가 통째로 무력화된다 —
실제로 그렇게 셰이더 캐시를 건너뛰고 런타임 컴파일러를 직접 부르다가 (배포본에 DXC 가 없어서) 죽은 적이
있다(`EngineServiceList.xxx` 의 CommandStack 주석). 증상이 "빌드가 아니라 실행에서, 그것도 엉뚱한
자리에서" 나타나므로 컴파일러는 도와주지 않는다.

강제 규칙 — 셋이다:
  1) `owned=1` 인 행은 `EngineOwnedServices` 가 채운다. 그러니 호스트는 **그 저장소의 `bindInto(`** 를
     부르거나, 부르지 않겠다면 그 멤버들을 직접 대입해야 한다.
  2) `owned=0` 이면서 `required=1` 인 행(팩토리·구성별 조건부)은 호스트가 **직접** 대입해야 한다.
  3) 표에 없는 멤버를 대입하면 안 된다 (이름이 바뀐 뒤 남은 죽은 줄).

**호스트 목록을 적지 않는다** — `bindEngineServices(` 를 부르는 파일이 곧 호스트다. 새 호스트(도구·하네스)가
생겨도 자동으로 검사 대상이 된다.

선택(`required=0` · `SW_ENGINE_SERVICE_OPT`)은 강제하지 않는다. 그것이 선택인 이유가 "이 호스트에는 없을
수 있다" 이기 때문이다(Shipping 의 CommandStack, 툴의 RenderTargetRegistry).

  python Scripts/lint/gate/CheckEngineServiceBinding.py [--root <repo>]
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from LintGate import GateError, GateResult, LintGate  # noqa: E402

_kServiceListPath = "Source/Engine/Common/EngineServiceList.xxx"
# 호스트를 찾을 곳. `bindEngineServices(` 를 부르는 파일이 호스트다 — 이름을 적지 않는다.
_kHostSearchRoot = ("Source", "Test", "Tools")

_kRequiredRow = re.compile(
    r"^\s*SW_ENGINE_SERVICE(?P<const>_CONST)?\s*\(\s*(?P<member>_p\w+)\s*,[^,]+,[^,]+,[^,]+,"
    r"\s*(?P<required>[01])\s*,[^,]+,\s*(?P<owned>[01])\s*\)",
    re.M,
)
_kOptionalRow = re.compile(r"^\s*SW_ENGINE_SERVICE_OPT\s*\(\s*(?P<member>_p\w+)\s*,", re.M)
_kBindCall = re.compile(r"\bbindEngineServices\s*\(")
# 생성된 저장소가 owned=1 을 대신 꽂아 주는 자리 (`EngineOwnedServices::bindInto`).
_kGeneratedBindCall = re.compile(r"\bbindInto\s*\(")
_kAssignment = re.compile(r"\.(?P<member>_p\w+)\s*=")


def readServiceRows(rootDir: Path) -> tuple[list[str], list[str], set[str]]:
    """(호스트가 직접 채워야 하는 필수 멤버, 저장소가 채우는 필수 멤버, 표에 있는 모든 멤버)."""
    path = rootDir / _kServiceListPath
    if path.exists() is False:
        raise GateError(f"{_kServiceListPath} 가 없습니다 — 서비스 표가 옮겨졌다면 이 검사도 같이 옮기세요.")

    text = path.read_text(encoding="utf-8", errors="ignore")
    listHandFilled: list[str] = []
    listStorageFilled: list[str] = []
    setKnown: set[str] = set()
    for match in _kRequiredRow.finditer(text):
        member = match.group("member")
        setKnown.add(member)
        if match.group("required") != "1":
            continue
        if match.group("owned") == "1":
            listStorageFilled.append(member)
        else:
            listHandFilled.append(member)
    for match in _kOptionalRow.finditer(text):
        setKnown.add(match.group("member"))

    if not listHandFilled and not listStorageFilled:
        raise GateError(f"{_kServiceListPath} 에서 required=1 인 줄을 하나도 읽지 못했습니다 — 검사가 헛돌고 있습니다.")
    return listHandFilled, listStorageFilled, setKnown


def findBindingHosts(rootDir: Path) -> list[Path]:
    """`bindEngineServices(` 를 부르는 소스 파일들. 선언만 있는 헤더·정의 파일은 뺀다."""
    listHost: list[Path] = []
    for folder in _kHostSearchRoot:
        base = rootDir / folder
        if base.exists() is False:
            continue
        for path in base.rglob("*.cpp"):
            text = path.read_text(encoding="utf-8", errors="ignore")
            if _kBindCall.search(text) is None:
                continue
            # 정의 자신(EngineServices.cpp)은 채우는 쪽이 아니다.
            if path.name == "EngineServices.cpp":
                continue
            listHost.append(path)
    return sorted(listHost)


def checkHosts(rootDir: Path) -> tuple[list[str], int]:
    listHandFilled, listStorageFilled, setKnown = readServiceRows(rootDir)
    listHost = findBindingHosts(rootDir)
    if not listHost:
        raise GateError("bindEngineServices 를 부르는 파일을 하나도 찾지 못했습니다 — 검사가 헛돌고 있습니다.")

    errors: list[str] = []
    for host in listHost:
        text = host.read_text(encoding="utf-8", errors="ignore")
        setAssigned = {match.group("member") for match in _kAssignment.finditer(text)}
        bUsesStorage = _kGeneratedBindCall.search(text) is not None
        relPath = host.relative_to(rootDir).as_posix()

        for member in listHandFilled:
            if member in setAssigned:
                continue
            errors.append(
                f"{relPath}: 필수 서비스 `{member}` 를 채우지 않습니다 — 목록에 owned=0 으로 적혀 있어 "
                f"**호스트가 직접** 꽂아야 합니다(팩토리·구성별 조건부). 빠지면 areEngineServicesBound() 가 "
                f"영영 false 가 되고 그 함수로 게이팅되는 경로가 통째로 죽습니다"
            )

        if bUsesStorage:
            continue

        for member in listStorageFilled:
            if member in setAssigned:
                continue
            errors.append(
                f"{relPath}: `EngineOwnedServices::bindInto()` 도 부르지 않고 필수 서비스 `{member}` 도 "
                f"채우지 않습니다 — 저장소를 쓰거나(권장) 목록의 owned=1 행을 전부 직접 꽂아야 합니다"
            )
        for member in sorted(setAssigned - setKnown):
            errors.append(
                f"{relPath}: 표에 없는 멤버 `{member}` 에 대입합니다 — "
                f"{_kServiceListPath} 에서 빠졌거나 이름이 바뀐 줄입니다"
            )
    return errors, len(listHost)


class CheckEngineServiceBindingGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있어 어긋날 수 없다."""

    description = "엔진 서비스 표와 바인딩 호스트 대조"
    buildComment = "Checking every host fills the required engine services..."
    timeoutSeconds = 15
    preCommitPattern = ("*.cpp", "*.xxx")
    selfTestCases = [
        {
            "name": "호스트가 필수 서비스를 빠뜨림",
            "files": {
                _kServiceListPath: (
                    "SW_ENGINE_SERVICE( _pTaskManager, class, TaskManager, getTaskManager, 1, 0, 0 )\n"
                    "SW_ENGINE_SERVICE( _pSceneManager, class, SceneManager, getSceneManager, 1, 1, 0 )\n"
                ),
                "Source/App/Probe.cpp": (
                    "void probe()\n"
                    "{\n"
                    "    EngineServices services{};\n"
                    "    services._pTaskManager = nullptr;\n"
                    "    engine::bindEngineServices( services );\n"
                    "}\n"
                ),
            },
        },
        {
            "name": "표에서 사라진 멤버에 대입",
            "files": {
                _kServiceListPath: "SW_ENGINE_SERVICE( _pTaskManager, class, TaskManager, getTaskManager, 1, 0, 0 )\n",
                "Source/App/Probe.cpp": (
                    "void probe()\n"
                    "{\n"
                    "    EngineServices services{};\n"
                    "    services._pTaskManager = nullptr;\n"
                    "    services._pGoneManager = nullptr;\n"
                    "    engine::bindEngineServices( services );\n"
                    "}\n"
                ),
            },
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        errors, hostCount = checkHosts(repositoryRoot)
        return GateResult(
            listViolation=errors,
            summary=f"{hostCount} binding hosts checked",
        )


main = CheckEngineServiceBindingGate.run


if __name__ == "__main__":
    sys.exit(main())
