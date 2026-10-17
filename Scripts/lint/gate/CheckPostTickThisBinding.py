#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""틱 뒤 실행 대기열(`deferPostTick` · `executeOrDeferPostTick`)에 `this` 를 묶은 메서드 델리게이트를 넣는 것을 막는다.

미룬 일은 틱이 끝난 뒤 돈다. 그 사이에 컴포넌트가 지워지면 `SW_DELEGATE_METHOD( ..., this )` 는 지워진 객체를 부른다(핸들을 거치지 않는다).
컴포넌트 자신에 대한 일은 `GameObjectManager::executeOrDeferPostTickOnComponent<T>( getHandle(), []( T& component ) { ... } )` 로 넘긴다 —
실행할 때 핸들로 다시 찾고, 사라졌거나 삭제 대기면 부르지 않는다(Object 헤더 주석).

  python Scripts/lint/gate/CheckPostTickThisBinding.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import kLintTargetRelDirs  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402
from SourceScan import FileFindings, SourceFile, SourceScanResult, joinFindings  # noqa: E402

_kQueueCall = r"\b(?:deferPostTick|executeOrDeferPostTick)\s*\(\s*"
#: `this` 토큰(`*this` 는 값 복사라 뺀다). 인스턴스 인자가 `static_cast<Base*>( this )` 처럼 감싸여도 잡는다.
_kThisToken = r"(?<!\*)\bthis\b"

#: 대기열 함수 이름 뒤 괄호 안의 첫 인자가 `this` 를 묶은 SW_DELEGATE_METHOD 인 모양(줄을 넘어도 잡는다).
_kThisBindingRe = re.compile(_kQueueCall + r"SW_DELEGATE_METHOD\s*\([^;]*?" + _kThisToken, re.DOTALL)
#: 대기열에 바로 넘긴 람다(맨 람다 또는 SW_DELEGATE_LAMBDA 로 감싼 것)의 캡처 목록.
_kLambdaCaptureRe = re.compile(_kQueueCall + r"(?:SW_DELEGATE_LAMBDA\s*\([^,;]*,\s*)?\[([^\]]*)\]", re.DOTALL)
#: 미리 만들어 둔 델리게이트 변수: `<이름> = SW_DELEGATE_METHOD( ..., this )`.
_kThisDelegateVariableRe = re.compile(r"\b(\w+)\s*(?:=|\{)\s*SW_DELEGATE_METHOD\s*\([^;]*?" + _kThisToken, re.DOTALL)


def _isUnsafeCapture(captureText: str) -> bool:
    """캡처 목록이 `this` 를 잡거나 기본 캡처(`=` · `&`)로 암묵적으로 잡을 수 있으면 참이다."""
    for item in (part.strip() for part in captureText.split(",")):
        if item in ("=", "&", "this"):
            return True
    return False


class CheckPostTickThisBindingGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "틱 뒤 실행에 this 메서드 델리게이트 금지 검사"
    buildComment = "Checking that post-tick work does not bind raw this..."
    timeoutSeconds = 30
    preCommitPattern = ("*.cpp", "*.h", "*.inl")
    preCommitFileArgument = "--files"
    sourceScanRoots = kLintTargetRelDirs
    sourceSuffixes = (".cpp", ".h", ".inl")
    violationHeader = "틱 뒤 실행에 this 를 묶은 델리게이트"
    hint = ("  미룬 사이에 컴포넌트가 지워질 수 있습니다. executeOrDeferPostTickOnComponent<T>( getHandle(), []( T& component ) { ... } ) 로\n"
            "  넘기면 실행할 때 핸들로 다시 찾고, 사라졌거나 삭제 대기면 부르지 않습니다(Source/Engine/Object/ 의 헤더 주석).")
    selfTestCases = [
        {
            "name": "executeOrDeferPostTick 에 this 메서드",
            "files": {
                "Source/Probe/Probe.cpp": "void Probe::f() { pManager->executeOrDeferPostTick( SW_DELEGATE_METHOD( GameObjectManager::PostTickDelegate, &Probe::g, this ) ); }\n",
            },
        },
        {
            "name": "줄을 넘긴 deferPostTick",
            "files": {
                "Source/Probe/Probe.cpp": "void Probe::f()\n{\n    pManager->deferPostTick(\n        SW_DELEGATE_METHOD( GameObjectManager::PostTickDelegate, &Probe::g,\n                            this ) );\n}\n",
            },
        },
        {
            "name": "감싼 this 인스턴스",
            "files": {
                "Source/Probe/Probe.cpp": "void Probe::f() { pManager->deferPostTick( SW_DELEGATE_METHOD( GameObjectManager::PostTickDelegate, &Base::g, static_cast<Base*>( this ) ) ); }\n",
            },
        },
        {
            "name": "SW_DELEGATE_LAMBDA 가 this 캡처",
            "files": {
                "Source/Probe/Probe.cpp": "void Probe::f() { pManager->executeOrDeferPostTick( SW_DELEGATE_LAMBDA( GameObjectManager::PostTickDelegate, [pManager, this]() { g(); } ) ); }\n",
            },
        },
        {
            "name": "맨 람다 기본 캡처 =",
            "files": {
                "Source/Probe/Probe.cpp": "void Probe::f() { pManager->deferPostTick( [=]() { g(); } ); }\n",
            },
        },
        {
            "name": "맨 람다 기본 캡처 &",
            "files": {
                "Source/Probe/Probe.cpp": "void Probe::f() { pManager->executeOrDeferPostTick( [&, value]() { g( value ); } ); }\n",
            },
        },
        {
            "name": "미리 만든 델리게이트 변수",
            "files": {
                "Source/Probe/Probe.cpp": "void Probe::f()\n{\n    auto func = SW_DELEGATE_METHOD( GameObjectManager::PostTickDelegate, &Probe::g, this );\n    pManager->deferPostTick( std::move( func ) );\n}\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        text = source.text
        if "PostTick" not in text:
            return None
        listMatch = list(_kThisBindingRe.finditer(text))
        listMatch += [match for match in _kLambdaCaptureRe.finditer(text) if _isUnsafeCapture(match.group(1))]
        for nameMatch in _kThisDelegateVariableRe.finditer(text):
            useRe = re.compile(_kQueueCall + r"(?:std::move\s*\(\s*)?" + re.escape(nameMatch.group(1)) + r"\s*\)")
            listMatch += list(useRe.finditer(text, nameMatch.end()))
        return FileFindings(listViolation=[f"{source.relative}:{text.count(chr(10), 0, match.start()) + 1}: {' '.join(match.group(0).split())}"
                                           for match in sorted(listMatch, key=lambda m: m.start())])

    def finishSourceScan(self, repositoryRoot: Path, args: argparse.Namespace, result: SourceScanResult) -> GateResult:
        listViolation, _ = joinFindings(result.listFindings)
        return GateResult(listViolation=listViolation, summary=f"C++ 파일 {result.fileCount}개")


main = CheckPostTickThisBindingGate.run


if __name__ == "__main__":
    sys.exit(main())
