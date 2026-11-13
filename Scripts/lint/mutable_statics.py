#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/mutable_statics.py

가변 정적 저장소 — 함수 정적 변수, 이름공간 범위(익명 이름공간 · `static` · 이름 있는 이름공간)의 변수, 클래스의 `static` 데이터 멤버 — 를 두지 않는다.
정적 등록 공통 틀(`SW_DEFINE_REGISTRATION_LIST` · `SW_DEFINE_LOCKED_REGISTRATION_LIST` · `SW_DEFINE_REGISTRATION_SLOT` · `SW_DEFINE_REGISTRATION_CHAIN`)과 예외 표 `Config/Rules/Lint/MutableStaticExceptions.json` 의 자리만 허용한다.

**왜 린트인가.** 가변 정적은 합법 C++ 이라 형식으로 막지 못한다. 그런데 함수 정적이든 파일 범위든 가변 정적은 싱글턴이다(사용자 지적 2026-11-08 —
함수 정적을 익명 이름공간 · atomic 전역으로 옮기는 것은 자리만 바꾼 같은 싱글턴이다): 런타임 상태 · 정책 · 신호 · 번호 발급기가 엔진 · 에디터 범위 밖에
숨어 엔진을 다시 세워도 남고, 테스트가 서로 섞이고, 누가 쓰는지 보이지 않는다. 그런 값은 주인 객체(서비스 · 매니저 · 디바이스 · 레지스트리)의 멤버다.
한 번 알리는 깃발은 `SW_LOG_*_ONCE_ID`. 다른 TU · DLL 의 정적 등록자가 범위보다 먼저 쓰는 정적 등록 목록 · 칸만 공통 틀로 쓴다(언리얼 compiled-in 등록과
같은 자리). 그 매크로들은 함수 정적을 `#define` 안에 두므로 이 검사에 잡히지 않는다 — 다른 매크로가 같은 일을 하지 않게 매크로 본문은 `MACRO_ALLOWED_FILES` 만 허용한다.

허용(가변이 아니다): `const` · `constexpr` 값(포인터면 포인터 자체가 const), 참조, `thread_local`(스레드 자기 칸 — 공유 상태가 아니다), 함수 선언, `extern` 선언(정의 쪽을 본다).
예외 표는 까닭(`_why`)이 필수다 — 할당 · 엔진보다 먼저 도는 침입형 등록 머리도 틀(`SW_DEFINE_REGISTRATION_CHAIN`)로 두므로, 정말 프로세스 수명인 바닥(크래시 처리기 버퍼 ·
퓨텍스 버킷 · 플랫폼 진입점의 앱 자리)만 적는다. 항목은 `파일` + `이름`(변수 이름)이고, 이름이 소스에서 사라진 항목도 오류다(표가 썩지 않게).

