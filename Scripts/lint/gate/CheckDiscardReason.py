#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckDiscardReason.py

실패 가능 함수(load · read · write · apply … — `CheckFallibleNodiscard` 의 동사)의 결과를 `(void)` 로 버리는 자리는 **왜 버려도 되는지** 한 줄을 든다.

그 함수들은 `[[nodiscard]]` 라 결과를 그냥 버리면 빌드가 선다(`-Werror=unused-result`). `(void)` 는 그 경고를 끄는 길이라, 이유가 없으면
읽기 · 쓰기 · 적용의 실패를 조용히 삼킨 자리와 일부러 버린 자리를 가를 수 없다. 이유는 같은 줄 끝의 `//` 주석, 호출이 여러 줄이면 `;` 까지의
어느 줄 끝, 또는 바로 윗줄의 `//` 주석이다. 흔한 이유: 피호출자가 실패를 스스로 로그로 남긴다(`FileUtil::removeFile` 은 경고를 찍는다) ·
없어도 되는 선택 파일이다 · 이미 같은 프레임에서 다른 길로 알렸다.

그 밖의 `(void)` (값을 돌려주는 조회 · 쓰지 않는 인자 표시)는 이 규칙 밖이다(docs/agents/NamingCpp.md).

  python Scripts/lint/gate/CheckDiscardReason.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.CodeText import blankComments, blankCommentsAndLiterals  # noqa: E402
from common.Constants import kLintTargetRelDirs  # noqa: E402
from lint.SharedRules import readFallibleVerbs  # noqa: E402
from lint.LintGate import GateResult, LintGate  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile, SourceScanResult, joinFindings  # noqa: E402

_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx")
#: `(void)` 뒤의 호출 — 마지막 이름(`a.b->c::load`)을 뽑는다. 템플릿 인자 `<…>` 는 건너뛴다.
_kVoidCallRe = re.compile(r"\(\s*void\s*\)\s*(?P<callee>[A-Za-z_][\w:.\->]*?)\s*(?:<[^;()]*?>)?\s*\(")
#: 실패 가능 동사(또는 `re` + 동사)로 시작하는 이름 — `CheckFallibleNodiscard` 와 같은 판정.
_kFallibleNameRe = re.compile(r"^(?:re)?(?:" + "|".join(readFallibleVerbs()) + r")(?:[A-Z0-9_]\w*)?$")


def findFallibleDiscards(text: str, source: SourceFile | None = None) -> list[tuple[int, str, str]]:
    """
    파일 하나에서 실패 가능 함수의 결과를 `(void)` 로 버리는 자리를 (줄 번호, 함수 이름, 이유 — 없으면 빈 글자)로 돌려줍니다.
    `RunDiscardReasons` 보고서도 이 판정을 쓴다. 게이트는 함께 도는 게이트와 가린 글을 나눠 쓰려고 `source` 를 넘긴다.
    """
    listOriginal = text.splitlines()
    listNoComment = (blankComments(text) if source is None else source.noComment).splitlines()
    listCode = blankCommentsAndLiterals(text).splitlines() if source is None else source.listMaskedLine
    listResult: list[tuple[int, str, str]] = []

    def commentOfInternal(index: int) -> str:
        """그 줄 끝의 `//` 주석 글자(없으면 빈 글자)."""
        if index >= len(listOriginal) or listOriginal[index] == listNoComment[index]:
            return ""
        position = listOriginal[index].find("//", len(listNoComment[index].rstrip()))
        return listOriginal[index][position + 2:].strip() if position >= 0 else ""

    for index, line in enumerate(listCode):
        for match in _kVoidCallRe.finditer(line):
            name = re.split(r"->|\.|::", match.group("callee"))[-1]
            if _kFallibleNameRe.match(name) is None:
                continue
            reason = ""
            cursor = index
            while cursor < len(listCode):
                reason = commentOfInternal(cursor)
                if reason or ";" in listCode[cursor]:
                    break
                cursor += 1
            if not reason and index > 0 and listOriginal[index - 1].strip().startswith("//"):
                reason = listOriginal[index - 1].strip()[2:].strip()
            listResult.append((index + 1, name, reason))
    return listResult


class CheckDiscardReasonGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "실패 가능 함수(load · read · apply …)의 결과를 (void) 로 버리는 자리에 이유 한 줄이 있는지 검사"
    buildComment = "Checking that discarded fallible results carry a reason..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in kLintTargetRelDirs)
    preCommitFileArgument = "--files"
    sourceScanRoots = kLintTargetRelDirs
    sourceSuffixes = _kSuffixes
    violationHeader = "이유 없이 버린 실패 가능 결과"
    hint = ("  (void)FileUtil::removeFile( path ); // 없으면 할 일이 없고, 실패는 removeFile 이 경고로 남긴다\n"
            "  이유를 댈 수 없으면 버리지 말고 처리하거나(되돌리기 · 오류 로그 · 단언) 위로 돌려줍니다.")
    selfTestCases = [
        {"name": "이유 없이 버린 읽기", "expectedViolation": "Source/Probe/A.cpp:3: (void)readBytes(…) 에 이유가 없습니다", "files": {"Source/Probe/A.cpp": "void f()\n{\n    (void)readBytes( pData, size );\n}\n"}},
        {"name": "이유 없이 버린 멤버 적용(여러 줄 호출)", "expectedViolation": "Source/Probe/B.cpp:3: (void)applyOverrides(…) 에 이유가 없습니다",
         "files": {"Source/Probe/B.cpp": "void f()\n{\n    (void)pAsset->applyOverrides(\n        listOverride );\n}\n"}},
        {"name": "re + 동사 · 이름공간 호출", "expectedViolation": "Test/Probe/C.cpp:3: (void)removeFile(…) 에 이유가 없습니다", "files": {"Test/Probe/C.cpp": "void f()\n{\n    (void)sw::FileUtil::removeFile( path );\n}\n"}},
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        if "void" not in source.text:
            return None
        listDiscard = findFallibleDiscards(source.text, source)
        return FileFindings(listViolation=[f"{source.relative}:{lineNumber}: (void){name}(…) 에 이유가 없습니다 | {source.listLine[lineNumber - 1].strip()}"
                                           for lineNumber, name, reason in listDiscard if not reason], value=len(listDiscard))

    def finishSourceScan(self, repositoryRoot: Path, args: argparse.Namespace, result: SourceScanResult) -> GateResult:
        listViolation, _ = joinFindings(result.listFindings)
        countDiscard = sum(findings.value or 0 for findings in result.listFindings)
        return GateResult(listViolation=listViolation, summary=f"실패 가능 결과를 버린 자리 {countDiscard}")


main = CheckDiscardReasonGate.run

if __name__ == "__main__":
    sys.exit(main())
