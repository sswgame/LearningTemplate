#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
어떤 린트가 있고 어떻게 돌리는가 — **한 곳**.

`gate/` 에 파일을 놓는 것이 규칙이라고 해 놓고, 정작 CMake 쪽은 목록을 손으로 들고 있었다.
린트 하나를 더하려면 네 곳을 고쳐야 했다:

1. `lint/gate/` 에 파일을 놓는다                           ← 여기까지가 "자리가 규칙"
2. `Scripts/common/Constants.py` 에 `kScriptLintCheckXxx` 경로 상수를 더한다
3. `GenerateCMakeConstants.py` 에 `set(SW_SCRIPT_LINT_CHECK_XXX ...)` 한 줄을 더한다
4. `cmake/Engine/AssetAndToolTargets.cmake` 에 `sw_addRepoPythonTarget` 한 덩이와
   `add_test` + `set_tests_properties` 한 덩이를 더한다 (11개가 그렇게 쌓여 135줄이었다)

2~4 는 1 에서 **기계적으로 유도되는 것**이다. 그래서 여기서 유도하고, CMake 는 그 결과를
`include()` 한다(`Scripts/setup/GenerateLintTargets.py` → `generated/sw/config/LintTargets.cmake`).
이 저장소가 이미 `Constants.py` → `ConfigVars.cmake` 로 하고 있는 방식 그대로다.

린트마다 다른 것(설명 · 타임아웃 · 추가 인자)은 **린트 자신이 든다** — 게이트는 `LintGate`
클래스 속성으로, 셀프테스트는 모듈 상수(`kLint*`)로. 표를 여기 모으면 그 표가 또 어긋난다.
"""

from __future__ import annotations

import importlib
import sys
from dataclasses import dataclass
from pathlib import Path
from types import ModuleType

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from LintGate import LintGate, findGateClass  # noqa: E402

#: CMake 가 타깃·CTest 로 등록하는 린트가 사는 폴더 (`lint/` 기준).
#: `fixer/` 와 `report/` 는 빌드를 막지 않으므로 등록하지 않는다 — 폴더가 곧 성격이다(CLAUDE.md).
kRegisteredFolder = ("gate", "selftest")

#: 타임아웃을 따로 적지 않은 린트의 기본값(초).
kDefaultTimeoutSeconds = 30

_kLintDir = Path(__file__).resolve().parent


@dataclass(frozen=True)
class LintScript:
    """폴더에서 찾은 린트 파일 하나 — 아직 메타데이터를 요구하지 않은 상태."""

    name: str
    folderName: str
    relPath: str
    scriptPath: Path
    module: ModuleType

    @property
    def gateClass(self) -> type[LintGate] | None:
        return findGateClass(self.module)


@dataclass(frozen=True)
class LintTarget:
    """
    CMake 가 린트 하나를 등록하는 데 필요한 전부.

    - `name`             : CTest 이름이자 커스텀 타깃 이름. 파일 이름 그대로다.
    - `scriptRelPath`    : 저장소 루트 기준 스크립트 경로.
    - `buildComment`     : ninja 가 이 타깃을 만들 때 찍는 줄. **영어다** — 이 저장소의 빌드
                           출력은 전부 영어이고, Windows 콘솔 코드페이지에서 한글이 깨진 전례가 있다.
    - `timeoutSeconds`   : CTest TIMEOUT.
    - `listCtestArgument`: `--root` 말고 더 필요한 인자. CMake 변수 참조(`${CMAKE_BINARY_DIR}`)를
                           그대로 적어 둔다 — 생성 결과가 CMake 파일이라 거기서 풀린다.
    """

    name: str
    scriptRelPath: str
    buildComment: str
    timeoutSeconds: int
    listCtestArgument: tuple[str, ...]


def discoverLintScripts(folderName: str = "") -> list[LintScript]:
    """
    등록 대상 린트 파일을 전부(또는 한 폴더만) 찾습니다. **메타데이터를 요구하지 않는다** —
    "거기 무엇이 있는가" 와 "그것이 제대로 선언되었는가" 는 다른 질문이고, 후자는 부르는 쪽이 본다.

    **목록이 아니라 자리가 규칙이다** — 폴더를 훑는다.
    """
    listScript: list[LintScript] = []
    for folder in (folderName,) if folderName else kRegisteredFolder:
        for scriptPath in sorted((_kLintDir / folder).glob("*.py")):
            if scriptPath.stem == "__init__":
                continue
            name = scriptPath.stem
            listScript.append(
                LintScript(
                    name=name,
                    folderName=folder,
                    relPath=f"Scripts/lint/{folder}/{name}.py",
                    scriptPath=scriptPath,
                    module=importlib.import_module(f"{folder}.{name}"),
                )
            )
    return listScript


def makeLintTarget(script: LintScript) -> LintTarget:
    """
    린트 파일 하나에서 CMake 등록 정보를 뽑습니다. 선언이 없으면 **그 자리에서 멈춘다** —
    설명 없는 타깃은 빌드 출력에서 정체를 알 수 없고, 타임아웃 없는 CTest 는 매달린다.
    """
    gateClass = script.gateClass
    if gateClass is not None:
        return LintTarget(
            name=script.name,
            scriptRelPath=script.relPath,
            buildComment=gateClass.buildComment or f"Running {script.name}...",
            timeoutSeconds=gateClass.timeoutSeconds,
            listCtestArgument=tuple(gateClass.listCtestArgument),
        )

    buildComment = getattr(script.module, "kLintBuildComment", "")
    if not buildComment:
        raise ValueError(
            f"{script.relPath}: `kLintBuildComment` 가 없습니다 — CMake 가 이 린트를 무슨 이름으로 "
            f"찍을지 알 수 없습니다 (게이트라면 `LintGate` 를 상속하세요)"
        )
    return LintTarget(
        name=script.name,
        scriptRelPath=script.relPath,
        buildComment=buildComment,
        timeoutSeconds=int(getattr(script.module, "kLintTimeoutSeconds", kDefaultTimeoutSeconds)),
        listCtestArgument=tuple(getattr(script.module, "kLintCtestArguments", ())),
    )


def discoverLintTargets(folderName: str = "") -> list[LintTarget]:
    """등록 대상 린트를 찾아 CMake 등록 정보까지 만들어 돌려줍니다."""
    return [makeLintTarget(script) for script in discoverLintScripts(folderName)]
