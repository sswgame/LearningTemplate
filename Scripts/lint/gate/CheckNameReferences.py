#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
코드 · 실행 인자 · 시나리오가 **이름으로** 부르는 전역 변수 · 입력 액션 · 오디오 이벤트 · 시나리오 단계 · 탐침 · 개발 명령이 정의돼 있는지 봅니다.

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
- `NameReference/AudioEvent`: `postEvent` · `postEventAt` 에 문자열로 적은 이벤트 이름이 `*.audioevents.xml` 의 `<AudioEventDesc _name>` 에 있다
  (팩 범위는 입력 액션과 같다). 런타임은 모르는 이벤트를 오류 한 줄로 알리고 소리를 내지 않는다.
코드 쪽 두 규칙은 주석을 지운 글만 본다(문서 주석의 예는 코드가 아니다). 이름 종류는 `_kListCodeNameKind` 의 한 줄이다.
- `NameReference/ScenarioStep` · `ScenarioProbe` · `DevCommand`: 시나리오(`*.scenario.xml`)의 `<At>` 안 단계 이름, `probe` 속성,
  `<DevCommand line>` 의 첫 낱말이 `SW_AUTOMATION_STEP` · `SW_AUTOMATION_PROBE` · `SW_DEV_COMMAND` 로 등록된 이름이다. 실행기는 이것을
  시나리오를 올린 실기동(hostgpu 의 AppScenarioTest)에서야 오류로 낸다 — CI(nogpu)는 단계 · 명령 이름을 바꿔도 통과했다.

  python Scripts/lint/gate/CheckNameReferences.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import collections
import re
import sys
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass
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
_kStringOrCommentRe = re.compile(r'"(?:\\.|[^"\\\n])*"|//[^\n]*|/\*.*?\*/', re.S)
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
#: 이름으로 오디오 이벤트를 올리는 호출(`GameSound::postEvent` · `AudioEngine::postEvent` · `postEventAt`).
_kAudioEventCallRe = re.compile(r"\bpostEvent(?:At)?\s*\(\s*(?:hashed_string\s*\(\s*)?\"([^\"]+)\"")
#: 시나리오 단계 · 탐침 · 개발 명령의 등록 — 둘째 인자가 이름(리터럴 또는 `k…Kind` 상수)이다.
_kStepRegistrationRe = re.compile(r'SW_AUTOMATION_STEP(?:_WITH_HOOKS)?\s*\(\s*\w+\s*,\s*"([^"]+)"')
_kStepRegistrationByConstantRe = re.compile(r"SW_AUTOMATION_STEP(?:_WITH_HOOKS)?\s*\(\s*\w+\s*,\s*(?:\w+::)?(k\w+Kind)\b")
_kStepKindConstantRe = re.compile(r'\b(k\w+Kind)\s*(?:\[\s*\])?\s*=\s*"([^"]+)"')
_kProbeRegistrationRe = re.compile(r'SW_AUTOMATION_(?:QUERY_)?PROBE\s*\(\s*\w+\s*,\s*"([^"]+)"')
_kDevCommandRegistrationRe = re.compile(r'SW_DEV_COMMAND\s*\(\s*\w+\s*,\s*"([^"]+)"')
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


@dataclass(frozen=True)
class CodeNameKind:
    """코드가 문자열로 부르는 이름 한 종류 — 어느 호출 · 상수에서 줍고, 어느 데이터 파일의 어느 원소 · 속성에 정의되는가."""

    rule: str
    label: str
    listCodeRe: tuple[re.Pattern[str], ...]   # 첫 그룹이 이름 하나
    constantRe: re.Pattern[str] | None       # 첫 그룹이 `"X"` 또는 `{ "A", "B" }`
    dataGlob: str
    element: str
    attribute: str
    marker: str                               # 이 글이 없는 파일은 읽지 않는다


