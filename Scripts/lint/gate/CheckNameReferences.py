#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
코드 · 실행 인자 · 시나리오가 **이름으로** 부르는 전역 변수와 입력 액션이 정의돼 있는지 봅니다.

런타임은 모르는 이름을 오류로 멈추지 않는다.
- 명령줄의 `-gv_<이름>` 은 모듈이 나중에 등록할 수 있어 보류표에 남고, 아무도 가져가지 않으면 App 이 기동 뒤에 경고 한 줄만 남긴다.
  시나리오의 `<Variable name="gv_…">` 은 그 게임을 올린 실기동에서야 오류가 된다. 이름을 바꾸면 스크립트 · 부하 프로필 · 시나리오가
  조용히 그 스위치를 잃는다.
- 입력 맵에 없는 액션을 물으면(`wasActionTriggered( hashed_string( "Park.Build" ) )`) 로그 없이 false 다 — 그 조작이 영영 안 된다.
데이터 쪽(씬 · 프리팹 컴포넌트 칸)의 이름 참조는 에셋 검증 규칙 `component_reference` 가 보고, 이 게이트는 코드 · 인자 쪽을 본다.

검사 규칙:
- `NameReference/GlobalVariable`: `-gv_<이름>`(소스 · 테스트 · 스크립트 · 설정 · 리소스 · CMake · CI 의 텍스트; C++ 는 문자열 안만)과
  시나리오 `<Variable name="gv_…">` 의 이름이 `SW_*GLOBAL_VARIABLE` 정의나 `registerVariable( "gv_…"` · `addArgument<…>( { "gv_…" }` 에 있다.
  `-gv_netEmu*` 처럼 `*` 가 붙은 것은 묶음을 가리키는 글이라 보지 않는다.
- `NameReference/InputAction`: 입력 맵 질의(`wasActionTriggered` · `isActionDown` · `getVector2D` …)에 문자열로 적은 액션 이름과
  `k…Action` 상수(배열 포함)의 값이 입력 맵(`*.input.xml` 의 `<action name>`)에 있다. `Source/Games/<게임>` 은 엔진 맵 + 그 게임 팩의 맵만,
  나머지 코드는 엔진 맵 + 모든 팩의 맵에서 찾는다. 게임의 팩은 그 폴더 코드가 가장 많이 부르는 `game/<팩>/` 이다.

  python Scripts/lint/gate/CheckNameReferences.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import collections
import re
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import collectSourceFiles, normalizePath  # noqa: E402
from LintGate import GateError, GateResult, LintGate  # noqa: E402

#: 전역 변수 정의 — 매크로(첫 인자 타입, 둘째 이름)와 이름 문자열로 등록하는 두 호출.
_kDefinitionMacroRe = re.compile(r"\bSW_(?:TEST_)?GLOBAL_VARIABLE(?:_SHIPPED)?\s*\(\s*[^,()]+(?:\([^()]*\))?\s*,\s*(gv_\w+)")
_kDefinitionCallRe = re.compile(r"(?:registerVariable\s*\(\s*|addArgument\s*<[^>]*>\s*\(\s*\{\s*)\"(gv_\w+)\"")
#: 실행 인자로 쓴 이름. 뒤에 `*` 가 붙으면 묶음 표기다.
_kArgumentUseRe = re.compile(r"(?<![A-Za-z0-9_])-(gv_[A-Za-z0-9_]+)(?![A-Za-z0-9_*])")
_kCppStringRe = re.compile(r'"(?:\\.|[^"\\\n])*"')
#: 시나리오의 전역 변수 단계.
_kScenarioVariableRe = re.compile(r"<Variable\b[^>]*\bname\s*=\s*\"(gv_\w+)\"")

#: 이름을 받는 입력 맵 질의 — 첫 인자를 문자열로 적은 것만 본다(`hashed_string( "X" )` 포함).
_kActionQueryRe = re.compile(
    r"\.(?:wasActionTriggered|isActionDown|wasActionPressed|wasActionReleased|getActionHoldDuration|getActionPhase|getAxis1D|getVector2D|"
    r"getActionHandle|isActionToggled|getGlyphForAction|isChordDown|wasChordTriggered)\s*\(\s*(?:hashed_string\s*\(\s*)?\"([^\"]+)\"")
