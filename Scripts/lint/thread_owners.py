"""
Scripts/lint/thread_owners.py

엔진 스레드 형식(`sw::thread` · `DedicatedWorker`)을 거치지 않고 표준 · OS 함수로 스레드를 바로 만드는 파일을 허용 목록(`Config/Rules/Lint/ThreadOwners.json` 의 `_owners`)과 맞춘다.

왜 린트인가. 엔진 스레드를 누가 만드는지는 형식이 막는다 — `sw::thread` 생성자와 `DedicatedWorker::start` 는 열쇠(`ThreadCreationKey`,
`Core/Concurrency/thread.h`)를 받고 열쇠는 그 헤더의 친구(주인)만 만든다. 남은 틈은 표준 · OS 이름(`std::thread` · `std::async` · `CreateThread` ·
`pthread_create`)이다 — 우리가 정의하지 않은 이름이라 형식으로 막을 수 없고, 모든 TU 가 거치는 `PlatformOSHeaders.h` 에서 `#pragma GCC poison` 으로 막으면
뒤에 오는 표준 헤더(`<thread>` 의 gthr)까지 멈춘다. 그래서 이 모양만 목록과 맞춘다.

- 모양: `std::thread`(형식으로 쓰는 곳 — `std::thread::id` 는 아니다) · `std::jthread` · `std::async` · `CreateThread` · `_beginthreadex` · `pthread_create`. 주석 · 문자열은 보지 않는다.
- 목록의 키는 확장자 없는 경로(`.h` · `.cpp` 둘 다 맞는다)다. 까닭 빈 줄 · 더는 그 모양을 쓰지 않는 줄은 표 형식(`rule_tables.ExceptionTable`)이 낸다.
- 테스트(`/Tests/`)는 경합을 일부러 만들므로 보지 않는다.

`py -3 -m Scripts lint thread-owners [--self-check]` 로 돌리고, 커밋 훅이 돌린다.
"""

from __future__ import annotations

import re
from typing import Mapping

from lint.lint_rule import LintRule, SourceFile
from lint.lint_scope import LintScope
from lint.rule_tables import ExceptionTable

THREAD_SHAPE = re.compile(r"\bstd::thread\b(?!\s*::)|\bstd::jthread\b|\bstd::async\s*\(|\bCreateThread\s*\(|\b_beginthreadex\s*\(|\bpthread_create\s*\(")
PREFILTER = ("std::thread", "std::jthread", "std::async", "CreateThread", "_beginthreadex", "pthread_create")


def find_thread_lines(file: SourceFile) -> list[int]:
    """스레드를 만드는 모양이 있는 줄 번호(1 부터). 주석 · 문자열은 뺀다."""
    if not any(word in file.text for word in PREFILTER):
        return []
    return [index + 1 for index, line in enumerate(file.code.split("\n")) if THREAD_SHAPE.search(line)]


def to_owner_key(relative: str) -> str:
    """`Source/A/B.cpp` → `Source/A/B` — 목록은 헤더와 본문을 한 주인으로 본다."""
    return relative.rsplit(".", 1)[0]


def find_problems(found: Mapping[str, list[int]], table: ExceptionTable, whole_repo: bool = True) -> list[str]:
    list_problem = [f"{path}:{list_line[0]}: 표준 · OS 함수로 스레드를 바로 만든다 — 계산 · 짧은 IO 는 `engine::runParallelByCost` · 태스크 · 스테이지로,"
                    f" OS 를 막고 기다려야 하면 `sw::thread`(주인은 `ThreadCreationKey` 의 친구)로 만든다(그래도 표준 형식이 꼭 있어야 하면 {table.hint('_owners')})"
                    for path, list_line in found.items() if not table.allows("_owners", to_owner_key(path))]
    return list_problem + table.list_problems(whole_repo)


class ThreadOwnersRule(LintRule):
    id = "thread-owners"
    group = "경계"
    blocks = "표준 · OS 함수로 스레드를 바로 만드는 파일"
    why_not_type = "엔진 스레드는 열쇠(ThreadCreationKey)가 막지만 표준 · OS 이름은 우리가 정의하지 않아 형식으로 막지 못한다"
    tables = ("",)

    def selects(self, path: str) -> bool:
        return path.startswith("Source/") and path.endswith((".h", ".cpp")) and "Tests" not in path.split("/")

    def inspect(self, file: SourceFile) -> list[int]:
        return find_thread_lines(file)

    def evaluate(self, scope: LintScope, found: Mapping[str, list[int]]) -> list[str]:
        return find_problems(found, self.load_exceptions("_owners"), scope.whole_repo)

    def self_check(self) -> None:
        def check(path: str, text: str, *list_owner: str) -> list[str]:
            list_line = find_thread_lines(SourceFile(path, text))
            return find_problems({path: list_line} if list_line else {}, ExceptionTable.make(self.id, _owners={owner: "까닭" for owner in list_owner}))

        bad = "void A::start()\n{\n    std::thread worker( &A::run, this );\n}\n"
        assert len(check("Source/A.cpp", bad)) == 1, "목록 밖 스레드는 잡아야 한다"
        assert check("Source/A.cpp", bad, "Source/A") == [], "목록의 주인은 통과해야 한다"
        keyed = "void A::start()\n{\n    _thread = sw::thread( ThreadCreationKey{}, &A::run, this );\n}\n"
        assert check("Source/A.cpp", keyed) == [], "엔진 스레드 형식은 열쇠가 막는다(린트가 보지 않는다)"
        raw = "HANDLE h = CreateThread( nullptr, 0, &run, nullptr, 0, nullptr );\n"
        assert len(check("Source/A.cpp", raw)) == 1, "OS 함수도 스레드를 만든다"
        identity = "bool A::isOwner() const { return _owner == std::this_thread::get_id(); }\natomic<std::thread::id> _owner;\n"
        assert check("Source/A.cpp", identity) == [], "스레드 번호만 쓰는 곳은 스레드를 만들지 않는다"
        comment = "// std::thread( &A::run, this ) 는 쓰지 않는다\nconst char* p = \"std::thread\";\n"
        assert check("Source/A.cpp", comment) == [], "주석 · 문자열은 통과해야 한다"
        stale = check("Source/A.cpp", "void f();\n", "Source/A")
        assert len(stale) == 1 and "낡은 예외 _owners 'Source/A'" in stale[0], "더는 스레드를 만들지 않는 항목은 낡은 예외다"
