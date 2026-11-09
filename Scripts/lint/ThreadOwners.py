#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/ThreadOwners.py

작업 시스템(`TaskManager`) 밖에서 스레드를 만드는 파일을 허용 목록(`Config/Dev/ThreadOwners.json`)과 맞춘다.

**왜 린트인가.** 자체 스레드는 워커 수가 TaskManager 와 따로 놀아 코어를 과포화하고, 잠들며 결과를 묻는 폴링을 부른다(2026-11-08 병렬 · 비동기 전수 조사).
그런데 `sw::thread` 는 OS 를 막고 기다려야 하는 곳(소켓 · 파일 감시 · 프로세스 · 크래시 · 렌더 스레드)에 꼭 필요한 정상 형식이라 형식으로 막을 수 없다.
그래서 스레드를 만드는 파일은 까닭과 함께 목록에 두고, 목록 밖에서 새로 만들면 멈춘다. 계산 · 짧은 IO 는 `engine::runParallelByCost` · 태스크 · 스테이지로 낸다
(기준은 `docs/agents/CppStyle.md` "Parallel and async — which tool where").

- 스레드를 만드는 모양: `sw::thread` · `std::thread`(형식으로 쓰는 곳 — `std::thread::id` 는 아니다) · `std::jthread` · `std::async` · `DedicatedWorker<…>` · `CreateThread` ·
  `_beginthreadex` · `pthread_create`. 주석 · 문자열은 보지 않는다.
- 목록 항목 `_path` 는 확장자 없는 경로(`.h` · `.cpp` 둘 다 맞는다)다. 더는 스레드를 만들지 않는 항목은 낡은 예외로 멈춘다(목록이 사실과 어긋나지 않게).
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
from lint.CppShape import stripCode  # noqa: E402

kConfigPath = "Config/Dev/ThreadOwners.json"
kThreadShape = re.compile(r"\b(?:sw|std)::thread\b(?!\s*::)|\bstd::jthread\b|\bstd::async\s*\(|\bDedicatedWorker\s*<|\bCreateThread\s*\(|\b_beginthreadex\s*\(|\bpthread_create\s*\(")
kPrefilter = ("thread", "DedicatedWorker", "async", "CreateThread")


def findThreadLines(text: str) -> list[int]:
    """글 하나에서 스레드를 만드는 모양이 있는 줄 번호(1 부터)를 돌려준다. 주석 · 문자열은 뺀다."""
    if not any(word in text for word in kPrefilter):
        return []
    code = stripCode(text)
    return [index + 1 for index, line in enumerate(code.split("\n")) if kThreadShape.search(line)]


def toOwnerKey(relative: str) -> str:
    """`Source/A/B.cpp` → `Source/A/B` — 목록은 헤더와 본문을 한 주인으로 본다."""
    return relative.rsplit(".", 1)[0]


def findProblems(listSource: Iterable[tuple[str, str]], setOwner: frozenset[str]) -> list[str]:
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
        listProblem.append(f"{relative}:{listLine[0]}: TaskManager 밖에서 스레드를 만든다 — 계산 · 짧은 IO 는 `engine::runParallelByCost` · 태스크 · 스테이지로 낸다"
                           f"(OS 를 막고 기다려야 하면 {kConfigPath} 에 까닭과 함께 적는다)")
    for owner in sorted(setOwner - setUsedOwner):
        listProblem.append(f"{kConfigPath}: `{owner}` 는 더는 스레드를 만들지 않는다 — 목록에서 뺀다")
    return listProblem


def readSources(projectRoot: Path) -> list[tuple[str, str]]:
    listSource: list[tuple[str, str]] = []
    for path in (projectRoot / "Source").rglob("*"):
        if path.suffix not in (".h", ".cpp") or "Tests" in path.parts:
            continue
        try:
            listSource.append((path.relative_to(projectRoot).as_posix(), path.read_text(encoding="utf-8")))
        except (UnicodeDecodeError, OSError):
            continue
    return listSource


def loadOwners(projectRoot: Path) -> frozenset[str]:
    """허용 목록입니다. 없거나 까닭이 빈 항목이 있으면 예외로 멈춘다(빈 목록으로 대신하지 않는다)."""
    data = json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))
    for entry in data["_listOwner"]:
        if not entry["_reason"].strip():
            raise ValueError(f"{kConfigPath}: `{entry['_path']}` 에 까닭이 없다")
    return frozenset(entry["_path"] for entry in data["_listOwner"])


def checkThreadOwners(projectRoot: Path) -> list[str]:
    return findProblems(readSources(projectRoot), loadOwners(projectRoot))


def runSelfCheck() -> None:
    bad = "void A::start()\n{\n    _thread = sw::thread( &A::run, this );\n}\n"
    assert len(findProblems([("Source/A.cpp", bad)], frozenset())) == 1, "목록 밖 스레드는 잡아야 한다"
    assert findProblems([("Source/A.cpp", bad)], frozenset({"Source/A"})) == [], "목록의 주인은 통과해야 한다"
    member = "class A\n{\n    DedicatedWorker<Job> _worker;\n};\n"
    assert len(findProblems([("Source/A.h", member)], frozenset())) == 1, "전용 작업 스레드도 스레드다"
    identity = "bool A::isOwner() const { return _owner == std::this_thread::get_id(); }\natomic<std::thread::id> _owner;\n"
    assert findProblems([("Source/A.cpp", identity)], frozenset()) == [], "스레드 번호만 쓰는 곳은 스레드를 만들지 않는다"
    comment = "// sw::thread( &A::run, this ) 는 쓰지 않는다\nconst char* p = \"std::thread\";\n"
    assert findProblems([("Source/A.cpp", comment)], frozenset()) == [], "주석 · 문자열은 통과해야 한다"
    stale = findProblems([("Source/A.cpp", "void f();\n")], frozenset({"Source/A"}))
    assert len(stale) == 1 and "더는" in stale[0], "더는 스레드를 만들지 않는 항목은 낡은 예외다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="TaskManager 밖에서 스레드를 만드는 파일을 허용 목록과 맞춘다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")
    if arguments.self_check:
        runSelfCheck()
        print("[ThreadOwners] self-check OK")
        return ExitCode.Passed
    listProblem = checkThreadOwners(getProjectRoot())
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[ThreadOwners] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
