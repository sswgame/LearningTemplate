#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/FeatureReferences.py

만들어 두고 아무 데도 싣지 않은 기능을 잡는다 — 리플렉션 컴포넌트 · 감시 값 · 개발 명령마다 콘텐츠 · 시나리오 · 코드의 참조를 세어 0 이면 위반이다.
**왜 린트인가.** "쓰는 곳이 있다" 는 형식이 말할 수 없다(컴포넌트는 데이터가 이름으로 부른다). 그래서 이 틈만 글로 본다. 근본 원인은 기능을 "코드 + 단위 테스트" 까지만 만들고
사용처를 완료 조건에 두지 않은 것이다(`docs/NEXT.md`).

- 컴포넌트: `REFLECT` 가 붙은 `*Component`(폴더 `Tests/` 제외). 참조 = `Content` 글 파일의 그 이름, 코드(`Tests/` 제외)의 `addComponent<이름>`, 그 이름을 기반으로 삼은 클래스.
- 감시 값: `SW_AUTOMATION_*WATCH`. 참조 = 시나리오의 `watch="이름"`, 등록 줄 밖 코드 · 스크립트의 `"이름"` 글자.
- 개발 명령: `SW_DEV_COMMAND`. 참조 = 시나리오 `line="…"` 의 낱말, 등록 줄 밖 코드 · 스크립트 글자의 첫 낱말.
- 등록 없는 명령 함수: 명령 모양(`static bool f( const vector<string>&, string& )`)의 함수가 그 파일에서 정의 말고 한 번도 불리지 않으면 위반이다 —
  `*DevCommands.cpp` 를 기능 파일로 합칠 때(165e16647c) `SW_DEV_COMMAND` 줄만 빠져 치트 명령 아홉이 함수만 남았는데, 익명 구조체의 static 멤버는 컴파일러가 경고하지 않는다.

예외는 데이터 한 곳(`Config/Rules/Lint/FeatureReferenceExceptions.json`)에 까닭과 함께 둔다. 까닭이 비었거나, 이제 참조가 생겼거나, 정의가 사라진 예외도 위반이다 — 표가 낡지 않게.