_kListCodeNameKind: tuple[CodeNameKind, ...] = (
    CodeNameKind(rule="InputAction", label="입력 맵 액션", listCodeRe=(_kActionQueryRe,), constantRe=_kActionConstantRe,
                 dataGlob="*.input.xml", element="action", attribute="name", marker="Action"),
    CodeNameKind(rule="AudioEvent", label="오디오 이벤트", listCodeRe=(_kAudioEventCallRe,), constantRe=None,
                 dataGlob="*.audioevents.xml", element="AudioEventDesc", attribute="_name", marker="Event"),
)


def collectDataNames(repositoryRoot: Path, kind: CodeNameKind) -> tuple[set[str], dict[str, set[str]]]:
    """(팩 밖 데이터의 이름, 팩 → 그 팩 데이터의 이름)."""
    resourceRoot = repositoryRoot / "Resource"
    uniqueShared: set[str] = set()
    mapPackName: dict[str, set[str]] = collections.defaultdict(set)
    for path in sorted(resourceRoot.rglob(kind.dataGlob)):
        try:
            root = ElementTree.parse(path).getroot()
        except ElementTree.ParseError as error:
            raise GateError(f"{path}: {kind.label} 데이터를 읽지 못했습니다 — {error}") from error
        names = {element.get(kind.attribute, "") for element in root.iter(kind.element)} - {""}
        parts = path.relative_to(resourceRoot).parts
        if parts[0] == "game" and len(parts) > 2:
            mapPackName[parts[1]] |= names
        else:
            uniqueShared |= names
    return uniqueShared, mapPackName


def stripCommentsInternal(text: str) -> str:
    """C++ 주석을 지웁니다(문자열 안의 `//` 는 남긴다). 문서 주석의 예(`postEvent( "Footstep" )`)는 코드가 아니다."""
    return _kStringOrCommentRe.sub(lambda match: match.group(0) if match.group(0).startswith('"') else " ", text)


def collectCodeNames(text: str, kind: CodeNameKind) -> list[str]:
    """코드 한 파일(주석을 지운 글)에서 이 종류의 이름으로 쓰인 문자열."""
    listName: list[str] = []
    for codeRe in kind.listCodeRe:
        listName += codeRe.findall(text)
    if kind.constantRe is not None:
        for value in kind.constantRe.findall(text):
            listName += [name for name in _kQuotedRe.findall(value) if _kActionNameRe.fullmatch(name)]
    return listName


def collectSourceByGame(repositoryRoot: Path) -> dict[str, list[tuple[str, str]]]:
    """게임 폴더 이름(게임 밖은 "") → (위치, 글) 목록."""
    gamesRoot = repositoryRoot / "Source" / "Games"
    mapGameText: dict[str, list[tuple[str, str]]] = collections.defaultdict(list)
    for path, text in LintGate.readFiles(collectSourceFiles([repositoryRoot / "Source"], _kCppSuffixes)):
        try:
            gameName = path.relative_to(gamesRoot).parts[0]
        except ValueError:
            gameName = ""
        mapGameText[gameName].append((normalizePath(str(path.relative_to(repositoryRoot))), text))
    return mapGameText


def findPackOfGame(listFile: list[tuple[str, str]]) -> str:
    """게임 폴더 코드가 가장 많이 부르는 `game/<팩>/` 의 팩 이름."""
    packCounter: collections.Counter[str] = collections.Counter()
    for _, text in listFile:
        packCounter.update(_kPackRe.findall(text))
    return packCounter.most_common(1)[0][0] if packCounter else ""