#: 액션 이름 상수 — `kFooAction = "X"`, `kFooAction[] = "X"`, `kArrFooAction[] = { "A", "B" }`, `kFooActionName = "X"`.
_kActionConstantRe = re.compile(r"\bk[A-Z]\w*Action(?:Name)?\s*(?:\[\s*\])?\s*=\s*(\{[^}]*\}|\"[^\"]*\")")
#: 액션 이름의 모양(`Move` · `Park.Build`) — 같은 이름 꼴의 상수 중 데이터 낱말(`"action"` · `"discarded"`)은 액션이 아니다.
_kActionNameRe = re.compile(r"[A-Z][A-Za-z0-9]*(?:\.[A-Z][A-Za-z0-9]*)*")
_kQuotedRe = re.compile(r"\"([^\"]*)\"")
_kPackRe = re.compile(r"\bgame/([a-z0-9_]+)/")

#: `-gv_` 를 찾는 텍스트의 자리와 확장자. 문서(`docs/` · `*.md`)는 설명이라 보지 않는다.
_kArgumentScanRoots = ("Source", "Test", "Scripts", "Config", "Resource", "cmake", ".github")
_kArgumentSuffixes = {".cpp", ".h", ".inl", ".py", ".json", ".xml", ".cmake", ".txt", ".yml", ".yaml", ".ps1", ".bat", ".sh"}
_kCppSuffixes = {".cpp", ".h", ".inl"}
#: 린트 자신 — 게이트 파일이 증거 조각으로 틀린 이름을 든다.
_kLintSelfTestDir = "Scripts/lint/"


def collectDefinedGlobalVariables(repositoryRoot: Path) -> set[str]:
    """`Source` · `Test` 의 전역 변수 정의 이름."""
    unique: set[str] = set()
    listPath = collectSourceFiles([repositoryRoot / "Source", repositoryRoot / "Test"], _kCppSuffixes)
    for _, text in LintGate.readFiles(listPath, mustContain="gv_"):
        unique.update(_kDefinitionMacroRe.findall(text))
        unique.update(_kDefinitionCallRe.findall(text))
    return unique


def collectGlobalVariableUses(repositoryRoot: Path) -> list[tuple[str, str]]:
    """(위치, 이름) — 실행 인자 `-gv_…` 와 시나리오 `<Variable name>`."""
    listUse: list[tuple[str, str]] = []
    listRoot = [repositoryRoot / root for root in _kArgumentScanRoots if (repositoryRoot / root).is_dir()]
    listPath = collectSourceFiles(listRoot, _kArgumentSuffixes)
    presetPath = repositoryRoot / "CMakePresets.json"
    if presetPath.is_file():
        listPath.append(presetPath)
    for path, text in LintGate.readFiles(listPath, mustContain="gv_"):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        if relative.startswith(_kLintSelfTestDir):
            continue  # 게이트의 증거 조각(selfTestCases)은 일부러 틀린 이름을 든다
        listChunk = _kCppStringRe.findall(text) if path.suffix in _kCppSuffixes else [text]
        for chunk in listChunk:
            for name in _kArgumentUseRe.findall(chunk):
                listUse.append((relative, name))
        if path.suffix == ".xml":
            for name in _kScenarioVariableRe.findall(text):
                listUse.append((relative, name))
    return listUse


def collectInputActions(repositoryRoot: Path) -> tuple[set[str], dict[str, set[str]]]:
    """(엔진 맵의 액션, 팩 → 그 팩 맵의 액션)."""
    resourceRoot = repositoryRoot / "Resource"
    engineActions: set[str] = set()
    mapPackAction: dict[str, set[str]] = collections.defaultdict(set)
    for path in sorted(resourceRoot.rglob("*.input.xml")):
        try:
            root = ElementTree.parse(path).getroot()
        except ElementTree.ParseError as error:
            raise GateError(f"{path}: 입력 맵을 읽지 못했습니다 — {error}") from error
        names = {element.get("name", "") for element in root.iter("action")} - {""}
        parts = path.relative_to(resourceRoot).parts
        if parts[0] == "game" and len(parts) > 2:
            mapPackAction[parts[1]] |= names
        else:
            engineActions |= names
    return engineActions, mapPackAction


def collectActionNamesInCode(text: str) -> list[str]:
    """코드 한 파일에서 액션 이름으로 쓰인 문자열."""
    listName = list(_kActionQueryRe.findall(text))
    for value in _kActionConstantRe.findall(text):
        listName += [name for name in _kQuotedRe.findall(value) if _kActionNameRe.fullmatch(name)]
    return listName


