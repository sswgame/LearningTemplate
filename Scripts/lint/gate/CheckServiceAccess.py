#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
서비스 접근이 **소유 수명마다 한 창구, 한 어휘**인지 검사한다.

접근 창구는 이름 둘이다 — `getSubsystem<T>()`(`T&`, 반드시 있음)와 `findSubsystem<T>()`(`T*`, 없을 수 있음). 범위는 엔진(`engine::`) · 게임(`game::`)
· 에디터(`editor::`) · 씬(`GameObjectManager::getSubsystem<T>()`)이고, 범위를 든 객체의 멤버(`EditorContext` · `GameInstanceBase` · `SubsystemScope`)는
그대로다. 이 게이트는 옛 창구가 다시 생기지 않게 막는다.

  - `ServiceAccess/RetiredWindow`: 지워진 이름 — `getService<` · `getSceneService<` · `isServiceBound<` · `getBoundEngineServices(`,
    `locateSubsystem<…>( engine::getEngineSubsystems() )`, 그리고 `engine::` 아래의 이름 붙은 접근자(`engine::getTypeRegistry()` 따위).
    `engine::` 아래에 남는 `get*` 은 규칙 데이터의 `allowed_engine_function` 뿐이다. 주석 · 문자열 안의 언급은 보지 않는다.
  - `ServiceAccess/Singleton`: 헤더의 `static … getInstance()`. 전역 단일체는 수명과 순서가 숨는다 — 새 서비스는 서브시스템으로 만들어 범위에 넣는다.
    예외는 `Scripts/lint/rules/CheckServiceAccess.toml` 의 `[exemption]`(타입 이름 → 이유)이고, 더는 `getInstance()` 가 없는 타입은 낡은 예외로 실패한다.

대상은 `Source/` · `Test/` · `Tools/ReflectionParser/` · `Tools/OnlineLoadBot/` 의 `.h` · `.cpp` · `.inl` 이다(생성 `.gen.cpp` 와 ThirdParty 제외).

  python Scripts/lint/gate/CheckServiceAccess.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import findFast, kLintTargetRelDirs  # noqa: E402
from common.RuleData import kKindTextList  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402
from SourceScan import FileFindings, SourceFile, SourceScanResult, joinFindings, scanSourceGates  # noqa: E402

_kListScanRoot = kLintTargetRelDirs
_kSuffixes = (".h", ".cpp", ".inl")
_kHeaderSuffixes = (".h", ".inl")

_kRuleSchema = {"allowed_engine_function": kKindTextList}
_kRuleData = LintGate.readRules("CheckServiceAccess", _kRuleSchema, requiredKeys=("allowed_engine_function",))
_kSetRemainingEngineFunction = frozenset(_kRuleData["allowed_engine_function"])

_kRetiredWindowCategory = "ServiceAccess/RetiredWindow"
_kSingletonCategory = "ServiceAccess/Singleton"

# 지워진 창구 다섯 모양. 이름 붙은 접근자는 아래 `_kEngineNamedAccessorRe` 가 따로 본다(허용 이름을 데이터에서 빼야 해서).
_kRetiredWindowRe = (
    (re.compile(r"\b(?:getService|getSceneService|isServiceBound)\s*<"), "getSubsystem<T>() / findSubsystem<T>() 를 쓰십시오"),
    (re.compile(r"\bgetBoundEngineServices\s*\("), "engine::getSubsystem<T>() / findSubsystem<T>() 를 쓰십시오"),
    (re.compile(r"\blocateSubsystem\s*<[^;]*?>\s*\(\s*(?:\w+::)*getEngineSubsystems\s*\(\s*\)\s*\)"),
     "engine::findSubsystem<T>() 를 쓰십시오"),
)
_kEngineNamedAccessorRe = re.compile(r"(?<![\w$])engine::(get[A-Z]\w*)\s*\(")

_kStaticGetInstanceRe = re.compile(r"\bstatic\b[^;{}()=]*?\bgetInstance\s*\(")
_kClassHeadRe = re.compile(r"\b(?:class|struct)\s+(?:(?:\w*API|alignas\s*\([^)]*\))\s+)*(\w+)")


