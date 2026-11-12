#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/ThreadOwners.py

엔진 스레드 형식(`sw::thread` · `DedicatedWorker`)을 거치지 않고 표준 · OS 함수로 스레드를 바로 만드는 파일을 허용 목록(`Config/Rules/Lint/ThreadOwners.json`)과 맞춘다.

**왜 린트인가.** 엔진 스레드를 누가 만드는지는 형식이 막는다 — `sw::thread` 생성자와 `DedicatedWorker::start` 는 열쇠(`ThreadCreateKey`,
`Core/Concurrency/thread.h`)를 받고 열쇠는 그 헤더의 친구(주인)만 만든다. 남은 틈은 표준 · OS 이름(`std::thread` · `std::async` · `CreateThread` ·
`pthread_create`)이다 — 우리가 정의하지 않은 이름이라 형식으로 막을 수 없고, 모든 TU 가 거치는 `PlatformOSHeaders.h` 에서 `#pragma GCC poison` 으로 막으면
뒤에 오는 표준 헤더(`<thread>` 의 gthr)까지 멈춘다. 그래서 이 모양만 목록과 맞춘다.

- 모양: `std::thread`(형식으로 쓰는 곳 — `std::thread::id` 는 아니다) · `std::jthread` · `std::async` · `CreateThread` · `_beginthreadex` · `pthread_create`. 주석 · 문자열은 보지 않는다.
- 목록 항목 `_path` 는 확장자 없는 경로(`.h` · `.cpp` 둘 다 맞는다)다. 더는 그 모양을 쓰지 않는 항목은 낡은 예외로 멈춘다.
- 테스트(`/Tests/`)는 경합을 일부러 만들므로 보지 않는다.

`py -3 -m Scripts lint thread-owners [--self-check]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint.LintScope import LintScope, makeWholeScope, readTexts  # noqa: E402
from lint.CppShape import stripCode  # noqa: E402

kConfigPath = "Config/Rules/Lint/ThreadOwners.json"
kThreadShape = re.compile(r"\bstd::thread\b(?!\s*::)|\bstd::jthread\b|\bstd::async\s*\(|\bCreateThread\s*\(|\b_beginthreadex\s*\(|\bpthread_create\s*\(")
kPrefilter = ("std::thread", "std::jthread", "std::async", "CreateThread", "_beginthreadex", "pthread_create")


def findThreadLines(text: str) -> list[int]:
    """글 하나에서 스레드를 만드는 모양이 있는 줄 번호(1 부터)를 돌려준다. 주석 · 문자열은 뺀다."""
    if not any(word in text for word in kPrefilter):
        return []
    code = stripCode(text)
    return [index + 1 for index, line in enumerate(code.split("\n")) if kThreadShape.search(line)]


def toOwnerKey(relative: str) -> str:
    """`Source/A/B.cpp` → `Source/A/B` — 목록은 헤더와 본문을 한 주인으로 본다."""
    return relative.rsplit(".", 1)[0]


def findProblems(listSource: Iterable[tuple[str, str]], setOwner: frozenset[str], bWholeRepo: bool = True) -> list[str]:
    listProblem: list[str] = []
    setUsedOwner: set[str] = set()
    for relative, text in listSource:
        listLine = findThreadLines(text)
        if not listLine:
            continue
        owner = toOwnerKey(relative)
        if owner in setOwner:
            setUsedOwner.add(owner)
            continue
        listProblem.append(f"{relative}:{listLine[0]}: 표준 · OS 함수로 스레드를 바로 만든다 — 계산 · 짧은 IO 는 `engine::runParallelByCost` · 태스크 · 스테이지로,"
                           f" OS 를 막고 기다려야 하면 `sw::thread`(주인은 `ThreadCreateKey` 의 친구)로 만든다(그래도 표준 형식이 꼭 있어야 하면 {kConfigPath} 에 까닭과 함께)")
    for owner in sorted(setOwner - setUsedOwner) if bWholeRepo else ():   # 낡은 항목은 저장소 전체를 봐야 안다
        listProblem.append(f"{kConfigPath}: `{owner}` 는 더는 스레드를 만들지 않는다 — 목록에서 뺀다")
    return listProblem


def readSources(projectRoot: Path, scope: LintScope) -> list[tuple[str, str]]:
    return readTexts(projectRoot, scope.select(("Source",), (".h", ".cpp"), ("Tests",)))


def loadOwners(projectRoot: Path) -> frozenset[str]:
    """허용 목록입니다. 없거나 까닭이 빈 항목이 있으면 예외로 멈춘다(빈 목록으로 대신하지 않는다)."""
    data = json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))
    for entry in data["_listOwner"]:
        if not entry["_reason"].strip():
            raise ValueError(f"{kConfigPath}: `{entry['_path']}` 에 까닭이 없다")
    return frozenset(entry["_path"] for entry in data["_listOwner"])


def checkThreadOwners(projectRoot: Path, scope: LintScope) -> list[str]:
    return findProblems(readSources(projectRoot, scope), loadOwners(projectRoot), scope.bWholeRepo)


def runSelfCheck() -> None:
    bad = "void A::start()\n{\n    std::thread worker( &A::run, this );\n}\n"
    assert len(findProblems([("Source/A.cpp", bad)], frozenset())) == 1, "목록 밖 스레드는 잡아야 한다"
    assert findProblems([("Source/A.cpp", bad)], frozenset({"Source/A"})) == [], "목록의 주인은 통과해야 한다"
    keyed = "void A::start()\n{\n    _thread = sw::thread( ThreadCreateKey{}, &A::run, this );\n}\n"
    assert findProblems([("Source/A.cpp", keyed)], frozenset()) == [], "엔진 스레드 형식은 열쇠가 막는다(린트가 보지 않는다)"
    raw = "HANDLE h = CreateThread( nullptr, 0, &run, nullptr, 0, nullptr );\n"
    assert len(findProblems([("Source/A.cpp", raw)], frozenset())) == 1, "OS 함수도 스레드를 만든다"
    identity = "bool A::isOwner() const { return _owner == std::this_thread::get_id(); }\natomic<std::thread::id> _owner;\n"
    assert findProblems([("Source/A.cpp", identity)], frozenset()) == [], "스레드 번호만 쓰는 곳은 스레드를 만들지 않는다"
    comment = "// std::thread( &A::run, this ) 는 쓰지 않는다\nconst char* p = \"std::thread\";\n"
    assert findProblems([("Source/A.cpp", comment)], frozenset()) == [], "주석 · 문자열은 통과해야 한다"
    stale = findProblems([("Source/A.cpp", "void f();\n")], frozenset({"Source/A"}))
    assert len(stale) == 1 and "더는" in stale[0], "더는 스레드를 만들지 않는 항목은 낡은 예외다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="표준 · OS 함수로 스레드를 바로 만드는 파일을 허용 목록과 맞춘다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")
    if arguments.self_check:
        runSelfCheck()
        print("[ThreadOwners] self-check OK")
        return ExitCode.Passed
    listProblem = checkThreadOwners(getProjectRoot(), makeWholeScope(getProjectRoot()))
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[ThreadOwners] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
