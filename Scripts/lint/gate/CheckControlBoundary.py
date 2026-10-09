#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
입력 · 매핑 · 행동 세 층의 경계를 지킨다 — GameFramework · Games 에서 입력(InputManager · InputMap)을 읽어도 되는 파일은 정해져 있다.

층(Source/GameFramework/README.md "조종 — 폰, 조종자, 의도"):
  - 입력 층(Engine/Input)은 장치 사건만 안다.
  - 매핑 층(InputMap)을 읽는 것은 **플레이어 조종자** · 플레이어 뷰 카메라 · 명령형 게임의 디렉터(명령 조종자)뿐이다.
  - 폰(몸 · 이동 · 탈것)은 `ControlIntent` 만 읽는다 — 플레이어 · AI · 네트워크 · 리플레이가 같은 의도로 몬다.

그래서 `Engine/Input/InputManager.h` · `Engine/Input/InputMap.h` 를 include 하거나 `getService<InputManager>` · `getInputMap()` 을 부르는
파일은 아래 허용 표에 이유와 함께 있어야 한다. 폰 쪽 파일(`Base/Control` · `Base/Vehicle` 의 `*MovementComponent*` · `*VehicleComponent*` ·
`Pawn*`)은 허용 표에 넣을 수도 없다 — 경계의 핵심이다.

허용 표에 든 파일도 **입력 층을 직접 묻지 않는다**(언리얼 Enhanced Input — 게임 코드는 Input Action 만 읽는다): 키 · 마우스 버튼 · 휠 ·
이동량 · 패드 조회(`isKeyDown` · `wasMouseButtonPressed` · `getMouseWheel` · `getMouseDelta` · `getMouse()` · `getGamepad()` …)는 GameFramework ·
Games 어디서든 위반이고, 클릭 · 끌기 · 시점 · 확대는 입력 맵 액션(`Camera.Look` · `Camera.Zoom` · `Skirmish.Select` …)으로 읽는다.
남는 장치 조회는 **커서 화면 위치 하나** — `getMousePositionNormalized()`(언리얼 `GetMousePosition` 자리, 커서 아래 땅 고르기)뿐이다.

  python Scripts/lint/gate/CheckControlBoundary.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import fnmatch
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import normalizePath  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kListScanRoot = ("Source/GameFramework", "Source/Games")

# 입력을 읽는 모양 — include 둘과 조회 둘.
_kInputReadRe = re.compile(
    r'#\s*include\s*"Engine/Input/(?:InputManager|InputMap)\.h"'
    r"|\bgetService\s*<\s*InputManager\s*>"
    r"|\bgetInputMap\s*\(\s*\)"
)

# 입력 층(장치)을 직접 묻는 조회 — `InputManager` 의 키 · 버튼 · 휠 · 이동량 · 패드 창구와 장치 꺼내기. 액션이 아니라 장치를 읽는다.
# 커서 화면 위치(`getMousePositionNormalized`)만 뺀다 — 이름이 `getMousePosition` 으로 시작해도 `(` 가 바로 붙어야 걸린다.
_kListRawInputQuery = (
    "isKeyDown", "wasKeyPressed", "wasKeyReleased",
    "isMouseButtonDown", "wasMouseButtonPressed", "wasMouseButtonReleased",
    "getMousePosition", "getMouseDelta", "getMouseWheel", "isPointerOverRect",
    "getGamepadLeftTrigger", "getGamepadRightTrigger",
    "getKeyboard", "getMouse", "getGamepad", "getDevice",
    "getLastFrameEvents", "wasAnyInputPressed",
)
_kRawInputQueryRe = re.compile(r"(?:\.|->)\s*(" + "|".join(_kListRawInputQuery) + r")\s*\(")