def lineOfInternal(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def findEnclosingClassNames(code: str) -> list[tuple[int, str]]:
    """`{` 마다 그 중괄호가 여는 클래스 이름을 모아 (여는 위치, 이름) 으로 돌려줍니다. 클래스가 아닌 중괄호는 이름이 빈 문자열입니다."""
    listOpen: list[tuple[int, str]] = []
    segmentStart = 0
    for match in re.finditer(r"[;{}]", code):
        if match.group(0) == "{":
            segment = code[segmentStart:match.start()]
            head = _kClassHeadRe.search(segment) if re.search(r"\benum\b", segment) is None else None
            listOpen.append((match.start(), head.group(1) if head else ""))
        segmentStart = match.end()
    return listOpen


def classNameAtInternal(code: str, offset: int) -> str:
    """`offset` 를 감싸는 가장 안쪽 클래스의 이름입니다(없으면 빈 문자열)."""
    stack: list[str] = []
    listOpen = dict(findEnclosingClassNames(code))
    for match in re.finditer(r"[{}]", code[:offset]):
        if match.group(0) == "{":
            stack.append(listOpen.get(match.start(), ""))
        elif stack:
            stack.pop()
    for name in reversed(stack):
        if name:
            return name
    return ""


def findRetiredWindowUses(relative: str, text: str, code: str) -> list[str]:
    listOriginalLine = text.splitlines()
    listViolation: list[str] = []
    listHit: list[tuple[int, str, str]] = []
    for regex, advice in _kRetiredWindowRe:
        if findFast(regex, code) is not None:
            listHit.extend((match.start(), match.group(0), advice) for match in regex.finditer(code))
    for match in _kEngineNamedAccessorRe.finditer(code) if findFast(_kEngineNamedAccessorRe, code) is not None else ():
        if match.group(1) not in _kSetRemainingEngineFunction and match.group(1) != "getBoundEngineServices":  # 뒤쪽은 위 표가 이미 센다
            listHit.append((match.start(), match.group(0), "engine::getSubsystem<T>() / findSubsystem<T>() 를 쓰십시오"))
    for offset, spelling, advice in sorted(listHit):
        lineNumber = lineOfInternal(code, offset)
        listViolation.append(f"{relative}:{lineNumber}: [{_kRetiredWindowCategory}] {spelling.strip()} — {advice} | {listOriginalLine[lineNumber - 1].strip()}")
    return listViolation


def findSingletons(relative: str, text: str, code: str) -> list[str]:
    gate = CheckServiceAccessGate
    listOriginalLine = text.splitlines()
    listViolation: list[str] = []
    for _, className in findEnclosingClassNames(code):
        if className in gate.mapExemption:
            gate.seeExemption(className)   # 타입은 있는데 getInstance() 가 없으면 낡은 예외로 실패한다
    for match in _kStaticGetInstanceRe.finditer(code):
        typeName = classNameAtInternal(code, match.start())
        lineNumber = lineOfInternal(code, match.start())
        if typeName in gate.mapExemption:
            gate.useExemption(typeName)
            continue
        owner = typeName or "(클래스 밖)"
        listViolation.append(f"{relative}:{lineNumber}: [{_kSingletonCategory}] {owner}::getInstance() — 전역 단일체입니다. "
                             f"새 서비스는 서브시스템으로 만들어 범위에 넣으십시오 | {listOriginalLine[lineNumber - 1].strip()}")
    return listViolation


def findServiceAccessViolationsInFile(source: SourceFile) -> list[str]:
    relative = source.relative
    if relative.endswith(".gen.cpp") or "/ThirdParty/" in f"/{relative}":
        return []
    listViolation = findRetiredWindowUses(relative, source.text, source.masked)
    if relative.endswith(_kHeaderSuffixes):
        listViolation.extend(findSingletons(relative, source.text, source.masked))
    return listViolation


def findServiceAccessViolations(repositoryRoot: Path, listTargetFile: list[str] | None) -> list[str]:
    """`repositoryRoot`(셀프테스트의 조각 트리) 를 이 게이트 하나로 훑은 위반 — 게이트와 같은 훑기다."""
    gate = CheckServiceAccessGate()
    args = argparse.Namespace(files=listTargetFile)
    return gate.finishSourceScan(repositoryRoot, args, scanSourceGates(repositoryRoot, [(gate, args)])[0]).listViolation


class CheckServiceAccessGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 깨끗한 조각과 예외 쪽은 `selftest/CheckServiceAccessSelfTest.py` 가 본다."""

    description = "서비스 접근이 한 어휘(getSubsystem / findSubsystem)인지 · 헤더에 새 전역 단일체(getInstance)가 없는지 검사"
    buildComment = "Checking service access vocabulary and singletons..."
    timeoutSeconds = 60
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    sourceScanRoots = _kListScanRoot
    sourceSuffixes = _kSuffixes
    ruleSchema = _kRuleSchema
    violationHeader = "서비스 접근 위반"
    hint = (
        "  서비스 접근은 범위마다 getSubsystem<T>()(반드시 있음) / findSubsystem<T>()(없을 수 있음) 둘입니다:\n"
        "      engine::getSubsystem<TaskManager>()      game::findSubsystem<T>()      editor::findSubsystem<T>()      GameObjectManager::getSubsystem<T>()\n"
        "  새 서비스는 전역 단일체(static getInstance())가 아니라 서브시스템으로 만들어 범위에 넣습니다.\n"
        "  정말 단일체가 필요하면 Scripts/lint/rules/CheckServiceAccess.toml 의 [exemption] 에 타입 이름과 이유를 적습니다."
    )
    selfTestCases = [
        {
            "name": "옛 getService 조회",
            "expectedViolation": "Source/Probe/ProbeService.cpp:3: [ServiceAccess/RetiredWindow] getService<",
            "files": {
                "Source/Probe/ProbeService.cpp": "void probe()\n{\n    auto* pManager = game::getService<InputManager>();\n}\n",
            },
        },
        {
            "name": "이름 붙은 엔진 접근자",
            "expectedViolation": "Source/Probe/ProbeNamed.cpp:3: [ServiceAccess/RetiredWindow] engine::getTypeRegistry(",
            "files": {
                "Source/Probe/ProbeNamed.cpp": "void probe()\n{\n    engine::getTypeRegistry().find( 1 );\n}\n",
            },
        },
        {
            "name": "새 전역 단일체",
            "expectedViolation": "Source/Probe/ProbeSingleton.h:4: [ServiceAccess/Singleton] ProbeSingleton::getInstance()",
            "files": {
                "Source/Probe/ProbeSingleton.h": "class ProbeSingleton\n{\npublic:\n    static ProbeSingleton& getInstance();\n};\n",
            },
        },
        {
            "name": "getInstance() 를 더는 두지 않는 예외 타입(낡은 예외)",
            "expectedViolation": "[낡은 예외] 'RawAnimationCodec' 의 대상은 그대로인데 규칙을 어기지 않습니다",
            "files": {
                "Source/Probe/ProbeCodec.h": "class RawAnimationCodec\n{\n};\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 Source · Test · Tools 전체)")

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        return FileFindings(listViolation=findServiceAccessViolationsInFile(source))

    def finishSourceScan(self, repositoryRoot: Path, args: argparse.Namespace, result: SourceScanResult) -> GateResult:
        listViolation, _ = joinFindings(result.listFindings)
        listRetired = [line for line in listViolation if f"[{_kRetiredWindowCategory}]" in line]
        summary = (f"{' · '.join(_kListScanRoot)} — {_kRetiredWindowCategory} {len(listRetired)} · "
                   f"{_kSingletonCategory} {len(listViolation) - len(listRetired)}")
        return GateResult(listViolation=listViolation, summary=summary)


main = CheckServiceAccessGate.run


if __name__ == "__main__":
    sys.exit(main())
