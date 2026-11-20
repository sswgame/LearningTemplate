#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/thread_owners.py

엔진 스레드 형식(`sw::thread` · `DedicatedWorker`)을 거치지 않고 표준 · OS 함수로 스레드를 바로 만드는 파일을 허용 목록(`Config/Rules/Lint/ThreadOwners.json` 의 `_owners`)과 맞춘다.

**왜 린트인가.** 엔진 스레드를 누가 만드는지는 형식이 막는다 — `sw::thread` 생성자와 `DedicatedWorker::start` 는 열쇠(`ThreadCreationKey`,
`Core/Concurrency/thread.h`)를 받고 열쇠는 그 헤더의 친구(주인)만 만든다. 남은 틈은 표준 · OS 이름(`std::thread` · `std::async` · `CreateThread` ·
`pthread_create`)이다 — 우리가 정의하지 않은 이름이라 형식으로 막을 수 없고, 모든 TU 가 거치는 `PlatformOSHeaders.h` 에서 `#pragma GCC poison` 으로 막으면
뒤에 오는 표준 헤더(`<thread>` 의 gthr)까지 멈춘다. 그래서 이 모양만 목록과 맞춘다.

- 모양: `std::thread`(형식으로 쓰는 곳 — `std::thread::id` 는 아니다) · `std::jthread` · `std::async` · `CreateThread` · `_beginthreadex` · `pthread_create`. 주석 · 문자열은 보지 않는다.
- 목록의 키는 확장자 없는 경로(`.h` · `.cpp` 둘 다 맞는다)다. 까닭 빈 줄 · 더는 그 모양을 쓰지 않는 줄은 표 형식(`rule_tables.ExceptionTable`)이 낸다.
- 테스트(`/Tests/`)는 경합을 일부러 만들므로 보지 않는다.

`py -3 -m Scripts lint thread-owners [--self-check]` 로 돌리고, 커밋 훅(`rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.lint_scope import LintScope, make_whole_scope, read_texts  # noqa: E402
from lint.cpp_shape import strip_code  # noqa: E402
from lint.rule_tables import ExceptionTable  # noqa: E402

RULE_ID = "thread-owners"
THREAD_SHAPE = re.compile(r"\bstd::thread\b(?!\s*::)|\bstd::jthread\b|\bstd::async\s*\(|\bCreateThread\s*\(|\b_beginthreadex\s*\(|\bpthread_create\s*\(")
PREFILTER = ("std::thread", "std::jthread", "std::async", "CreateThread", "_beginthreadex", "pthread_create")


def find_thread_lines(text: str) -> list[int]:
    """글 하나에서 스레드를 만드는 모양이 있는 줄 번호(1 부터)를 돌려준다. 주석 · 문자열은 뺀다."""
    if not any(word in text for word in PREFILTER):
        return []
    code = strip_code(text)
    return [index + 1 for index, line in enumerate(code.split("\n")) if THREAD_SHAPE.search(line)]


def to_owner_key(relative: str) -> str:
    """`Source/A/B.cpp` → `Source/A/B` — 목록은 헤더와 본문을 한 주인으로 본다."""
    return relative.rsplit(".", 1)[0]


def find_problems(list_source: Iterable[tuple[str, str]], table: ExceptionTable, whole_repo: bool = True) -> list[str]:
    list_problem: list[str] = []
    for relative, text in list_source:
        list_line = find_thread_lines(text)
        if not list_line or table.allows("_owners", to_owner_key(relative)):
            continue
        list_problem.append(f"{relative}:{list_line[0]}: 표준 · OS 함수로 스레드를 바로 만든다 — 계산 · 짧은 IO 는 `engine::runParallelByCost` · 태스크 · 스테이지로,"
                           f" OS 를 막고 기다려야 하면 `sw::thread`(주인은 `ThreadCreationKey` 의 친구)로 만든다(그래도 표준 형식이 꼭 있어야 하면 {table.hint('_owners')})")
    return list_problem + table.list_problems(whole_repo)


def read_sources(project_root: Path, scope: LintScope) -> list[tuple[str, str]]:
    return read_texts(project_root, scope.select(("Source",), (".h", ".cpp"), ("Tests",)))


def check_thread_owners(project_root: Path, scope: LintScope) -> list[str]:
    return find_problems(read_sources(project_root, scope), ExceptionTable.load(project_root, RULE_ID, ("_owners",)), scope.whole_repo)


def run_self_check() -> None:
    def owners(*list_owner: str) -> ExceptionTable:
        return ExceptionTable.make(RULE_ID, _owners={owner: "까닭" for owner in list_owner})

    bad = "void A::start()\n{\n    std::thread worker( &A::run, this );\n}\n"
    assert len(find_problems([("Source/A.cpp", bad)], owners())) == 1, "목록 밖 스레드는 잡아야 한다"
    assert find_problems([("Source/A.cpp", bad)], owners("Source/A")) == [], "목록의 주인은 통과해야 한다"
    keyed = "void A::start()\n{\n    _thread = sw::thread( ThreadCreationKey{}, &A::run, this );\n}\n"
    assert find_problems([("Source/A.cpp", keyed)], owners()) == [], "엔진 스레드 형식은 열쇠가 막는다(린트가 보지 않는다)"
    raw = "HANDLE h = CreateThread( nullptr, 0, &run, nullptr, 0, nullptr );\n"
    assert len(find_problems([("Source/A.cpp", raw)], owners())) == 1, "OS 함수도 스레드를 만든다"
    identity = "bool A::isOwner() const { return _owner == std::this_thread::get_id(); }\natomic<std::thread::id> _owner;\n"
    assert find_problems([("Source/A.cpp", identity)], owners()) == [], "스레드 번호만 쓰는 곳은 스레드를 만들지 않는다"
    comment = "// std::thread( &A::run, this ) 는 쓰지 않는다\nconst char* p = \"std::thread\";\n"
    assert find_problems([("Source/A.cpp", comment)], owners()) == [], "주석 · 문자열은 통과해야 한다"
    stale = find_problems([("Source/A.cpp", "void f();\n")], owners("Source/A"))
    assert len(stale) == 1 and "낡은 예외 _owners 'Source/A'" in stale[0], "더는 스레드를 만들지 않는 항목은 낡은 예외다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="표준 · OS 함수로 스레드를 바로 만드는 파일을 허용 목록과 맞춘다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")
    if arguments.self_check:
        run_self_check()
        print("[ThreadOwners] self-check OK")
        return ExitCode.PASSED
    list_problem = check_thread_owners(get_project_root(), make_whole_scope(get_project_root()))
    for problem in list_problem:
        print(f"  - {problem}")
    print(f"[ThreadOwners] {'FAIL' if list_problem else 'OK'} ({len(list_problem)}건)")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