# 허용 표에 들 수 없는 폰 쪽 파일.
_kListPawnSidePattern = (
    "Source/GameFramework/Base/Actor/Control/*MovementComponent*",
    "Source/GameFramework/Base/Actor/Control/*VehicleComponent*",
    "Source/GameFramework/Base/Actor/Control/Pawn*",
    "Source/GameFramework/Base/Gameplay/Vehicle/*MovementComponent*",
    "Source/GameFramework/Base/Gameplay/Vehicle/*VehicleComponent*",
    "Source/GameFramework/Base/Gameplay/Vehicle/Pawn*",
)


def isPawnSideInternal(relative: str) -> bool:
    return any(fnmatch.fnmatch(relative, pattern) for pattern in _kListPawnSidePattern)


def checkTableInternal() -> list[str]:
    """허용 표가 폰 쪽 파일을 덮으면 그 자체가 위반이다(표를 넓혀 경계를 지우지 못하게)."""
    violations: list[str] = []
    for pattern in CheckControlBoundaryGate.mapExemption:
        for pawnPattern in _kListPawnSidePattern:
            probe = pawnPattern.replace("*", "Probe")
            if fnmatch.fnmatch(probe, pattern):
                violations.append(f"[Control Boundary] allow-list entry '{pattern}' covers pawn-side files ('{pawnPattern}')")
    return violations


def findViolations(repositoryRoot: Path, listTargetFile: list[str] | None) -> list[str]:
    gate = CheckControlBoundaryGate
    violations = checkTableInternal()
    listPath = LintGate.selectTargetFiles(repositoryRoot, listTargetFile, listScanRoot=_kListScanRoot, suffixes=(".cpp", ".h", ".inl"))
    for path, text in LintGate.readFiles(listPath):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        allowKey = None if isPawnSideInternal(relative) else gate.findExemptionKey(relative)
        if allowKey is not None:
            gate.seeExemption(allowKey)
        for lineIndex, line in enumerate(text.splitlines(), start=1):
            rawMatch = _kRawInputQueryRe.search(line)
            if rawMatch:
                violations.append(f"[Control Boundary] {relative}:{lineIndex}: reads the input device ({rawMatch.group(1)}) — read an InputMap action "
                                  f"instead (only the cursor position, getMousePositionNormalized, is a device query): {line.strip()}")
                continue
            if not _kInputReadRe.search(line):
                continue
            if allowKey is not None:
                gate.useExemption(allowKey)
                continue
            violations.append(f"[Control Boundary] {relative}:{lineIndex}: pawn-side code reads input — produce a ControlIntent in a controller "
                              f"instead: {line.strip()}")
    return violations


