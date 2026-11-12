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
from common.Paths import getProjectRoot  # noqa: E402
from lint.FactCache import loadFacts  # noqa: E402
from lint.LintScope import LintScope, listRepoFiles, makeWholeScope  # noqa: E402

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


def classifyPath(relative: str) -> str | None:
    """파일이 드는 묶음(`Sources` 의 칸 이름) — 훑지 않는 파일은 None."""
    suffix = Path(relative).suffix.lower()
    if relative.startswith("Source/") and suffix in kCppExtensions:
        return "dictTestCode" if "/Tests/" in relative else "dictHeader"
    if relative.startswith("Content/") and suffix in kContentExtensions:
        return "dictContent"
    if relative.startswith(("Scripts/", "Test/")) and suffix in kScriptExtensions and relative != "Scripts/lint/FeatureReferences.py":
        return "dictScript"
    return None


def stripRegistrations(text: str) -> str:
    for pattern in (kWatchPattern, kCommandPattern):
        text = pattern.sub("", text)
    return text


def extractFacts(bucket: str, path: str, text: str) -> dict[str, list[str]]:
    """파일 하나의 사실 — 정의(컴포넌트 · 감시 값 · 명령), 참조(컴포넌트 이름 · 글자 첫 낱말 · 시나리오 낱말), 등록 없는 명령 함수. 색인(`FactCache`)에 둔다."""
    facts: dict[str, list[str]] = {}
    if bucket == "dictHeader":
        if path.endswith(".h"):
            facts["component"] = [match.group(1) for match in kClassPattern.finditer(text)
                                  if any("REFLECT(" in line for line in text[:match.start()].splitlines()[-kReflectLookBackLines:])]
        if not (path.endswith("AutomationWatch.h") or path.endswith("AutomationComponentWatch.h")):   # 매크로 정의 자체
            facts["watch"] = [match.group(1) for match in kWatchPattern.finditer(text)]
            facts["command"] = [match.group(1) for match in kCommandPattern.finditer(text)]
        facts["componentReference"] = kAddComponentPattern.findall(text) + kBasePattern.findall(text)
        facts["lostRunner"] = [match.group(1) for match in kCommandRunnerPattern.finditer(text) if len(re.findall(rf"\b{match.group(1)}\b", text)) < 2]
    if bucket in ("dictHeader", "dictTestCode", "dictScript"):
        facts["literal"] = sorted(set(kLiteralPattern.findall(stripRegistrations(text))))
    if bucket == "dictContent":
        facts["componentReference"] = sorted({word for word in kWordPattern.findall(text) if word.endswith("Component")})
        if path.endswith(".scenario.xml"):
            facts["scenarioWatch"] = kWatchUsePattern.findall(text)
            facts["commandWord"] = [word for line in kCommandLinePattern.findall(text) for word in line.split()]
    return facts


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


def judgeFacts(mapFact: dict[str, dict[str, list[str]]], exceptions: Exceptions) -> list[str]:
    """저장소 전체 사실(경로 → 사실, 경로 순)로 참조 0 을 가린다 — 참조는 어느 파일에서 와도 되므로 판정은 늘 전체로 낸다."""
    def collect(key: str) -> set[str]:
        return {name for facts in mapFact.values() for name in facts.get(key, ())}

    def define(key: str) -> dict[str, str]:
        return {name: path for path, facts in mapFact.items() for name in facts.get(key, ())}   # 같은 이름은 뒤 파일이 이긴다

    listProblem: list[str] = []
    dictComponent = define("component")
    setComponentReference = collect("componentReference")
    for name, path in sorted(dictComponent.items()):
        bFolderExcepted = any(path.startswith(folder + "/") for folder in exceptions.dictFolder)
        if name not in setComponentReference and not bFolderExcepted and name not in exceptions.dictComponent:
            listProblem.append(f"{path}: 컴포넌트 {name} 를 콘텐츠도 코드도 쓰지 않는다 — 배치하거나 {kConfigPath} 에 까닭을 적는다")
    listProblem += checkExceptionsTable("컴포넌트", dictComponent, setComponentReference, exceptions.dictComponent)

    dictWatch = define("watch")
    dictCommand = define("command")
    setLiteral = collect("literal")
    setWatchReference = collect("scenarioWatch") | setLiteral
    setCommandReference = collect("commandWord") | setLiteral
    for name, path in sorted(dictWatch.items()):
        if name not in setWatchReference and name not in exceptions.dictWatch:
            listProblem.append(f"{path}: 감시 값 {name} 를 아무도 읽지 않는다 — 판정에 쓰거나 지우거나 {kConfigPath} 에 까닭을 적는다")
    listProblem += checkExceptionsTable("감시 값", dictWatch, setWatchReference, exceptions.dictWatch)
    for name, path in sorted(dictCommand.items()):
        if name not in setCommandReference and name not in exceptions.dictCommand:
            listProblem.append(f"{path}: 개발 명령 {name} 를 자동화가 부르지 않는다 — 시나리오로 잇거나 {kConfigPath} 에 까닭을 적는다")
    listProblem += checkExceptionsTable("개발 명령", dictCommand, setCommandReference, exceptions.dictCommand)
    for path in sorted(mapFact):
        for name in mapFact[path].get("lostRunner", ()):
            listProblem.append(f"{path}: 명령 함수 {name} 를 등록(SW_DEV_COMMAND)도 호출도 하지 않는다 — 등록 줄을 되살리거나 함수를 지운다")
    return listProblem


def findProblems(sources: Sources, exceptions: Exceptions) -> list[str]:
    mapFact = {path: extractFacts(bucket, path, text) for bucket in ("dictHeader", "dictTestCode", "dictContent", "dictScript")
               for path, text in getattr(sources, bucket).items()}
    return judgeFacts(mapFact, exceptions)


def readExceptions(projectRoot: Path) -> Exceptions:
    data = json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))
    return Exceptions(data["_folders"], data["_components"], data["_watches"], data["_devCommands"])


def readFacts(projectRoot: Path) -> dict[str, dict[str, list[str]]]:
    """저장소에 있는 파일만 본다(git 이 아는 파일 — 빌드 산출물 · 임포트 산출물을 줍지 않는다). 내용이 바뀐 파일만 다시 읽는다(`FactCache`)."""
    listPath = [path for path in listRepoFiles(projectRoot) if classifyPath(path) is not None]
    return loadFacts(projectRoot, "feature-refs", [__file__], listPath, lambda path, text: extractFacts(classifyPath(path), path, text), errors="ignore")


def checkFeatureReferences(projectRoot: Path, scope: LintScope) -> list[str]:
    """커밋 훅은 훑는 파일이 바뀌었을 때만 돈다(지운 파일 포함 — 참조를 지우면 참조 0 이 생긴다)."""
    if not scope.bWholeRepo and not any(classifyPath(path) for path in (*scope.listFile, *scope.listRemoved)):
        return []
    return judgeFacts(readFacts(projectRoot), readExceptions(projectRoot))


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
    listProblem = checkFeatureReferences(getProjectRoot(), makeWholeScope(getProjectRoot()))
    for line in listProblem:
        print(f"  - {line}")
    print(f"[FeatureReferences] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