`py -3 -m Scripts lint feature-refs [--self-check]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ExitCode import ExitCode  # noqa: E402
from common.Git import runGit  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402

kConfigPath = "Config/Rules/Lint/FeatureReferenceExceptions.json"
kCppExtensions = (".h", ".cpp", ".inl", ".xxx")
kContentExtensions = (".xml", ".json")
kScriptExtensions = (".py", ".cmake", ".sh", ".txt")

kClassPattern = re.compile(r'^\s*class\s+(?:SW_\w+\s+)?(\w+Component)\b[^;{]*?:\s*(?:public|private|protected)\s+([\w:]+)', re.M)
kBasePattern = re.compile(r'^\s*(?:class|struct)\s+(?:SW_\w+\s+)?\w+\b[^;{(]*?:\s*(?:public|private|protected)\s+(?:\w+::)*(\w+Component)\b', re.M)
kAddComponentPattern = re.compile(r'addComponent<\s*(?:\w+::)*(\w+Component)\s*>')
kWatchPattern = re.compile(r'SW_AUTOMATION_(?:WALL_|COMPONENT_|GETTER_)?WATCH\(\s*\w+\s*,\s*"([\w.]+)"[^;]*?\)\s*;?', re.S)
kCommandPattern = re.compile(r'SW_DEV_COMMAND\(\s*\w+\s*,\s*"([\w.]+)".*?\)\s*;', re.S)
kCommandRunnerPattern = re.compile(r'static\s+bool\s+(\w+)\(\s*const\s+vector<string>&\s*\w+\s*,\s*string&\s*\w+\s*\)')
kWatchUsePattern = re.compile(r'\bwatch="([^"]+)"')
kCommandLinePattern = re.compile(r'\bline="([^"]+)"')
kWordPattern = re.compile(r'[A-Za-z_]\w*')
kLiteralPattern = re.compile(r'"([A-Za-z][\w.]*)(?:[ "])')
kReflectLookBackLines = 6


@dataclass
class Sources:
    """훑을 글 묶음 — 시험이 파일 없이 넣을 수 있게 내용으로 든다."""

    dictHeader: dict[str, str] = field(default_factory=dict)      # 경로 → 글(`Tests/` 밖 C++)
    dictTestCode: dict[str, str] = field(default_factory=dict)    # 경로 → 글(`Tests/` 안 C++)
    dictContent: dict[str, str] = field(default_factory=dict)     # 경로 → 글(Content 글 파일)
    dictScript: dict[str, str] = field(default_factory=dict)      # 경로 → 글(Scripts · Test · CMake)


@dataclass(frozen=True)
class Exceptions:
    """`Config/Rules/Lint/FeatureReferenceExceptions.json` 의 내용."""

    dictFolder: dict[str, str]        # 폴더 → 까닭(그 안의 컴포넌트 모두)
    dictComponent: dict[str, str]
    dictWatch: dict[str, str]
    dictCommand: dict[str, str]


def findComponents(sources: Sources) -> dict[str, str]:
    """`REFLECT` 가 붙은 컴포넌트 이름 → 정의 파일."""
    dictFound: dict[str, str] = {}
    for path, text in sources.dictHeader.items():
        if not path.endswith(".h"):
            continue
        for match in kClassPattern.finditer(text):
            before = text[:match.start()].splitlines()[-kReflectLookBackLines:]
            if any("REFLECT(" in line for line in before):
                dictFound[match.group(1)] = path
    return dictFound


def findDefinitions(sources: Sources, pattern: re.Pattern[str]) -> dict[str, str]:
    """등록 매크로의 이름 → 정의 파일."""
    dictFound: dict[str, str] = {}
    for path, text in sources.dictHeader.items():
        if path.endswith("AutomationWatch.h") or path.endswith("AutomationComponentWatch.h"):
            continue   # 매크로 정의 자체
        for match in pattern.finditer(text):
            dictFound[match.group(1)] = path
    return dictFound


def collectComponentReferences(sources: Sources) -> set[str]:
    setReference: set[str] = set()
    for text in sources.dictContent.values():
        setReference.update(word for word in kWordPattern.findall(text) if word.endswith("Component"))
    for text in sources.dictHeader.values():
        setReference.update(kAddComponentPattern.findall(text))
        setReference.update(kBasePattern.findall(text))
    return setReference


def collectLiterals(sources: Sources, listStripPattern: Sequence[re.Pattern[str]]) -> set[str]:
    """등록 줄을 뺀 코드 · 스크립트의 글자 첫 낱말."""
    setLiteral: set[str] = set()
    for dictText in (sources.dictHeader, sources.dictTestCode, sources.dictScript):
        for text in dictText.values():
            for pattern in listStripPattern:
                text = pattern.sub("", text)
            setLiteral.update(kLiteralPattern.findall(text))
    return setLiteral


def collectScenarioWords(sources: Sources) -> tuple[set[str], set[str]]:
    """시나리오의 감시 값 이름들, `line="…"` 의 낱말들."""
    setWatch: set[str] = set()
    setCommandWord: set[str] = set()
    for path, text in sources.dictContent.items():
        if not path.endswith(".scenario.xml"):
            continue
        setWatch.update(kWatchUsePattern.findall(text))
        for line in kCommandLinePattern.findall(text):
            setCommandWord.update(line.split())
    return setWatch, setCommandWord


def checkExceptionsTable(kind: str, dictDefined: dict[str, str], setReference: set[str], dictException: dict[str, str]) -> list[str]:
    """표의 줄이 살아 있는지 — 까닭이 있고, 정의가 있고, 아직 참조가 없어야 한다."""
    listProblem: list[str] = []
    for name, reason in dictException.items():
        if not reason.strip():
            listProblem.append(f"{kConfigPath}: {kind} '{name}' 의 까닭이 비었다")
        elif name not in dictDefined:
            listProblem.append(f"{kConfigPath}: {kind} '{name}' 의 정의가 없다 — 줄을 지운다")
        elif name in setReference:
            listProblem.append(f"{kConfigPath}: {kind} '{name}' 는 이제 참조가 있다 — 줄을 지운다")
    return listProblem


def findProblems(sources: Sources, exceptions: Exceptions) -> list[str]:
    listProblem: list[str] = []
    dictComponent = findComponents(sources)
    setComponentReference = collectComponentReferences(sources)
    for name, path in sorted(dictComponent.items()):
        bFolderExcepted = any(path.startswith(folder + "/") for folder in exceptions.dictFolder)
        if name not in setComponentReference and not bFolderExcepted and name not in exceptions.dictComponent:
            listProblem.append(f"{path}: 컴포넌트 {name} 를 콘텐츠도 코드도 쓰지 않는다 — 배치하거나 {kConfigPath} 에 까닭을 적는다")
    listProblem += checkExceptionsTable("컴포넌트", dictComponent, setComponentReference, exceptions.dictComponent)

    dictWatch = findDefinitions(sources, kWatchPattern)
    dictCommand = findDefinitions(sources, kCommandPattern)
    setLiteral = collectLiterals(sources, [kWatchPattern, kCommandPattern])
    setScenarioWatch, setCommandWord = collectScenarioWords(sources)
    setWatchReference = setScenarioWatch | setLiteral
    setCommandReference = setCommandWord | setLiteral
    for name, path in sorted(dictWatch.items()):
        if name not in setWatchReference and name not in exceptions.dictWatch:
            listProblem.append(f"{path}: 감시 값 {name} 를 아무도 읽지 않는다 — 판정에 쓰거나 지우거나 {kConfigPath} 에 까닭을 적는다")
    listProblem += checkExceptionsTable("감시 값", dictWatch, setWatchReference, exceptions.dictWatch)
    for name, path in sorted(dictCommand.items()):
        if name not in setCommandReference and name not in exceptions.dictCommand:
            listProblem.append(f"{path}: 개발 명령 {name} 를 자동화가 부르지 않는다 — 시나리오로 잇거나 {kConfigPath} 에 까닭을 적는다")
    listProblem += checkExceptionsTable("개발 명령", dictCommand, setCommandReference, exceptions.dictCommand)
    for path, text in sorted(sources.dictHeader.items()):
        for match in kCommandRunnerPattern.finditer(text):
            name = match.group(1)
            if len(re.findall(rf"\b{name}\b", text)) < 2:
                listProblem.append(f"{path}: 명령 함수 {name} 를 등록(SW_DEV_COMMAND)도 호출도 하지 않는다 — 등록 줄을 되살리거나 함수를 지운다")
    return listProblem


def readExceptions(projectRoot: Path) -> Exceptions:
    data = json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))
    return Exceptions(data["_folders"], data["_components"], data["_watches"], data["_devCommands"])


def readSources(projectRoot: Path) -> Sources:
    """저장소에 있는 파일만 본다(`git ls-files`, 빌드 산출물 · 임포트 산출물을 줍지 않는다)."""
    result = runGit(["ls-files", "--cached", "--others", "--exclude-standard", "Source", "Content", "Scripts", "Test"], cwd=projectRoot)
    sources = Sources()
    for relative in result.stdout.splitlines():
        path = projectRoot / relative
        suffix = path.suffix.lower()
        if relative.startswith("Source/") and suffix in kCppExtensions:
            bucket = sources.dictTestCode if "/Tests/" in relative else sources.dictHeader
        elif relative.startswith("Content/") and suffix in kContentExtensions:
            bucket = sources.dictContent
        elif relative.startswith(("Scripts/", "Test/")) and suffix in kScriptExtensions and relative != "Scripts/lint/FeatureReferences.py":
            bucket = sources.dictScript
        else:
            continue
        try:
            bucket[relative] = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue   # 지운 채 스테이지하지 않은 파일
    return sources


def checkFeatureReferences(projectRoot: Path) -> list[str]:
    return findProblems(readSources(projectRoot), readExceptions(projectRoot))


def runSelfCheck() -> None:
    """규칙 자체의 시험 — 저장소를 읽지 않는다."""
    sources = Sources()
    sources.dictHeader["Source/A/Used.h"] = 'REFLECT( Category = "A" )\nclass SW_API UsedComponent : public Component\n{\n};\n'
    sources.dictHeader["Source/A/Lonely.h"] = 'REFLECT()\nclass SW_API LonelyComponent : public Component\n{\n};\n'
    sources.dictHeader["Source/A/Base.h"] = 'REFLECT()\nclass SW_API BaseThingComponent : public Component\n{\n};\nclass DerivedOne : public BaseThingComponent\n{\n};\n'
    sources.dictHeader["Source/A/Plain.h"] = 'class SW_API PlainComponent : public Component\n{\n};\n'
    sources.dictHeader["Source/A/Coded.h"] = 'REFLECT()\nclass CodedComponent : public Component\n{\n};\n'
    sources.dictHeader["Source/A/Spawner.cpp"] = 'void f( GameObject& o ) { o.addComponent<CodedComponent>(); }\n'
    sources.dictHeader["Source/A/W.cpp"] = ('SW_AUTOMATION_WATCH( a, "A.Read", "tip", &read );\nSW_AUTOMATION_WALL_WATCH( b,\n "A.Unread", "tip", &read );\n'
                                            'SW_DEV_COMMAND( c, "cmd.used", "cmd.used", "tip", &run );\nSW_DEV_COMMAND( d, "cmd.alone", "cmd.alone <x>",\n "tip", &run );\n'
                                            'SW_DEV_COMMAND( e, "cmd.sent", "cmd.sent", "tip", &run );\nvoid g() { send( "cmd.sent 1" ); }\n')
    sources.dictHeader["Source/A/Cmd.cpp"] = ('struct I { static bool runLost( const vector<string>& listArgument, string& outReply ) { return true; }\n'
                                              ' static bool runKept( const vector<string>& listArgument, string& outReply ) { return true; } };\n'
                                              'SW_DEV_COMMAND( k, "cmd.kept", "cmd.kept", "tip", &I::runKept );\n')
    sources.dictContent["Content/x.scene.xml"] = "<UsedComponent/>\n"
    sources.dictContent["Content/x.scenario.xml"] = '<Expect watch="A.Read" equals="1"/>\n<DevCommand line="netplay.command clients cmd.used 3"/>\n<DevCommand line="cmd.kept"/>\n'

    empty = Exceptions({}, {}, {}, {})
    listProblem = findProblems(sources, empty)
    assert len(listProblem) == 4, listProblem
    assert any("runLost" in line for line in listProblem)
    assert any("LonelyComponent" in line for line in listProblem)
    assert any("A.Unread" in line for line in listProblem)
    assert any("cmd.alone" in line for line in listProblem)

    # 예외: 까닭과 함께면 받고, 낡은 줄은 위반이다.
    assert [line for line in findProblems(sources, Exceptions({"Source/A": "x"}, {}, {"A.Unread": "x"}, {"cmd.alone": "x"})) if "runLost" not in line] == []
    listStale = findProblems(sources, Exceptions({}, {"LonelyComponent": "", "UsedComponent": "x", "GoneComponent": "x"}, {"A.Unread": "x"}, {"cmd.alone": "x"}))
    assert sum("까닭이 비었다" in line for line in listStale) == 1, listStale
    assert sum("이제 참조가 있다" in line for line in listStale) == 1, listStale
    assert sum("정의가 없다" in line for line in listStale) == 1, listStale


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="컴포넌트 · 감시 값 · 개발 명령의 참조 0 검사(머리말)")
    parser.add_argument("--self-check", action="store_true", help="규칙 자체를 시험한다")
    args = parser.parse_args(argv)
    if args.self_check:
        runSelfCheck()
        print("[FeatureReferences] self-check OK")
        return ExitCode.Passed
    listProblem = checkFeatureReferences(getProjectRoot())
    for line in listProblem:
        print(f"  - {line}")
    print(f"[FeatureReferences] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