`py -3 -m Scripts lint mutable-statics [--self-check]` 로 돌리고, 커밋 훅(`rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.lint_scope import LintScope, make_whole_scope, read_text  # noqa: E402

EXTENSIONS = (".h", ".cpp", ".inl")
# 함수 정적을 매크로 본문에 둘 수 있는 곳(세 틀의 정의 매크로)
MACRO_ALLOWED_FILES = ("Source/Core/Registry/RegistrationList.h", "Source/Core/Registry/RegistrationSlot.h", "Source/Core/Registry/RegistrationChain.h")
EXCEPTION_FILE = "Config/Rules/Lint/MutableStaticExceptions.json"
NAMESPACE_HEAD = re.compile(r"(?:\bnamespace\b[^;{}()]*|extern\s+\"C\"\s*)$")
CLASS_HEAD = re.compile(r"\b(?:class|struct|union)\b[^;{}()]*$")
ENUM_HEAD = re.compile(r"\benum\b[^;{}()]*$")
FUNCTION_HEAD = re.compile(r"(\)|\bconst|\bnoexcept|\boverride|\bfinal|\bmutable|->\s*[\w:<>*&, ]+|\btry)\s*$")
INIT_LIST_HEAD = re.compile(r"\)\s*:\s*[\w:]+\s*[({]")
LEADING_MACRO = re.compile(r"^(?:[A-Z][A-Z0-9_]+\s*\((?:[^()]|\([^()]*\))*\)\s*)+")
SKIP_HEAD = re.compile(r"^(using|typedef|template|friend|extern|namespace|class|struct|union|enum|return|operator)\b")
WHERE_TEXT = {"function": "함수 정적", "namespace": "이름공간 범위", "class": "클래스 static 데이터 멤버"}


def strip_code(text: str) -> str:
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
    return re.sub(r"(?m)^[ \t]*#(?:[^\n]*\\\r?\n)*[^\n]*", lambda m: re.sub(r"[^\n]", " ", m.group(0)), stripped)


def flatten_declaration(rest: str) -> str:
    """배열 크기 · 템플릿 인자를 접습니다(그 안의 * · ( 는 이 변수의 것이 아니다)."""
    flat = re.sub(r"\[[^\]]*\]", "[]", " ".join(rest.split()))
    while re.search(r"<[^<>]*>", flat):
        flat = re.sub(r"<[^<>]*>", "T", flat)
    return flat


def declarator_of(statement: str) -> str:
    """첫 = · { · ( 앞의 선언자 부분입니다."""
    return re.split(r"[=({]", flatten_declaration(statement), maxsplit=1)[0]


def declared_name(statement: str) -> str:
    """선언 문장의 변수 이름입니다(선언자의 마지막 식별자)."""
    m = re.search(r"(\w+)(\s*\[\])*\s*$", declarator_of(statement))
    return m.group(1) if m else ""


def is_mutable_static(statement: str) -> bool:
    """`static ...` 문장 하나가 가변 정적 변수 선언이면 true 입니다."""
    m = re.match(r"static\s+(.*)", statement, re.S)
    if m is None or statement.startswith(("static_assert", "static_cast")):
        return False
    rest = re.sub(r"^inline\s+", "", " ".join(m.group(1).split()))
    if re.match(r"(constexpr|thread_local|consteval|constinit\s+const)\b", rest):
        return False
    flat = flatten_declaration(rest)
    first = re.search(r"[=({]", flat)
    if first is not None and first.group(0) == "(":   # 함수 선언(괄호 생성자 변수는 쓰지 않는다 — 중괄호 초기화가 저장소 관례)
        return False
    declarator = flat[:first.start()] if first else flat
    if not re.search(r"[\w>*&]\s+[*&]*\s*\w+(\s*\[\])*\s*$|[*&]\s*\w+(\s*\[\])*\s*$", declarator):  # 형식 + 이름 꼴이 아니다
        return False
    if re.search(r"&\s*\w+\s*$", declarator):          # 참조는 다시 묶을 수 없다
        return False
    if re.search(r"\*\s*const\s+\w+(\s*\[\])*\s*$", declarator):  # T* const x
        return False
    if "*" not in declarator and re.search(r"(^|\s)const(\s|$)", declarator):  # const T x
        return False
    return True


def is_mutable_namespace_variable(statement: str) -> bool:
    """이름공간 범위의 문장 하나가 가변 변수 정의면 true 입니다(`static` 이 없어도 — 익명 이름공간 · 이름 있는 이름공간의 전역)."""
    s = LEADING_MACRO.sub("", " ".join(statement.split()))
    if s == "" or SKIP_HEAD.match(s) or s.startswith(("static_assert", "static_cast")):
        return False
    s = re.sub(r"^(?:(?:static|inline)\s+)+", "", s)
    if re.search(r"::\s*~?\w+(\s*\[\])*\s*$", declarator_of(s)):
        return False  # 클래스 정적 멤버의 바깥 정의 — 클래스 안 선언 쪽을 본다
    return is_mutable_static("static " + s)


def find_mutable_statics(text: str) -> list[tuple[int, str, str, str]]:
    """가변 정적 저장소 선언을 (줄, 자리, 이름, 문장) 으로 돌려줍니다. 자리는 function · namespace · class 입니다."""
    code = strip_code(text)
    stack: list[tuple[str, str]] = []   # (블록 종류, 블록 앞 머리)
    head = ""                           # 지금 깊이에서 마지막 ; · 블록 이후 쌓인 글
    found: list[tuple[int, str, str, str]] = []
    last = 0

    def report(where: str, statement: str, offset: int) -> None:
        flat = " ".join(statement.split())
        body = LEADING_MACRO.sub("", flat)
        body = re.sub(r"^(?:(?:static|inline)\s+)+", "", body)
        found.append((code.count("\n", 0, offset) + 1, where, declared_name(body), flat[:160]))

    for m in re.finditer(r"[{};]", code):
        ch = m.group(0)
        segment = code[last:m.start()]
        head += segment
        in_function = any(kind == "function" for kind, _ in stack)
        at_scope = not in_function and all(kind in ("namespace", "class") for kind, _ in stack)
        if in_function:
            statement = segment.strip()
            if ch == "{":
                statement = head.strip()
            if statement.startswith("static") and is_mutable_static(statement):
                offset = m.start() - len(segment) + (len(segment) - len(segment.lstrip()))
                if ch == "{":
                    offset = m.start() - len(head) + (len(head) - len(head.lstrip()))
                report("function", statement, offset)
        elif at_scope and ch == ";":
            body = re.sub(r"^\s*(?:(?:public|private|protected)\s*:\s*)+", "", head)
            statement = body.strip()
            offset = m.start() - len(body) + (len(body) - len(body.lstrip()))
            innermost = stack[-1][0] if stack else "namespace"
            if innermost == "class":
                mm = re.search(r"(?:^|[\s:)])((?:inline\s+)?static\s+.*)$", statement, re.S)
                if mm and is_mutable_static(re.sub(r"^inline\s+", "", mm.group(1))):
                    report("class", mm.group(1), offset)
            elif is_mutable_namespace_variable(statement):
                report("namespace", statement, offset)
        if ch == "{":
            pre = head.rstrip()
            tail = pre.split(";")[-1].strip()
            if in_function:
                kind = "init" if re.search(r"(=|\breturn|[\w>\]])\s*$", pre) and not FUNCTION_HEAD.search(pre) else "function"
                if re.search(r"\b(if|for|while|switch|do|else|try|catch)\b[^;]*$", pre) or pre.endswith(")") or pre == "":
                    kind = "function"
            elif NAMESPACE_HEAD.search(tail):
                kind = "namespace"
            elif ENUM_HEAD.search(tail):
                kind = "enum"
            elif CLASS_HEAD.search(tail):
                kind = "class"
            elif FUNCTION_HEAD.search(pre) or (INIT_LIST_HEAD.search(pre) and pre.endswith(("}", ")"))):
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


def iter_sources(project_root: Path, scope: LintScope) -> list[str]:
    return scope.select(("Source",), EXTENSIONS, ("ThirdParty",))


def load_exceptions(project_root: Path) -> tuple[dict[tuple[str, str], str], list[str]]:
    """예외 표를 (파일, 이름) → 까닭 으로 읽습니다. 까닭이 빈 항목은 문제로 돌려준다(폴백 없음 — 표가 없으면 오류)."""
    data = json.loads((project_root / EXCEPTION_FILE).read_text(encoding="utf-8"))
    table: dict[tuple[str, str], str] = {}
    list_problem: list[str] = []
    for entry in data["_listEntry"]:
        why = entry.get("_why", "").strip()
        if why == "":
            list_problem.append(f"{EXCEPTION_FILE}: {entry['_path']} `{entry['_name']}` — 까닭(_why)이 비었다")
        table[(entry["_path"], entry["_name"])] = why
    return table, list_problem


def check_mutable_statics(project_root: Path, scope: LintScope) -> list[str]:
    table, list_problem = load_exceptions(project_root)
    used: set[tuple[str, str]] = set()
    for rel_path in iter_sources(project_root, scope):
        if "/Tests/" in rel_path or rel_path.startswith("Source/Core/Testing/"):
            continue  # 테스트 고정물은 프로세스 수명 상태를 흉내 낸다 — 엔진 범위가 아니다
        text = read_text(project_root / rel_path, errors="replace")
        if text is None:
            raise FileNotFoundError(project_root / rel_path)   # scope 의 파일은 있다 — 예전 read_text 처럼 멈춘다
        for line, where, name, statement in find_mutable_statics(text):
            if (rel_path, name) in table:
                used.add((rel_path, name))
                continue
            list_problem.append(f"{rel_path}:{line}: {WHERE_TEXT[where]} 가변 변수 `{statement}` — 주인 객체(서비스 · 매니저 · 디바이스 · 레지스트리)의 멤버로, 정적 등록이면 SW_DEFINE_REGISTRATION_LIST · SLOT · CHAIN 으로, 정말 프로세스 바닥이면 {EXCEPTION_FILE} 에 까닭과 함께")
        if rel_path not in MACRO_ALLOWED_FILES:
            for m in re.finditer(r"(?m)^[ \t]*#\s*define\b(?:.*\\\n)*.*", text):
                if re.search(r"\bstatic\b(?!_assert|_cast)\s+(?!constexpr|const\b|inline\b)[\w:<>]+[^;\\]*\bs_\w+", m.group(0)) and "{" in m.group(0):
                    line = text.count("\n", 0, m.start()) + 1
                    if re.search(r"\)\s*\\?\s*\{", m.group(0)):
                        list_problem.append(f"{rel_path}:{line}: 매크로 본문의 함수 정적 가변 변수 — 정적 등록은 공통 틀 매크로(SW_DEFINE_REGISTRATION_LIST · SLOT · CHAIN)를 감싼다")
    for key in sorted(set(table) - used) if scope.whole_repo else ():   # 낡은 예외는 저장소 전체를 봐야 안다
        list_problem.append(f"{EXCEPTION_FILE}: {key[0]} `{key[1]}` — 소스에 없는 예외 항목(지운다)")
    return list_problem


def run_self_check() -> bool:
    def statics(code: str) -> list[str]:
        return [s for _, where, _, s in find_mutable_statics(code) if where == "function"]

    def scoped(code: str) -> list[tuple[str, str]]:
        return [(where, name) for _, where, name, _ in find_mutable_statics(code) if where != "function"]

    assert statics("int f() { static int s_x = 0; return ++s_x; }") == ["static int s_x = 0"]
    assert statics("int f() { static const int s_x = 3; return s_x; }") == []
    assert statics("int f() { static constexpr int kX = 3; return kX; }") == []
    assert statics("int f() { static const char* s_p = \"a\"; return 0; }") == ["static const char* s_p = \" \""]
    assert statics("int f() { static const char* const s_p = nullptr; return 0; }") == []
    assert statics("T& f() { static T& s_r = g(); return s_r; }") == []
    assert statics("void f() { static thread_local int t_x; }") == []
    assert statics("A::A() : _a{ 0 }, _b{} { static uint64 s_next = 1; _a = s_next++; }") == ["static uint64 s_next = 1"]
    assert statics("void f() { if ( x ) { static Registry s_registry{ 1 }; } }") == ["static Registry s_registry"]
    assert statics("void f() { struct L { static void g() { } }; }") == []
    assert statics("void f() { auto l = []() { static int s_y; }; }") == ["static int s_y"]
    assert statics("void f() { static const T* const s_arr[static_cast<uint32>( E::Count )] = { a }; }") == []
    assert statics("void f() { static const vector<T*> s_empty; }") == []
    assert statics("void f() { static_assert( true ); int x = static_cast<int>( 1.0 ); }") == []
    assert statics("#define M() static int s_hidden;\nvoid f() {}") == []
    # 이름공간 범위 · 클래스 정적 데이터 멤버
    assert scoped("namespace n { static int s_file = 0; }") == [("namespace", "s_file")]
    assert scoped("namespace { atomic<uint64> s_next{ 1 }; }") == [("namespace", "s_next")]
    assert scoped("namespace n { int g_count; vector<int> g_list; }") == [("namespace", "g_count"), ("namespace", "g_list")]
    assert scoped("namespace n { const int kA = 1; constexpr int kB = 2; extern int g_x; inline constexpr int kC = 3; const char* const kP = nullptr; }") == []
    assert scoped("namespace n { void f( int a ); int g(); using X = int; template<typename T> T h(); }") == []
    assert scoped("namespace n { class A; enum class E { A, B }; struct S { int _x; }; }") == []
    assert scoped("struct A { static int s_member; static const int kX; static constexpr int kY = 1; static void f() noexcept; static T* g( int x ); int _n; };") == [("class", "s_member")]
    assert scoped("class A { public: inline static atomic<int*> s_p{ nullptr }; };") == [("class", "s_p")]
    assert scoped("int A::s_member = 0;") == []
    assert scoped("namespace n { SW_DECLARE_THING( X ); thread_local int t_x; int& r = g(); }") == []
    assert scoped("namespace n { const char* s_name = nullptr; }") == [("namespace", "s_name")]
    assert scoped("namespace n { T s_table[] = { 1, 2 }; }") == [("namespace", "s_table")]
    print("[mutable-statics] self-check OK")
    return True


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[3] if __doc__ else None)
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args(argv)
    if args.self_check:
        return ExitCode.PASSED if run_self_check() else ExitCode.VIOLATION
    list_problem = check_mutable_statics(get_project_root(), make_whole_scope(get_project_root()))
    for line in list_problem:
        print(f"  - {line}")
    print(f"[mutable-statics] {'FAIL' if list_problem else 'OK'} ({len(list_problem)})")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
