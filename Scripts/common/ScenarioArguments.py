"""
시나리오 실행의 App 인자 어휘 — 정본은 엔진 쪽 두 자리이고, 이 모듈은 그것을 읽어 온다.

`AppScenarioTest`(C++)와 `py -3 -m Scripts scenario`(`qa/ScenarioRun.py`)는 같은 인자로 App 을 띄운다. 글을 두 벌로 적으면 한쪽만 바뀌어 어긋나므로 정본 하나에서 읽는다:

- 엔진이 읽는 인자의 철자(`-unattended` · `-scenario` · `-scenario-report` · `-EnableEditor` · `-game`) — `Source/Core/Predefined/ArgumentList.xxx` 의 줄
  (`ConfigReference.collectArguments` 가 읽는다. C++ 은 `getCommandLineArgumentSpelling` 으로 같은 표를 쓴다).
- 자동화 산출물 폴더 · 에디터 상태 폴더 이름 · 에디터 상태 폴더를 주는 전역 변수 이름 — `Source/Engine/Automation/AutomationScenario.h` 의
  `inline constexpr const utf8* kAutomationXxx = "…";` 줄(실행기 · 녹화기 · AppTest 가 쓴다).

철자나 상수 줄이 없거나 모양이 바뀌었으면 오류로 멈춘다(폴백 없음). 인자를 만드는 순서와 `<Launch>` 값은 시나리오 파일에서 오므로 여기 없다 — 인자 순서는 App 에게 의미가 없다.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

from .ConfigReference import collectArguments
from .Paths import getProjectRoot

#: 자동화 글 상수의 정본(C++ 엔진 헤더).
kAutomationHeaderRelPath = "Source/Engine/Automation/AutomationScenario.h"
#: 엔진 인자 표(철자의 정본).
kArgumentListRelPath = "Source/Core/Predefined/ArgumentList.xxx"
_kConstantRe = re.compile(r'inline\s+constexpr\s+const\s+utf8\*\s+(k\w+)\s*=\s*"([^"]*)"\s*;')
#: 어휘가 쓰는 엔진 인자의 열거자 이름(`ArgumentList.xxx` 줄의 첫 칸).
_kEnumUnattended = "UNATTENDED"
_kEnumScenario = "SCENARIO"
_kEnumScenarioReport = "SCENARIO_REPORT"
_kEnumEditor = "ENABLE_EDITOR"
_kEnumGame = "GAME"


@dataclass(frozen=True)
class ScenarioArgumentVocabulary:
    """시나리오 실행 인자의 글(앞의 `-` 와 값 앞의 `=` 까지 붙인 모양)."""

    unattendedArgument: str
    scenarioArgumentPrefix: str
    scenarioReportArgumentPrefix: str
    editorArgument: str
    editorStateDirArgumentPrefix: str
    gameArgumentPrefix: str
    automationFolder: str
    editorStateSubfolder: str


def readScenarioArgumentVocabulary(repositoryRoot: Path) -> ScenarioArgumentVocabulary:
    """저장소의 `ArgumentList.xxx` 와 `AutomationScenario.h` 에서 어휘를 읽는다. 철자나 상수가 하나라도 없으면 `sys.exit`."""
    mapSpelling = {argument.enumName: argument.listSpelling[0] for argument in collectArguments(repositoryRoot) if argument.listSpelling}
    headerPath = repositoryRoot / kAutomationHeaderRelPath
    mapConstant = dict(_kConstantRe.findall(headerPath.read_text(encoding="utf-8")))
    for enumName in (_kEnumUnattended, _kEnumScenario, _kEnumScenarioReport, _kEnumEditor, _kEnumGame):
        if enumName not in mapSpelling:
            sys.exit(f"{repositoryRoot / kArgumentListRelPath}: SW_REGISTER_ARGUMENT( {enumName}, … ) 줄을 찾지 못했다 — 줄이 바뀌었나")
    for constantName in ("kAutomationOutputFolder", "kAutomationEditorStateFolder", "kAutomationEditorStateVariable"):
        if constantName not in mapConstant:
            sys.exit(f"{headerPath}: `inline constexpr const utf8* {constantName} = \"…\";` 줄을 찾지 못했다 — 줄 모양이 바뀌었나")
    return ScenarioArgumentVocabulary(
        unattendedArgument=f"-{mapSpelling[_kEnumUnattended]}",
        scenarioArgumentPrefix=f"-{mapSpelling[_kEnumScenario]}=",
        scenarioReportArgumentPrefix=f"-{mapSpelling[_kEnumScenarioReport]}=",
        editorArgument=f"-{mapSpelling[_kEnumEditor]}",
        editorStateDirArgumentPrefix=f"-{mapConstant['kAutomationEditorStateVariable']}=",
        gameArgumentPrefix=f"-{mapSpelling[_kEnumGame]}=",
        automationFolder=mapConstant["kAutomationOutputFolder"],
        editorStateSubfolder=mapConstant["kAutomationEditorStateFolder"])


kVocabulary = readScenarioArgumentVocabulary(getProjectRoot())
