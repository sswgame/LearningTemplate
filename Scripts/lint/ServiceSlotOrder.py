#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/ServiceSlotOrder.py

같은 파일 안에서 서비스 칸 선언(`SW_SERVICE_SLOT` · `SW_SERVICE_SLOT_CONST`)이 그 타입의 첫 사용(`getSubsystem<T>` · `findSubsystem<T>` ·
`ServiceSlotOf<T>` · 등록 줄 `SW_ENGINE_SERVICE*( T`)보다 뒤에 오는 곳을 찾는다.

**왜 린트인가.** 칸 선언은 `ServiceSlotOf<T>` 의 명시적 특수화다. 사용이 먼저 오면 기본 템플릿이 그 자리에서 인스턴스화되고, 뒤의 특수화는
"인스턴스화 뒤 특수화" 로 ill-formed 다 — clang · gcc 는 멈추지만 MSVC 는 잡지 않아 Windows 빌드는 지나가고 Linux 에서만 드러났다(XTestInputInjector).
선언 자리 규칙(헤더의 클래스 정의 바로 뒤, `ServiceSlot.h`)은 형식으로 강제할 수 없다 — 매크로가 어디에 놓이든 합법 C++ 이다.
글 연산이라 같은 파일 안만 본다(include 로 앞에 온 다른 헤더의 사용은 컴파일러가 본다).

`py -3 -m Scripts lint service-slot-order [--self-check]`, 커밋 훅(`Rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint.LintScope import LintScope, makeWholeScope, readTexts  # noqa: E402
from lint.NamePathArguments import blankCommentsAndStrings  # noqa: E402

kSlot = re.compile(r"\bSW_SERVICE_SLOT(?:_CONST)?\s*\(\s*(\w+)\s*,")


def makeUsePattern(typeName: str) -> re.Pattern[str]:
    qualified = r"(?:const\s+)?(?:::)?(?:\w+::)*" + re.escape(typeName) + r"\b"
    return re.compile(r"\b(?:getSubsystem|findSubsystem|ServiceSlotOf)\s*<\s*(?:std::remove_const_t\s*<\s*)?" + qualified + r"|\bSW_ENGINE_SERVICE\w*\s*\(\s*" + qualified)


def findProblems(listSource: list[tuple[str, str]]) -> list[str]:
    listProblem: list[str] = []
    for path, text in listSource:
        if "SW_SERVICE_SLOT" not in text:
            continue
        code = blankCommentsAndStrings(text)
        for slot in kSlot.finditer(code):
            lineStart = code.rfind("\n", 0, slot.start()) + 1
            if code[lineStart:slot.start()].lstrip().startswith("#define"):
                continue  # 매크로 정의 자신
            use = makeUsePattern(slot.group(1)).search(code, 0, slot.start())
            if use is not None:
                useLine = code.count("\n", 0, use.start()) + 1
                slotLine = code.count("\n", 0, slot.start()) + 1
                listProblem.append(f"{path}:{slotLine}: {slot.group(1)} 의 서비스 칸 선언이 {useLine} 줄의 사용보다 뒤다 — clang 은 '인스턴스화 뒤 특수화' 로 멈춘다. "
                                   "선언을 헤더의 클래스 정의 바로 뒤로(ServiceSlot.h)")
    return listProblem


def readSources(projectRoot: Path, scope: LintScope) -> list[tuple[str, str]]:
    return readTexts(projectRoot, scope.select(("Source",), (".cpp", ".h", ".inl")))


def checkServiceSlotOrder(projectRoot: Path, scope: LintScope) -> list[str]:
    return findProblems(readSources(projectRoot, scope))


def runSelfCheck() -> None:
    late = "void f() { (void)engine::findSubsystem<Foo>(); }\nSW_SERVICE_SLOT( Foo, Optional );\n"
    assert len(findProblems([("a.cpp", late)])) == 1, "사용 뒤 선언은 잡아야 한다"
    early = "class Foo {};\nSW_SERVICE_SLOT( Foo, Optional );\nvoid f() { (void)engine::getSubsystem<sw::Foo>(); }\n"
    assert findProblems([("a.cpp", early)]) == [], "선언 뒤 사용은 통과한다"
    other = "void f() { (void)engine::getSubsystem<FooBar>(); }\nSW_SERVICE_SLOT( Foo, Required );\n"
    assert findProblems([("a.cpp", other)]) == [], "이름이 다른 타입은 통과한다"
    registered = "SW_ENGINE_SERVICE( Foo, Shared );\nSW_SERVICE_SLOT( Foo, Required );\n"
    assert len(findProblems([("a.cpp", registered)])) == 1, "등록 줄도 사용이다"
    comment = "// engine::getSubsystem<Foo>()\nSW_SERVICE_SLOT( Foo, Required );\n"
    assert findProblems([("a.cpp", comment)]) == [], "주석은 통과한다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="서비스 칸 선언이 같은 파일의 첫 사용보다 뒤에 오는 곳을 찾는다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[ServiceSlotOrder] self-check OK")
        return ExitCode.Passed
    listProblem = checkServiceSlotOrder(getProjectRoot(), makeWholeScope(getProjectRoot()))
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[ServiceSlotOrder] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
