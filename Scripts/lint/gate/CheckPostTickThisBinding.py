#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""틱 뒤 실행 대기열(`deferPostTick` · `executeOrDeferPostTick`)에 `this` 를 묶은 메서드 델리게이트를 넣는 것을 막는다.

미룬 일은 틱이 끝난 뒤 돈다. 그 사이에 컴포넌트가 지워지면 `SW_DELEGATE_METHOD( ..., this )` 는 지워진 객체를 부른다(핸들을 거치지 않는다).
컴포넌트 자신에 대한 일은 `GameObjectManager::executeOrDeferPostTickOnComponent<T>( getHandle(), []( T& component ) { ... } )` 로 넘긴다 —
실행할 때 핸들로 다시 찾고, 사라졌거나 삭제 대기면 부르지 않는다(Object README 함정 절).

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

#: 대기열 함수 이름 뒤 괄호 안의 첫 인자가 `this` 를 묶은 SW_DELEGATE_METHOD 인 모양(줄을 넘어도 잡는다).
_kThisBindingRe = re.compile(r"\b(?:deferPostTick|executeOrDeferPostTick)\s*\(\s*SW_DELEGATE_METHOD\s*\([^;]*?,\s*this\s*\)", re.DOTALL)


class CheckPostTickThisBindingGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "틱 뒤 실행에 this 메서드 델리게이트 금지 검사"
    buildComment = "Checking that post-tick work does not bind raw this..."
    timeoutSeconds = 30
    preCommitPattern = ("*.cpp", "*.h", "*.inl")
    preCommitFileArgument = ""
    violationHeader = "틱 뒤 실행에 this 를 묶은 델리게이트"
    hint = ("  미룬 사이에 컴포넌트가 지워질 수 있습니다. executeOrDeferPostTickOnComponent<T>( getHandle(), []( T& component ) { ... } ) 로\n"
            "  넘기면 실행할 때 핸들로 다시 찾고, 사라졌거나 삭제 대기면 부르지 않습니다(Source/Engine/Object/README.md).")
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
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listPath = self.selectTargetFiles(repositoryRoot, None, listScanRoot=kLintTargetRelDirs, suffixes=(".cpp", ".h", ".inl"))
        listViolation: list[str] = []
        for path in listPath:
            text = path.read_text(encoding="utf-8", errors="replace")
            if "SW_DELEGATE_METHOD" not in text:
                continue
            relativePath = path.relative_to(repositoryRoot).as_posix()
            for match in _kThisBindingRe.finditer(text):
                lineNumber = text.count("\n", 0, match.start()) + 1
                listViolation.append(f"{relativePath}:{lineNumber}: {' '.join(match.group(0).split())}")
        return GateResult(listViolation=listViolation, summary=f"C++ 파일 {len(listPath)}개")


main = CheckPostTickThisBindingGate.run


if __name__ == "__main__":
    sys.exit(main())