class CheckControlBoundaryGate(LintGate):
    """입력을 읽는 파일은 허용 표에만 — 폰은 의도만 읽는다."""

    #: 입력을 읽어도 되는 파일(fnmatch, 저장소 상대 경로) → 이유.
    mapExemption = {
        "Source/GameFramework/Base/Actor/Control/PlayerControllerComponent.*": "플레이어 조종자 — 입력 → 매핑 → 의도를 만드는 유일한 조종자",
        "Source/GameFramework/Base/Actor/Control/ControlSystem.*": "조종 시스템 — 플레이어 조종자에게 입력 관리자를 건넨다(스스로 액션을 읽지 않는다)",
        "Source/GameFramework/Base/Actor/Camera/*": "플레이어 뷰 카메라(시점 고르기 · 팬 · 줌) — 폰이 아니다",
        "Source/GameFramework/Base/Foundation/Framework/GameInstanceBase.*": "입력 맵 파일을 싣는다(매핑 층을 세움)",
        "Source/GameFramework/Base/Foundation/Data/GameSettings.h": "입력 맵 경로 설정",
        "Source/Games/NileCity/NileDirectorComponent.*": "명령 조종자 — 경영 게임은 폰이 없다(입력 → 키트 명령)",
        "Source/Games/StarSkirmish/SkirmishDirectorComponent.*": "명령 조종자 — RTS 는 폰이 없다(입력 → RtsWorld 명령)",
        "Source/Games/ThemeParkTycoon/ParkDirectorComponent.*": "명령 조종자 — 경영 게임은 폰이 없다(입력 → 공원 명령)",
        "Source/Games/MeadowVillage/MeadowFarmDirectorComponent.*": "명령 조종자 — 조립 시험 마을의 시간 빨리 감기(게임 규칙 명령)",
        "Source/Games/MeadowVillage/MeadowTownDirectorComponent.*": "명령 조종자 — 조립 시험 마을의 말 걸기(대화 명령)",
    }

    description = ("GameFramework · Games 에서 입력(InputManager · InputMap)을 읽는 파일이 허용 표(플레이어 조종자 · 플레이어 뷰 · 명령 조종자)에 있는지, "
                   "그리고 어디서도 장치(키 · 버튼 · 휠 · 이동량 · 패드)를 직접 묻지 않는지 검사")
    buildComment = "Checking that only controllers, player views and command directors read input..."
    timeoutSeconds = 15
    preCommitPattern = ("Source/GameFramework/*", "Source/Games/*")
    preCommitFileArgument = "--files"
    violationHeader = "폰 쪽 코드가 입력을 읽는다"
    hint = (
        "  폰(몸 · 이동 · 탈것)은 PawnComponent::getIntent() 의 ControlIntent 만 읽습니다.\n"
        "  입력 → 액션 → 의도는 PlayerControllerComponent 가 만들고, AI 는 AiControllerComponent 로 같은 의도를 냅니다.\n"
        "  명령형 장르의 디렉터 · 플레이어 뷰 카메라처럼 정말 입력을 읽어야 하면 이 게이트의 mapExemption 에 이유와 함께 한 줄.\n"
        "  허용 파일도 장치를 직접 묻지 않습니다 — 클릭 · 끌기 · 시점 · 확대는 입력 맵(*.input.xml)에 액션을 두고 InputMap 으로 읽고,\n"
        "  커서 화면 위치만 InputManager::getMousePositionNormalized() 로 읽습니다."
    )
    selfTestCases = [
        {
            "name": "게임의 몸 컴포넌트가 입력 맵을 읽음",
            "files": {
                "Source/Games/Probe/FooPlayerComponent.cpp": (
                    "void FooPlayerComponent::onTick()\n"
                    "{\n"
                    "    pInput->getInputMap().isActionDown( \"Jump\" );\n"
                    "}\n"
                ),
            },
        },
        {
            "name": "기반 폰 이동이 InputManager 를 include",
            "files": {
                "Source/GameFramework/Base/Actor/Control/ProbeMovementComponent.cpp": "#include \"Engine/Input/InputManager.h\"\n",
            },
        },
        {
            "name": "허용 표의 명령 조종자가 마우스 버튼을 직접 물음",
            "files": {
                "Source/Games/StarSkirmish/SkirmishDirectorComponent.cpp": (
                    "void SkirmishDirectorComponent::updateDrag( const InputManager& input )\n"
                    "{\n"
                    "    if ( input.wasMouseButtonPressed( MouseButton::Left ) )\n"
                    "        startDrag();\n"
                    "}\n"
                ),
            },
        },
        {
            "name": "허용 표의 플레이어 뷰 카메라가 휠을 직접 물음",
            "files": {
                "Source/GameFramework/Base/Actor/Camera/ProbeCameraComponent.cpp": "    zoom( pInput->getMouseWheel() );\n",
            },
        },
        {
            "name": "허용 표 밖 게임 코드가 장치에서 패드를 꺼냄",
            "files": {
                "Source/Games/Probe/ProbeComponent.cpp": "    const GamepadDevice* pPad = pInput->getGamepad( 0 );\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 GameFramework · Games 전체)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        violations = findViolations(repositoryRoot, args.files)
        return GateResult(listViolation=violations, summary="GameFramework · Games 의 입력 읽기")


main = CheckControlBoundaryGate.run


if __name__ == "__main__":
    sys.exit(main())
