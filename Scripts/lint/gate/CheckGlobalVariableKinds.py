#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""전역 변수의 정의와 `extern` 참조가 **같은 종류**인지 본다.

전역 변수 매크로는 두 종류다(`Core/GlobalVariable/GlobalVariableManager.h`).

  SW_GLOBAL_VARIABLE_INT( gv_viewMode, 0, "…" );                                  // 일반
  SW_TEST_GLOBAL_VARIABLE_INT( gv_benchMeshes, 0, "…" );                          // 테스트용 — Shipping 에서 빠진다
  SW_TEST_GLOBAL_VARIABLE_INT( gv_profileFrames, 0, "…", SW_KEEP_IN_SHIPPING );  // 테스트용 — Shipping 에 남는다

다른 TU 에서 참조할 때도 같은 모양을 쓴다(`SW_EXTERN_GLOBAL_VARIABLE_*` · `SW_EXTERN_TEST_GLOBAL_VARIABLE_*`, 같은 선택 인자).
가져다 쓰는 쪽이 "이 변수가 배포 빌드에 있는가" 를 참조 선언만 보고 알 수 있게 하려는 것이다.

종류가 어긋나도 컴파일러는 잡지 못한다 — 두 종류의 `extern` 선언은 같은 C++ 선언으로 펼쳐지기 때문이다. 그래서
참조가 "배포 빌드에서 바꿀 수 있다" 고 말하는데 정의는 빠지는 식의 거짓말을 여기서 막는다. 타입(`INT` · `ENUM` …)이 어긋나는 것도 같이 본다.

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

from common import collectSourceFiles, normalizePath, readTextFiles  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kListScanRoot = ("Source", "Test", "Tools/ReflectionParser")
_kCallRe = re.compile(r"\bSW_(EXTERN_)?(TEST_)?GLOBAL_VARIABLE_(BOOL|INT|FLOAT|STRING|ENUM)\s*\(")
_kIdentifierRe = re.compile(r"[A-Za-z_]\w*")
_kKeepToken = "SW_KEEP_IN_SHIPPING"

# 줄 번호를 지키려고 지운 자리는 공백으로 채운다(줄바꿈은 남긴다).
_kBlockCommentRe = re.compile(r"/\*.*?\*/", re.S)
_kLineCommentRe = re.compile(r"//[^\n]*")
_kStringRe = re.compile(r'"(?:\\.|[^"\\\n])*"')
_kDefineRe = re.compile(r"^[ \t]*#[ \t]*define(?:[^\n]*\\\n)*[^\n]*", re.M)


@dataclass
class GlobalVariableUse:
    """정의 또는 참조 하나."""

    name: str
    bTest: bool
    bKeep: bool
    typeName: str
    location: str

    def describe(self, bExtern: bool) -> str:
        """이 사용을 매크로 모양으로 적습니다."""
        prefix = "SW_EXTERN_" if bExtern else "SW_"
        macro = f"{prefix}{'TEST_' if self.bTest else ''}GLOBAL_VARIABLE_{self.typeName}"
        return f"{macro}( …, {_kKeepToken} )" if self.bKeep else macro


def blankInternal(match: re.Match) -> str:
    """맞은 부분을 줄바꿈만 남기고 공백으로 바꿉니다."""
    return re.sub(r"[^\n]", " ", match.group(0))


