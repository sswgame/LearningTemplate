#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""전역 변수의 `extern` 참조가 정의와 **같은 타입**인지 본다.

전역 변수 매크로는 첫 인자가 타입이고, 종류는 매크로 이름이 정한다(`Core/GlobalVariable/GlobalVariableManager.h`).

  SW_GLOBAL_VARIABLE( int32, gv_viewMode, 0, "…" );                       // 일반
  SW_TEST_GLOBAL_VARIABLE( int32, gv_benchMeshes, 0, "…" );               // 테스트용 — Shipping 에서 빠진다
  SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_profileFrames, 0, "…" );     // 테스트용 — Shipping 에 남는다
  SW_EXTERN_GLOBAL_VARIABLE( int32, gv_viewMode );                        // 참조 — 종류와 상관없이 같다

참조의 타입이 정의와 어긋나도 컴파일러가 늘 잡지는 못한다. 정의와 참조가 다른 TU 에 있으면 서로를 보지 않고, Itanium ABI(리눅스)는
변수 이름에 타입을 넣지 않아 링커도 조용히 같은 심볼로 잇는다 — `int32` 로 정의한 것을 `bool` 로 읽는 ODR 위반이 남는다. 그것을 여기서 막는다.
타입 글은 공백과 앞의 `::` · `sw::` 를 떼고 비교한다(`sw::string` 과 `string` 은 같다).

정의를 찾지 못한 참조는 판단하지 않는다(매크로 없이 `registerVariable` 로 등록한 것 등).

  python Scripts/lint/gate/CheckGlobalVariableKinds.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import blankMatch, collectSourceFiles, kLintTargetRelDirs, normalizePath  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kListScanRoot = kLintTargetRelDirs
_kExternMacro = "SW_EXTERN_GLOBAL_VARIABLE"
_kCallRe = re.compile(r"\b(SW_GLOBAL_VARIABLE|SW_TEST_GLOBAL_VARIABLE_SHIPPED|SW_TEST_GLOBAL_VARIABLE|SW_EXTERN_GLOBAL_VARIABLE)\s*\(")
_kIdentifierRe = re.compile(r"[A-Za-z_]\w*")

# 줄 번호를 지키려고 지운 자리는 공백으로 채운다(줄바꿈은 남긴다).
_kBlockCommentRe = re.compile(r"/\*.*?\*/", re.S)
_kLineCommentRe = re.compile(r"//[^\n]*")
_kStringRe = re.compile(r'"(?:\\.|[^"\\\n])*"')
_kDefineRe = re.compile(r"^[ \t]*#[ \t]*define(?:[^\n]*\\\n)*[^\n]*", re.M)


@dataclass
class GlobalVariableUse:
    """정의 또는 참조 하나."""

    name: str
    typeName: str
    macro: str
    location: str


def normalizeTypeInternal(typeText: str) -> str:
    """타입 글에서 공백과 앞의 `::` · `sw::` 를 뗍니다."""
    typeName = re.sub(r"\s+", "", typeText)
    typeName = typeName.removeprefix("::")
    return typeName.removeprefix("sw::")


def stripNonCodeInternal(text: str) -> str:
    """주석 · 문자열 · `#define` 을 지웁니다. 매크로 정의 안의 이름과 설명 문자열 안의 쉼표를 세지 않게 합니다."""
    text = _kBlockCommentRe.sub(blankMatch, text)
    text = _kLineCommentRe.sub(blankMatch, text)
    text = _kStringRe.sub(blankMatch, text)
    return _kDefineRe.sub(blankMatch, text)


def findClosingParenInternal(text: str, openIndex: int) -> int:
    """`openIndex` 의 여는 괄호와 짝이 되는 닫는 괄호 위치입니다. 없으면 -1 입니다."""
    depth = 0
    for index in range(openIndex, len(text)):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                return index
    return -1


