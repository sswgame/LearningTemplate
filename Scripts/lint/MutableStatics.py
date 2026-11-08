#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/FunctionStatics.py

함수 안 정적 가변 변수는 정적 등록 공통 틀 둘 — `SW_DEFINE_REGISTRATION_LIST`(`RegistrationList<T>`) · `SW_DEFINE_REGISTRATION_SLOT`(`RegistrationSlot<T>`) — 로만 둔다.

**왜 린트인가.** 함수 정적 변수는 합법 C++ 이라 형식으로 막지 못한다. 그런데 가변 함수 정적은 감춘 싱글턴이다(사용자 지적 2026-11-08):
런타임 상태 · 정책 · 신호가 엔진 · 에디터 범위 밖에 숨어 엔진을 다시 세워도 남고, 테스트가 서로 섞이고, 누가 쓰는지 보이지 않는다. 그런 값은 엔진 · 에디터
서브시스템이 든다. 다른 TU · DLL 의 정적 등록자가 범위보다 먼저 쓰는 정적 등록 목록 · 칸만 예외이고 그것도 공통 틀 하나로 쓴다(언리얼 compiled-in 등록과 같은 자리).
두 매크로는 함수 정적을 `#define` 안에 두므로 이 검사에 잡히지 않는다 — 다른 매크로가 같은 일을 하지 않게 매크로 본문은 `kMacroHomes` 만 허용한다.

허용(가변이 아니다): `const` · `constexpr` 값(포인터면 포인터 자체가 const), 참조, `thread_local`(스레드 자기 칸 — 공유 상태가 아니다), 정적 멤버 함수 선언.
예외 표는 없다 — 걸리면 서브시스템으로 옮기거나 등록 틀로 쓴다. 프로세스 바닥(크래시 처리 · 퓨텍스 · 정적 할당 저장소)처럼 엔진보다 먼저 서야 하는 것은
파일 범위의 상수 초기화 객체로 둔다(정적 초기화 순서와 무관하고 숨지 않는다).

`py -3 -m Scripts function-statics [--self-check]` 로 돌리고, 커밋 훅(`PreCommit.py`)이 돌린다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402

kExtensions = (".h", ".cpp", ".inl")
# 함수 정적을 매크로 본문에 둘 수 있는 곳(두 틀의 정의 매크로)
kMacroHomes = ("Source/Core/String/RegistrationList.h", "Source/Core/String/RegistrationSlot.h")
kScopeHead = re.compile(r"(?:\b(?:namespace|class|struct|union|enum)\b[^;{}()]*|extern\s+\"C\"\s*)$")
kFunctionHead = re.compile(r"(\)|\bconst|\bnoexcept|\boverride|\bfinal|\bmutable|->\s*[\w:<>*&, ]+|\btry)\s*$")
kInitListHead = re.compile(r"\)\s*:\s*[\w:]+\s*[({]")


def stripCode(text: str) -> str:
    """주석 · 문자열 · 전처리 줄을 같은 길이의 공백으로 바꿉니다(줄 번호를 지킨다)."""
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif c == '"' and i > 0 and text[i - 1] == "R" and re.match(r'R"([^(\s]*)\(', text[i - 1:]):
            m = re.match(r'R"([^(\s]*)\(', text[i - 1:])
            end = text.find(")" + m.group(1) + '"', i)
            end = n if end < 0 else end + len(m.group(1)) + 2
            out.append(re.sub(r"[^\n]", " ", text[i:end]))
            i = end
        elif c in "\"'":
            if c == "'" and i > 0 and (text[i - 1].isalnum() and text[i - 1] not in "uULR8"):
                out.append(c)  # 숫자 구분자(1'000)
                i += 1
                continue
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(c + " " * (j - i - 1) + (c if j < n else ""))
            i = j + 1
        else:
            out.append(c)
            i += 1
    stripped = "".join(out)
    return re.sub(r"(?m)^[ \t]*#.*(?:\\\n.*)*", lambda m: re.sub(r"[^\n]", " ", m.group(0)), stripped)


def isMutableStatic(statement: str) -> bool:
    """함수 안의 문장 하나가 가변 정적 변수 선언이면 true 입니다."""
    m = re.match(r"static\s+(.*)", statement, re.S)
    if m is None or statement.startswith(("static_assert", "static_cast")):
        return False
    rest = " ".join(m.group(1).split())
    if re.match(r"(constexpr|thread_local|inline\s+constexpr)\b", rest):
        return False
    flat = re.sub(r"\[[^\]]*\]", "[]", rest)
    while re.search(r"<[^<>]*>", flat):  # 템플릿 인자 안의 * 는 이 변수의 포인터가 아니다
        flat = re.sub(r"<[^<>]*>", "T", flat)
    declarator = re.split(r"[=({]", flat, maxsplit=1)[0]
    if "(" in rest.split("=")[0] and re.search(r"\w\s*\([^)]*\)\s*(const)?\s*$", rest):  # 함수 선언(지역 클래스의 정적 멤버 함수)
        return False
    if re.search(r"&\s*\w+\s*$", declarator):          # 참조는 다시 묶을 수 없다
        return False
    if re.search(r"\*\s*const\s+\w+(\s*\[[^\]]*\])*\s*$", declarator):  # T* const x
        return False
    if "*" not in declarator and re.search(r"(^|\s)const(\s|$)", declarator):  # const T x
        return False
    return True