def stripNonCodeInternal(text: str) -> str:
    """주석 · 문자열 · `#define` 을 지웁니다. 매크로 정의 안의 이름과 설명 문자열 안의 쉼표를 세지 않게 합니다."""
    text = _kBlockCommentRe.sub(blankInternal, text)
    text = _kLineCommentRe.sub(blankInternal, text)
    text = _kStringRe.sub(blankInternal, text)
    return _kDefineRe.sub(blankInternal, text)


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
    for path, rawText in readTextFiles(listPath, mustContain="GLOBAL_VARIABLE_"):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        if "/ThirdParty/" in f"/{relative}":
            continue
        text = stripNonCodeInternal(rawText)

        for match in _kCallRe.finditer(text):
            closeIndex = findClosingParenInternal(text, match.end() - 1)
            if closeIndex < 0:
                continue
            listArgument = [argument.strip() for argument in text[match.end():closeIndex].split(",")]
            name = listArgument[0] if listArgument else ""
            if _kIdentifierRe.fullmatch(name) is None:
                continue
            lineNumber = text.count("\n", 0, match.start()) + 1
            use = GlobalVariableUse(name=name,
                                    bTest=match.group(2) is not None,
                                    bKeep=_kKeepToken in listArgument,
                                    typeName=match.group(3),
                                    location=f"{relative}:{lineNumber}")
            (listExtern if match.group(1) else listDefinition).append(use)
    return listDefinition, listExtern


def findKindMismatches(listDefinition: list[GlobalVariableUse], listExtern: list[GlobalVariableUse]) -> list[str]:
    """정의와 종류 · 선택 인자 · 타입이 어긋나는 참조를 위반 문자열로 돌려줍니다."""
    mapDefinition = {definition.name: definition for definition in listDefinition}

    violations: list[str] = []
    for externUse in listExtern:
        definition = mapDefinition.get(externUse.name)
        if definition is None:
            continue
        bSameKind = (definition.bTest, definition.bKeep, definition.typeName) == \
                    (externUse.bTest, externUse.bKeep, externUse.typeName)
        if bSameKind:
            continue
        violations.append(
            f"[Global Variable Kind] {externUse.location}: {externUse.name} 을(를) {externUse.describe(True)} 로 참조하지만 "
            f"정의({definition.location})는 {definition.describe(False)} 입니다")
    return violations


class CheckGlobalVariableKindsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "전역 변수 정의와 extern 참조의 종류 일치 검사 (일반 · 테스트용 · Shipping 유지)"
    buildComment = "Checking global variable definitions against their extern declarations..."
    timeoutSeconds = 30
    preCommitPattern = ("*.h", "*.cpp", "*.inl")
    preCommitFileArgument = ""
    violationHeader = "정의와 종류가 다른 전역 변수 참조"
    hint = (
        "  참조는 정의와 같은 모양으로 적습니다:\n"
        "      SW_TEST_GLOBAL_VARIABLE_INT( gv_x, 0, \"…\" );                       ->  SW_EXTERN_TEST_GLOBAL_VARIABLE_INT( gv_x );\n"
        "      SW_TEST_GLOBAL_VARIABLE_INT( gv_y, 0, \"…\", SW_KEEP_IN_SHIPPING );  ->  SW_EXTERN_TEST_GLOBAL_VARIABLE_INT( gv_y, SW_KEEP_IN_SHIPPING );\n"
        "  참조만 보고 그 변수가 배포 빌드에서 바뀔 수 있는지 알 수 있어야 합니다."
    )
    selfTestCases = [
        {
            "name": "테스트용 정의를 일반 extern 으로 참조",
            "files": {
                "Source/Probe/ProbeGlobalVariable.cpp": "SW_TEST_GLOBAL_VARIABLE_INT( gv_probe, 0, \"probe\" );\n",
                "Source/Probe/ProbeGlobalVariable.h": "SW_EXTERN_GLOBAL_VARIABLE_INT( gv_probe );\n",
            },
        },
        {
            "name": "Shipping 에 남는 정의를 SW_KEEP_IN_SHIPPING 없이 참조",
            "files": {
                "Source/Probe/ProbeKeep.cpp": "SW_TEST_GLOBAL_VARIABLE_INT( gv_probeKeep, 0, \"probe, kept\", SW_KEEP_IN_SHIPPING );\n",
                "Source/Probe/ProbeKeep.h": "SW_EXTERN_TEST_GLOBAL_VARIABLE_INT( gv_probeKeep );\n",
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