def collectUses(repositoryRoot: Path) -> tuple[list[GlobalVariableUse], list[GlobalVariableUse]]:
    """저장소의 전역 변수 정의 목록과 참조 목록을 모읍니다."""
    listDefinition: list[GlobalVariableUse] = []
    listExtern: list[GlobalVariableUse] = []
    listPath = collectSourceFiles([repositoryRoot / r for r in _kListScanRoot], {".cpp", ".h", ".inl"})
    # 매크로 이름이 없는 파일은 주석 · 문자열을 지워 봐야 나올 것이 없다 — 천여 파일 중 수십 개만 남는다.
    for path, rawText in LintGate.readFiles(listPath, mustContain="GLOBAL_VARIABLE"):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        if "/ThirdParty/" in f"/{relative}":
            continue
        text = stripNonCodeInternal(rawText)

        for match in _kCallRe.finditer(text):
            closeIndex = findClosingParenInternal(text, match.end() - 1)
            if closeIndex < 0:
                continue
            listArgument = [argument.strip() for argument in text[match.end():closeIndex].split(",")]
            if len(listArgument) < 2 or _kIdentifierRe.fullmatch(listArgument[1]) is None:
                continue
            lineNumber = text.count("\n", 0, match.start()) + 1
            use = GlobalVariableUse(name=listArgument[1],
                                    typeName=normalizeTypeInternal(listArgument[0]),
                                    macro=match.group(1),
                                    location=f"{relative}:{lineNumber}")
            (listExtern if use.macro == _kExternMacro else listDefinition).append(use)
    return listDefinition, listExtern


def findKindMismatches(listDefinition: list[GlobalVariableUse], listExtern: list[GlobalVariableUse]) -> list[str]:
    """정의와 타입이 어긋나는 참조를 위반 문자열로 돌려줍니다."""
    mapDefinition = {definition.name: definition for definition in listDefinition}

    violations: list[str] = []
    for externUse in listExtern:
        definition = mapDefinition.get(externUse.name)
        if definition is None or definition.typeName == externUse.typeName:
            continue
        violations.append(
            f"[Global Variable Kind] {externUse.location}: {externUse.name} 을(를) {externUse.typeName} 로 참조하지만 "
            f"정의({definition.location}, {definition.macro})는 {definition.typeName} 입니다")
    return violations


class CheckGlobalVariableKindsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "전역 변수 정의와 extern 참조의 타입 일치 검사"
    buildComment = "Checking global variable definitions against their extern declarations..."
    timeoutSeconds = 30
    preCommitPattern = ("*.h", "*.cpp", "*.inl")
    preCommitFileArgument = ""
    violationHeader = "정의와 타입이 다른 전역 변수 참조"
    hint = (
        "  참조는 정의와 같은 타입으로 적습니다:\n"
        "      SW_TEST_GLOBAL_VARIABLE( int32, gv_x, 0, \"…\" );  ->  SW_EXTERN_GLOBAL_VARIABLE( int32, gv_x );\n"
        "  다른 TU 의 참조는 컴파일러가 정의와 맞춰 보지 않습니다."
    )
    selfTestCases = [
        {
            "name": "int32 정의를 float32 로 참조",
            "files": {
                "Source/Probe/ProbeGlobalVariable.cpp": "SW_TEST_GLOBAL_VARIABLE( int32, gv_probe, 0, \"probe\" );\n",
                "Source/Probe/ProbeGlobalVariable.h": "SW_EXTERN_GLOBAL_VARIABLE( float32, gv_probe );\n",
            },
        },
        {
            "name": "Shipping 에 남는 enum 정의를 다른 enum 으로 참조",
            "files": {
                "Source/Probe/ProbeKeep.cpp": "SW_TEST_GLOBAL_VARIABLE_SHIPPED( ProbeMode, gv_probeKeep, ProbeMode::A, \"probe, kept\" );\n",
                "Source/Probe/ProbeKeep.h": "SW_EXTERN_GLOBAL_VARIABLE( ProbeOtherMode, gv_probeKeep );\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        pass

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listDefinition, listExtern = collectUses(repositoryRoot)
        violations = findKindMismatches(listDefinition, listExtern)
        return GateResult(listViolation=violations,
                          summary=f"정의 {len(listDefinition)}개 · 참조 {len(listExtern)}개")


main = CheckGlobalVariableKindsGate.run


if __name__ == "__main__":
    sys.exit(main())
