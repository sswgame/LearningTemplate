#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/thread_owners.py

엔진 스레드 형식(`sw::thread` · `DedicatedWorker`)을 거치지 않고 표준 · OS 함수로 스레드를 바로 만드는 파일을 허용 목록(`Config/Rules/Lint/ThreadOwners.json`)과 맞춘다.

**왜 린트인가.** 엔진 스레드를 누가 만드는지는 형식이 막는다 — `sw::thread` 생성자와 `DedicatedWorker::start` 는 열쇠(`ThreadCreationKey`,
`Core/Concurrency/thread.h`)를 받고 열쇠는 그 헤더의 친구(주인)만 만든다. 남은 틈은 표준 · OS 이름(`std::thread` · `std::async` · `CreateThread` ·
`pthread_create`)이다 — 우리가 정의하지 않은 이름이라 형식으로 막을 수 없고, 모든 TU 가 거치는 `PlatformOSHeaders.h` 에서 `#pragma GCC poison` 으로 막으면
뒤에 오는 표준 헤더(`<thread>` 의 gthr)까지 멈춘다. 그래서 이 모양만 목록과 맞춘다.

- 모양: `std::thread`(형식으로 쓰는 곳 — `std::thread::id` 는 아니다) · `std::jthread` · `std::async` · `CreateThread` · `_beginthreadex` · `pthread_create`. 주석 · 문자열은 보지 않는다.
- 목록 항목 `_path` 는 확장자 없는 경로(`.h` · `.cpp` 둘 다 맞는다)다. 더는 그 모양을 쓰지 않는 항목은 낡은 예외로 멈춘다.
- 테스트(`/Tests/`)는 경합을 일부러 만들므로 보지 않는다.

`py -3 -m Scripts lint thread-owners [--self-check]` 로 돌리고, 커밋 훅(`rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.lint_scope import LintScope, make_whole_scope, read_texts  # noqa: E402
from lint.cpp_shape import strip_code  # noqa: E402

CONFIG_PATH = "Config/Rules/Lint/ThreadOwners.json"
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


def find_problems(list_source: Iterable[tuple[str, str]], set_owner: frozenset[str], whole_repo: bool = True) -> list[str]:
    list_problem: list[str] = []
    set_used_owner: set[str] = set()
    for relative, text in list_source:
        list_line = find_thread_lines(text)
        if not list_line:
            continue
        owner = to_owner_key(relative)
        if owner in set_owner:
            set_used_owner.add(owner)
            continue
        list_problem.append(f"{relative}:{list_line[0]}: 표준 · OS 함수로 스레드를 바로 만든다 — 계산 · 짧은 IO 는 `engine::runParallelByCost` · 태스크 · 스테이지로,"
                           f" OS 를 막고 기다려야 하면 `sw::thread`(주인은 `ThreadCreationKey` 의 친구)로 만든다(그래도 표준 형식이 꼭 있어야 하면 {CONFIG_PATH} 에 까닭과 함께)")
    for owner in sorted(set_owner - set_used_owner) if whole_repo else ():   # 낡은 항목은 저장소 전체를 봐야 안다
        list_problem.append(f"{CONFIG_PATH}: `{owner}` 는 더는 스레드를 만들지 않는다 — 목록에서 뺀다")
    return list_problem


def read_sources(project_root: Path, scope: LintScope) -> list[tuple[str, str]]:
    return read_texts(project_root, scope.select(("Source",), (".h", ".cpp"), ("Tests",)))


def load_owners(project_root: Path) -> frozenset[str]:
    """허용 목록입니다. 없거나 까닭이 빈 항목이 있으면 예외로 멈춘다(빈 목록으로 대신하지 않는다)."""
    data = json.loads((project_root / CONFIG_PATH).read_text(encoding="utf-8"))
    for entry in data["_listOwner"]:
        if not entry["_reason"].strip():
            raise ValueError(f"{CONFIG_PATH}: `{entry['_path']}` 에 까닭이 없다")
    return frozenset(entry["_path"] for entry in data["_listOwner"])


def check_thread_owners(project_root: Path, scope: LintScope) -> list[str]:
    return find_problems(read_sources(project_root, scope), load_owners(project_root), scope.whole_repo)


def run_self_check() -> None:
    bad = "void A::start()\n{\n    std::thread worker( &A::run, this );\n}\n"
    assert len(find_problems([("Source/A.cpp", bad)], frozenset())) == 1, "목록 밖 스레드는 잡아야 한다"
    assert find_problems([("Source/A.cpp", bad)], frozenset({"Source/A"})) == [], "목록의 주인은 통과해야 한다"
    keyed = "void A::start()\n{\n    _thread = sw::thread( ThreadCreationKey{}, &A::run, this );\n}\n"
    assert find_problems([("Source/A.cpp", keyed)], frozenset()) == [], "엔진 스레드 형식은 열쇠가 막는다(린트가 보지 않는다)"
    raw = "HANDLE h = CreateThread( nullptr, 0, &run, nullptr, 0, nullptr );\n"
    assert len(find_problems([("Source/A.cpp", raw)], frozenset())) == 1, "OS 함수도 스레드를 만든다"
    identity = "bool A::isOwner() const { return _owner == std::this_thread::get_id(); }\natomic<std::thread::id> _owner;\n"
    assert find_problems([("Source/A.cpp", identity)], frozenset()) == [], "스레드 번호만 쓰는 곳은 스레드를 만들지 않는다"
    comment = "// std::thread( &A::run, this ) 는 쓰지 않는다\nconst char* p = \"std::thread\";\n"
    assert find_problems([("Source/A.cpp", comment)], frozenset()) == [], "주석 · 문자열은 통과해야 한다"
    stale = find_problems([("Source/A.cpp", "void f();\n")], frozenset({"Source/A"}))
    assert len(stale) == 1 and "더는" in stale[0], "더는 스레드를 만들지 않는 항목은 낡은 예외다"


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