def findMutableStatics(text: str) -> list[tuple[int, str]]:
    """함수 본문 안의 가변 정적 변수 선언을 (줄, 문장) 으로 돌려줍니다."""
    code = stripCode(text)
    stack: list[tuple[str, str]] = []   # (블록 종류, 블록 앞 머리)
    head = ""                           # 지금 깊이에서 마지막 ; · 블록 이후 쌓인 글
    found: list[tuple[int, str]] = []
    last = 0
    for m in re.finditer(r"[{};]", code):
        ch = m.group(0)
        segment = code[last:m.start()]
        head += segment
        inFunction = any(kind == "function" for kind, _ in stack)
        if inFunction:
            statement = segment.strip()
            if ch == "{":
                statement = head.strip()
            if statement.startswith("static") and isMutableStatic(statement):
                offset = m.start() - len(segment) + (len(segment) - len(segment.lstrip()))
                if ch == "{":
                    offset = m.start() - len(head) + (len(head) - len(head.lstrip()))
                found.append((code.count("\n", 0, offset) + 1, " ".join(statement.split())[:160]))
        if ch == "{":
            pre = head.rstrip()
            if inFunction:
                kind = "init" if re.search(r"(=|\breturn|[\w>\]])\s*$", pre) and not kFunctionHead.search(pre) else "function"
                if re.search(r"\b(if|for|while|switch|do|else|try|catch)\b[^;]*$", pre) or pre.endswith(")") or pre == "":
                    kind = "function"
            elif kScopeHead.search(pre.split(";")[-1].strip()):
                kind = "scope"
            elif kFunctionHead.search(pre) or ( kInitListHead.search(pre) and pre.endswith( ( "}", ")" ) ) ):
                kind = "function"
            else:
                kind = "init"
            stack.append((kind, head))
            head = ""
        elif ch == "}":
            if stack:
                kind, before = stack.pop()
                head = before + "{...}" if kind == "init" else ""
            else:
                head = ""
        else:  # ;
            head = ""
        last = m.end()
    return found


def iterSources(projectRoot: Path) -> Iterable[Path]:
    for path in (projectRoot / "Source").rglob("*"):
        if path.suffix in kExtensions and "ThirdParty" not in path.parts:
            yield path


def checkFunctionStatics(projectRoot: Path) -> list[str]:
    listProblem: list[str] = []
    for path in iterSources(projectRoot):
        relPath = path.relative_to(projectRoot).as_posix()
        if "/Tests/" in relPath or relPath.startswith("Source/Core/Testing/"):
            continue  # 테스트 고정물은 프로세스 수명 상태를 흉내 낸다 — 엔진 범위가 아니다
        text = path.read_text(encoding="utf-8", errors="replace")
        for line, statement in findMutableStatics(text):
            listProblem.append(f"{relPath}:{line}: 함수 정적 가변 변수 `{statement}` — 엔진 · 에디터 서브시스템이 들거나, 정적 등록이면 SW_DEFINE_REGISTRATION_LIST · SW_DEFINE_REGISTRATION_SLOT 로")
        if relPath not in kMacroHomes:
            for m in re.finditer(r"(?m)^[ \t]*#\s*define\b(?:.*\\\n)*.*", text):
                if re.search(r"\bstatic\b(?!_assert|_cast)\s+(?!constexpr|const\b|inline\b)[\w:<>]+[^;\\]*\bs_\w+", m.group(0)) and "{" in m.group(0):
                    line = text.count("\n", 0, m.start()) + 1
                    if re.search(r"\)\s*\\?\s*\{", m.group(0)):
                        listProblem.append(f"{relPath}:{line}: 매크로 본문의 함수 정적 가변 변수 — 정적 등록은 공통 틀 매크로(SW_DEFINE_REGISTRATION_LIST · SLOT)를 감싼다")
    return listProblem


def selfCheck() -> bool:
    def statics(code: str) -> list[str]:
        return [s for _, s in findMutableStatics(code)]

    assert statics("int f() { static int s_x = 0; return ++s_x; }") == ["static int s_x = 0"]
    assert statics("int f() { static const int s_x = 3; return s_x; }") == []
    assert statics("int f() { static constexpr int kX = 3; return kX; }") == []
    assert statics("int f() { static const char* s_p = \"a\"; return 0; }") == ["static const char* s_p = \" \""]
    assert statics("int f() { static const char* const s_p = nullptr; return 0; }") == []
    assert statics("T& f() { static T& s_r = g(); return s_r; }") == []
    assert statics("void f() { static thread_local int t_x; }") == []
    assert statics("struct A { static int s_member; };") == []
    assert statics("namespace n { static int s_file = 0; }") == []
    assert statics("A::A() : _a{ 0 }, _b{} { static uint64 s_next = 1; _a = s_next++; }") == ["static uint64 s_next = 1"]
    assert statics("void f() { if ( x ) { static Registry s_registry{ 1 }; } }") == ["static Registry s_registry"]
    assert statics("void f() { struct L { static void g() { } }; }") == []
    assert statics("void f() { auto l = []() { static int s_y; }; }") == ["static int s_y"]
    assert statics("void f() { static const T* const s_arr[static_cast<uint32>( E::Count )] = { a }; }") == []
    assert statics("void f() { static const vector<T*> s_empty; }") == []
    assert statics("void f() { static_assert( true ); int x = static_cast<int>( 1.0 ); }") == []
    assert statics("#define M() static int s_hidden;\nvoid f() {}") == []
    print("[function-statics] self-check OK")
    return True


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[3] if __doc__ else None)
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args(argv)
    if args.self_check:
        return ExitCode.Passed if selfCheck() else ExitCode.Violation
    listProblem = checkFunctionStatics(getProjectRoot())
    for line in listProblem:
        print(f"  - {line}")
    print(f"[function-statics] {'FAIL' if listProblem else 'OK'} ({len(listProblem)})")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