def findCodeNameViolations(repositoryRoot: Path) -> tuple[list[str], dict[str, int]]:
    mapGameText = collectSourceByGame(repositoryRoot)
    listViolation: list[str] = []
    mapCheckedCount: dict[str, int] = {}
    for kind in _kListCodeNameKind:
        uniqueShared, mapPackName = collectDataNames(repositoryRoot, kind)
        uniqueAll = uniqueShared.union(*mapPackName.values()) if mapPackName else set(uniqueShared)
        checkedCount = 0
        for gameName, listFile in sorted(mapGameText.items()):
            known, scope = uniqueAll, f"어느 {kind.dataGlob}"
            if gameName:
                pack = findPackOfGame(listFile)
                known, scope = uniqueShared | mapPackName.get(pack, set()), f"팩 밖과 game/{pack} 의 {kind.dataGlob}"
            for relative, text in listFile:
                if kind.marker not in text:
                    continue
                for name in collectCodeNames(stripCommentsInternal(text), kind):
                    checkedCount += 1
                    if name not in known:
                        listViolation.append(f"[NameReference/{kind.rule}] {relative}: {kind.label} '{name}' 이(가) {scope}에 없습니다")
        mapCheckedCount[kind.rule] = checkedCount
    return listViolation, mapCheckedCount


def collectAutomationNames(repositoryRoot: Path) -> tuple[set[str], set[str], set[str]]:
    """(등록된 시나리오 단계 종류, 탐침 이름, 개발 명령 이름) — `Source` 의 등록 매크로에서 줍는다(종류 상수 `k…Kind` 도 푼다)."""
    uniqueStep: set[str] = set()
    uniqueProbe: set[str] = set()
    uniqueCommand: set[str] = set()
    mapKindConstant: dict[str, str] = {}
    listKindIdentifier: list[str] = []
    for _, text in LintGate.readFiles(collectSourceFiles([repositoryRoot / "Source"], _kCppSuffixes)):
        mapKindConstant.update(dict(_kStepKindConstantRe.findall(text)))
        if "SW_AUTOMATION_" in text:
            uniqueStep.update(_kStepRegistrationRe.findall(text))
            listKindIdentifier += _kStepRegistrationByConstantRe.findall(text)
            uniqueProbe.update(_kProbeRegistrationRe.findall(text))
        if "SW_DEV_COMMAND" in text:
            uniqueCommand.update(_kDevCommandRegistrationRe.findall(text))
    uniqueStep.update(mapKindConstant[name] for name in listKindIdentifier if name in mapKindConstant)
    return uniqueStep, uniqueProbe, uniqueCommand


def findScenarioViolations(repositoryRoot: Path) -> tuple[list[str], int]:
    """시나리오의 `<At>` 단계 · `probe` · `<DevCommand line>` 첫 낱말이 등록된 이름인가."""
    uniqueStep, uniqueProbe, uniqueCommand = collectAutomationNames(repositoryRoot)
    bAutomationEngine = (repositoryRoot / "Source" / "Engine" / "Automation").is_dir()
    if bAutomationEngine and not uniqueStep:
        raise GateError("등록된 시나리오 단계를 하나도 찾지 못했습니다 — SW_AUTOMATION_STEP 모양이 바뀌었나요?")
    listViolation: list[str] = []
    scenarioCount = 0
    resourceRoot = repositoryRoot / "Resource"
    for path in sorted(resourceRoot.rglob("*.scenario.xml")):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        try:
            root = ElementTree.parse(path).getroot()
        except ElementTree.ParseError:
            continue  # XML 이 깨진 것은 에셋 검증 규칙 xml-parses 가 알린다
        scenarioCount += 1
        for at in root.iter("At"):
            for step in at:
                if step.tag not in uniqueStep:
                    listViolation.append(f"[NameReference/ScenarioStep] {relative}: 단계 <{step.tag}> 가 등록돼 있지 않습니다(SW_AUTOMATION_STEP)")
        for element in root.iter():
            probe = element.get("probe")
            if probe and probe not in uniqueProbe:
                listViolation.append(f"[NameReference/ScenarioProbe] {relative}: 탐침 '{probe}' 가 등록돼 있지 않습니다(SW_AUTOMATION_PROBE)")
        for element in root.iter("DevCommand"):
            listToken = element.get("line", "").split()
            if listToken and listToken[0] not in uniqueCommand:
                listViolation.append(f"[NameReference/DevCommand] {relative}: 개발 명령 '{listToken[0]}' 이(가) 등록돼 있지 않습니다(SW_DEV_COMMAND)")
    return listViolation, scenarioCount


class CheckNameReferencesGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "코드 · 실행 인자 · 시나리오가 이름으로 부르는 전역 변수 · 입력 액션 · 오디오 이벤트 · 시나리오 단계 · 탐침 · 개발 명령이 정의돼 있는지 검사"
    buildComment = "Checking that names used by code, arguments and scenarios (global variables, input actions, audio events, steps, probes, dev commands) are defined..."
    timeoutSeconds = 60
    preCommitPattern = ("*.h", "*.cpp", "*.inl", "*.py", "*.json", "*.xml", "*.cmake", "*.txt", "*.yml", "*.ps1")
    preCommitFileArgument = ""
    violationHeader = "정의되지 않은 이름"
    hint = (
        "  전역 변수: 정의(SW_*GLOBAL_VARIABLE)의 이름으로 고치거나, 지운 스위치면 그 인자 · 시나리오 줄을 지웁니다.\n"
        "  입력 액션 · 오디오 이벤트: 그 팩의 *.input.xml · *.audioevents.xml 에 이름을 더하거나 코드의 이름을 데이터의 이름으로 고칩니다.\n"
        "  시나리오: 단계 · 탐침 · 개발 명령의 이름을 등록된 이름(SW_AUTOMATION_STEP · SW_AUTOMATION_PROBE · SW_DEV_COMMAND)으로 고칩니다."
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
        {
            "name": "코드가 라이브러리에 없는 오디오 이벤트를 올린다",
            "files": {
                "Resource/engine/audio/probe.audioevents.xml": "<AudioEventLibrary><AudioEventDesc _name=\"Hit\"/></AudioEventLibrary>\n",
                "Source/Engine/Probe.cpp": "// postEvent( \"Commented\" ) 는 보지 않는다\nvoid f() { (void)GameSound::postEvent( \"Hti\" ); }\n",
            },
        },
        {
            "name": "시나리오가 등록되지 않은 개발 명령을 부른다",
            "files": {
                "Source/Engine/ProbeSteps.cpp": ("SW_AUTOMATION_STEP( devCommand, AutomationEnvironmentSteps::kDevCommandKind, &run, &validate, false );\n"
                                                 "SW_DEV_COMMAND( Bugit, \"bugit\", \"\", \"\", &run );\n"),
                "Source/Engine/ProbeSteps.h": "static constexpr const utf8* kDevCommandKind = \"DevCommand\";\n",
                "Resource/engine/automation/probe.scenario.xml": "<Scenario><At frame=\"1\"><DevCommand line=\"bugitt now\"/></At></Scenario>\n",
            },
        },
        {
            "name": "시나리오가 등록되지 않은 탐침을 본다",
            "files": {
                "Source/Engine/ProbeSteps.cpp": ("SW_AUTOMATION_STEP( expect, \"Expect\", &run, &validate, false );\n"
                                                 "SW_AUTOMATION_PROBE( count, \"Probe.Count\", \"\", &count );\n"),
                "Resource/engine/automation/probe.scenario.xml": "<Scenario><At frame=\"1\"><Expect probe=\"Probe.Cuont\" equals=\"1\"/></At></Scenario>\n",
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
        codeViolation, mapCheckedCount = findCodeNameViolations(repositoryRoot)
        listViolation += codeViolation
        scenarioViolation, scenarioCount = findScenarioViolations(repositoryRoot)
        listViolation += scenarioViolation
        codeSummary = ", ".join(f"{rule} {count}곳" for rule, count in mapCheckedCount.items())
        return GateResult(listViolation=sorted(set(listViolation)),
                          summary=f"전역 변수 정의 {len(uniqueDefined)}개 · 이름 사용 {len(listUse)}곳, 코드의 이름 {codeSummary}, 시나리오 {scenarioCount}개")


main = CheckNameReferencesGate.run


if __name__ == "__main__":
    sys.exit(main())