def findInputActionViolations(repositoryRoot: Path) -> tuple[list[str], int]:
    engineActions, mapPackAction = collectInputActions(repositoryRoot)
    allActions = engineActions.union(*mapPackAction.values()) if mapPackAction else set(engineActions)
    listViolation: list[str] = []
    checkedCount = 0
    gamesRoot = repositoryRoot / "Source" / "Games"
    listPath = collectSourceFiles([repositoryRoot / "Source"], _kCppSuffixes)
    mapGameText: dict[str, list[tuple[str, str]]] = collections.defaultdict(list)
    for path, text in LintGate.readFiles(listPath):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        try:
            gameName = path.relative_to(gamesRoot).parts[0]
        except ValueError:
            gameName = ""
        mapGameText[gameName].append((relative, text))
    for gameName, listFile in sorted(mapGameText.items()):
        known = allActions
        scope = "어느 입력 맵"
        if gameName:
            packCounter: collections.Counter[str] = collections.Counter()
            for _, text in listFile:
                packCounter.update(_kPackRe.findall(text))
            pack = packCounter.most_common(1)[0][0] if packCounter else ""
            known = engineActions | mapPackAction.get(pack, set())
            scope = f"엔진 입력 맵과 game/{pack} 의 입력 맵"
        for relative, text in listFile:
            if "Action" not in text:
                continue
            for name in collectActionNamesInCode(text):
                checkedCount += 1
                if name not in known:
                    listViolation.append(f"[NameReference/InputAction] {relative}: 액션 '{name}' 이(가) {scope}에 없습니다")
    return listViolation, checkedCount


class CheckNameReferencesGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "코드 · 실행 인자 · 시나리오가 이름으로 부르는 전역 변수와 입력 액션이 정의돼 있는지 검사"
    buildComment = "Checking that global variable and input action names used by code, arguments and scenarios are defined..."
    timeoutSeconds = 60
    preCommitPattern = ("*.h", "*.cpp", "*.inl", "*.py", "*.json", "*.xml", "*.cmake", "*.txt", "*.yml", "*.ps1")
    preCommitFileArgument = ""
    violationHeader = "정의되지 않은 이름"
    hint = (
        "  전역 변수: 정의(SW_*GLOBAL_VARIABLE)의 이름으로 고치거나, 지운 스위치면 그 인자 · 시나리오 줄을 지웁니다.\n"
        "  입력 액션: 그 팩의 *.input.xml 에 <action name> 을 더하거나 코드의 이름을 맵의 이름으로 고칩니다."
    )
    selfTestCases = [
        {
            "name": "스크립트가 지운 전역 변수를 넘긴다",
            "files": {
                "Source/Probe/Probe.cpp": "SW_TEST_GLOBAL_VARIABLE( int32, gv_probeCount, 0, \"probe\" );\n",
                "Scripts/qa/Probe.py": "listArgument = [\"-gv_probeCount=3\", \"-gv_probeCounts=4\"]\n",
            },
        },
        {
            "name": "시나리오가 없는 전역 변수를 바꾼다",
            "files": {
                "Source/Probe/Probe.cpp": "SW_GLOBAL_VARIABLE( bool, gv_probeOn, false, \"probe\" );\n",
                "Resource/engine/automation/probe.scenario.xml": "<Scenario><Variable name=\"gv_probeOff\" value=\"1\"/></Scenario>\n",
            },
        },
        {
            "name": "게임 코드가 팩 입력 맵에 없는 액션을 묻는다",
            "files": {
                "Resource/game/probe/data/probe.input.xml": "<InputMap><action name=\"Probe.Jump\"/></InputMap>\n",
                "Source/Games/Probe/ProbeDirector.cpp": (
                    "const char* kSettings = \"game/probe/data/gamesettings.xml\";\n"
                    "bool b = inputMap.wasActionTriggered( hashed_string( \"Probe.Jmup\" ) );\n"),
            },
        },
        {
            "name": "액션 상수 배열의 이름 하나가 맵에 없다",
            "files": {
                "Resource/engine/input/default.input.xml": "<InputMap><action name=\"Group0\"/></InputMap>\n",
                "Source/Engine/Probe.cpp": "constexpr const char* kArrGroupAction[] = { \"Group0\", \"Group1\" };\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        pass

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        uniqueDefined = collectDefinedGlobalVariables(repositoryRoot)
        listUse = collectGlobalVariableUses(repositoryRoot)
        listViolation = [f"[NameReference/GlobalVariable] {location}: 전역 변수 '{name}' 의 정의가 없습니다"
                         for location, name in listUse if name not in uniqueDefined]
        actionViolation, actionCount = findInputActionViolations(repositoryRoot)
        listViolation += actionViolation
        return GateResult(listViolation=sorted(set(listViolation)),
                          summary=f"전역 변수 정의 {len(uniqueDefined)}개 · 이름 사용 {len(listUse)}곳, 코드의 입력 액션 이름 {actionCount}곳")


main = CheckNameReferencesGate.run


if __name__ == "__main__":
    sys.exit(main())
