"""
시나리오 실행의 App 인자 어휘 — 정본은 C++ `Test/AppTest/AppTestUtil.h` 의 `AppTestUtil::k…Argument…` 상수 한 곳이다.

`AppScenarioTest`(C++)와 `py -3 -m Scripts scenario`(`qa/ScenarioRun.py`)는 같은 인자(`-unattended` · `-scenario=` · `-scenario-report=` · `-EnableEditor` ·
`-gv_editorStateDir=` · `-game=` 과 보고 · 에디터 상태 폴더 이름)로 App 을 띄운다. 두 벌로 적으면 한쪽만 바뀌어 어긋나므로, 이 모듈은 헤더의 상수 줄을
읽어 온다(`AppRun.readRhiUnusableExitCode` 와 같은 방식). 줄 모양이 바뀌었거나 상수가 없으면 오류로 멈춘다(폴백 없음).
인자를 만드는 순서와 `<Launch>` 값은 시나리오 파일에서 오므로 여기 없다 — 인자 순서는 App 에게 의미가 없다.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

from .Paths import getProjectRoot

#: 상수의 정본(C++ 헤더).
kArgumentHeaderRelPath = "Test/AppTest/AppTestUtil.h"
_kConstantRe = re.compile(r'static\s+constexpr\s+const\s+utf8\*\s+(k\w+)\s*=\s*"([^"]*)"\s*;')


@dataclass(frozen=True)
class ScenarioArgumentVocabulary:
    """시나리오 실행 인자의 글 — 필드 이름은 헤더 상수 이름에서 `k` 를 뗀 것이다."""

    unattendedArgument: str
    scenarioArgumentPrefix: str
    scenarioReportArgumentPrefix: str
    editorArgument: str
    editorStateDirArgumentPrefix: str
    gameArgumentPrefix: str
    automationFolder: str
    editorStateSubfolder: str


def readScenarioArgumentVocabulary(headerPath: Path) -> ScenarioArgumentVocabulary:
    """헤더에서 어휘를 읽는다. 상수가 하나라도 없으면 `sys.exit`(줄 모양이 바뀌었나)."""
    mapConstant = {name: text for name, text in _kConstantRe.findall(headerPath.read_text(encoding="utf-8"))}
    mapValue: dict[str, str] = {}
    for field in ScenarioArgumentVocabulary.__dataclass_fields__:
        constantName = "k" + field[0].upper() + field[1:]
        if constantName not in mapConstant:
            sys.exit(f"{headerPath}: `static constexpr const utf8* {constantName} = \"…\";` 줄을 찾지 못했다 — 줄 모양이 바뀌었나")
        mapValue[field] = mapConstant[constantName]
    return ScenarioArgumentVocabulary(**mapValue)


kVocabulary = readScenarioArgumentVocabulary(getProjectRoot() / kArgumentHeaderRelPath)
