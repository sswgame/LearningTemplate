#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""서비스의 저장소 완료 함수가 실패를 로그로 내면서 취소 여부를 묻지 않는 곳을 잡는다.

종료 때문에 취소된 저장소 일은 닫힌 연결의 결과(`Unavailable`)로 끝나 진짜 저장소 고장과 값이 같다(`IServiceStoreWork::markCancelled`).
그래서 완료 함수가 `SW_LOG_WARNING` · `SW_LOG_ERROR` 를 낼 때 `ServiceStoreCompletion::isCancelled` 를 묻지 않으면 종료 때마다 거짓 경고가 난다.

저장소 완료 함수는 두 모양으로 알아본다 — `ServiceStoreCompletion` 을 매개변수로 받는 함수, 그리고 `IServiceStoreWork` 를 잇는 클래스의 `complete`
(`Foo::complete` 로 밖에 정의한 `...Work::complete` 포함). 그 본문에 `SW_LOG_WARNING` · `SW_LOG_ERROR` 가 있고 `isCancelled(` 가 한 번도 없으면 위반이다.

형식으로 막지 못하는 틈이라 게이트로 둔다: 로그 호출은 함수 어디서든 부를 수 있는 매크로라 형식이 막지 못한다. 그래서 완료 함수를 형식(`ServiceStoreCompletion`)으로
알아볼 수 있게 두고, 로그를 내는 함수가 그 값을 한 번은 읽는지 본다. 로그마다 `isCancelled` 아래에 있는지까지는 보지 않는다(저장소와 무관한 로그가 같은 함수에 있을 수 있다).
`IServiceStoreWork` 의 `run` 안의 로그(저장소 스레드)는 보지 않는다 — 깨진 레코드 로그가 많아 오탐이 된다.

  python Scripts/lint/gate/CheckStoreCompletionLog.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.CppScope import blankPreprocessor, classifyHead, isLambdaOpen  # noqa: E402
from lint.LintGate import LintGate  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile  # noqa: E402

_kListScanRoot = ("Source",)
_kSuffixes = (".h", ".hpp", ".inl", ".cpp")

_kTokenRe = re.compile(r"[{};]")
_kLogRe = re.compile(r"\bSW_LOG_(?:WARNING|ERROR)(?:_ID)?\s*\(")
_kAskRe = re.compile(r"\bisCancelled\s*\(")
_kCompletionParamRe = re.compile(r"\bServiceStoreCompletion\b")
_kWorkScopeRe = re.compile(r"\bIServiceStoreWork\b")


def isStoreCompletion(name: str, head: str, bInWorkScope: bool) -> bool:
    """함수가 저장소 완료 함수인가: `ServiceStoreCompletion` 을 받거나, 일 클래스의 `complete` 다."""
    if _kCompletionParamRe.search(head) is not None:
        return True
    return name.endswith("complete") and (bInWorkScope or name.endswith("Work::complete"))


def findUnaskedLogs(source: SourceFile) -> list[str]:
    """취소 여부를 묻지 않고 로그를 내는 저장소 완료 함수를 위반 문자열로 돌려줍니다."""
    if source.relative.startswith("Source/") == False or _kLogRe.search(source.text) is None:
        return []
    code = blankPreprocessor(source.masked)
    listViolation: list[str] = []
    # 항목: (종류, 이름, 머리, 본문 시작, 일 클래스 안인가). 종류는 scope · function · block.
    listStack: list[tuple[str, str, str, int, bool]] = []
    headStart = 0
    for match in _kTokenRe.finditer(code):
        token = match.group()
        position = match.start()
        if token == "{":
            head = code[headStart:position]
            bInsideBody = bool(listStack) and listStack[-1][0] != "scope"
            if bInsideBody or isLambdaOpen(code, position):
                listStack.append(("block", "", "", position, False))
            else:
                kind = classifyHead(head)
                bInWorkScope = any(entry[0] == "scope" and entry[4] for entry in listStack)
                if kind == "scope":
                    listStack.append(("scope", "", head, position, _kWorkScopeRe.search(head) is not None or bInWorkScope))
                elif kind.startswith("function:"):
                    listStack.append(("function", kind[len("function:"):], head, position, bInWorkScope))
                else:
                    listStack.append(("block", "", "", position, False))
            headStart = position + 1
        elif token == "}":
            if listStack:
                kind, name, head, bodyStart, bInWorkScope = listStack.pop()
                if kind == "function" and isStoreCompletion(name, head, bInWorkScope):
                    body = code[bodyStart:position]
                    logMatch = _kLogRe.search(body)
                    if logMatch is not None and _kAskRe.search(body) is None:
                        lineNumber = code.count("\n", 0, bodyStart + logMatch.start()) + 1
                        listViolation.append(f"{source.relative}:{lineNumber}: {name} | {source.listLine[lineNumber - 1].strip()}")
            headStart = position + 1
        else:
            headStart = position + 1
    return listViolation


class CheckStoreCompletionLogGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "저장소 완료 함수가 로그를 내면서 ServiceStoreCompletion::isCancelled 를 묻지 않는 곳을 검사"
    buildComment = "Checking that store completions ask isCancelled before they log..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    sourceScanRoots = _kListScanRoot
    sourceSuffixes = _kSuffixes
    sourceSummary = f"{' · '.join(_kListScanRoot)} 의 저장소 완료 함수 로그"
    violationHeader = "취소 여부를 묻지 않고 로그를 내는 저장소 완료 함수"
    hint = (
        "  종료 때문에 취소된 저장소 일은 진짜 저장소 고장과 결과 값이 같습니다(Unavailable). 완료 함수는 ServiceStoreCompletion 을 받아\n"
        "  취소가 아닐 때만 경고 · 오류를 냅니다:\n"
        "      if ( result != ServiceStoreResult::Ok && completion.isCancelled() == false )\n"
        "          SW_LOG_WARNING( \"...\" );\n"
        "  complete() 에서는 IServiceStoreWork::getCompletion() 을 넘깁니다."
    )
    selfTestCases = [
        {
            "name": "ServiceStoreCompletion 을 받고도 묻지 않고 로그",
            "expectedViolation": "Source/Probe/ProbeApply.cpp:4: ProbeService::apply",
            "files": {
                "Source/Probe/ProbeApply.cpp": (
                    "void ProbeService::apply( bool bOk, ServiceStoreCompletion completion )\n"
                    "{\n"
                    "    if ( bOk == false )\n"
                    "        SW_LOG_WARNING( \"read failed\" );\n"
                    "}\n"
                ),
            },
        },
        {
            "name": "일 클래스의 complete 가 묻지 않고 로그",
            "expectedViolation": "Source/Probe/ProbeWork.cpp:7: complete",
            "files": {
                "Source/Probe/ProbeWork.cpp": (
                    "class ProbeWork final : public IServiceStoreWork\n"
                    "{\n"
                    "public:\n"
                    "    void run( IServiceStoreConnection& connection ) override {}\n"
                    "    void complete() override\n"
                    "    {\n"
                    "        SW_LOG_ERROR( \"failed\" );\n"
                    "    }\n"
                    "};\n"
                ),
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 Source 전체)")

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        return FileFindings(listViolation=findUnaskedLogs(source))


main = CheckStoreCompletionLogGate.run


if __name__ == "__main__":
    sys.exit(main())
