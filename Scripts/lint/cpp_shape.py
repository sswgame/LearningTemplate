#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/cpp_shape.py

C++ 모양 규칙 중 컴파일러가 보지 않는 열을 본다. 규칙 문서만 있고 검사가 없어 어긋남이 퍼졌던 자리다(2026-11-05 — 선언 순서 위반 1240 곳 · bool 깃발 1228 칸).
**왜 형식이 아니라 린트인가.** 여섯 다 C++ 문법으로 막을 수 없다(멤버 순서 · 칸 형식의 선택 · 익명 네임스페이스 수 · 함수가 놓인 타입 · include 자리).

1. 선언 순서(`docs/agents/Includes.md`): public 멤버 변수 → 생성자 · 소멸자 → … → private 함수(따로 된 접근 구역) → private 멤버 변수(마지막).
   - public 멤버 변수가 함수 뒤에 온다 · private/protected 멤버 변수 뒤에 함수가 온다 · private/protected 함수와 변수가 한 구역에 섞였다.
2. 깃발 칸(`docs/agents/CppStyle.md` "bit packing"): `bool _bX` 멤버 대신 `uint8 _bX : 1`. 주소를 쓰는 칸(ImGui `&b` · 출력 인자) · 여러 스레드가 따로 쓰는
   이웃 칸(같은 바이트 경합)은 `bool` 로 두고 그 줄에 `NOLINT(sw-bool-flag): <이유>` 를 적는다.
3. 익명 네임스페이스(`docs/agents/Helpers.md`): `.cpp` 에 하나(전처리 갈래가 갈리는 경우는 갈래마다), 빈 블록 없음.
4. 주인 우선(`docs/agents/Helpers.md`): 헤더의 static 함수만 든 타입(이름은 보지 않는다 — Util · Scanner · Rules …)이 같은 헤더의 타입 하나(열거 · 템플릿 · I 인터페이스 제외)를
   함수 서명 절반 이상에서 받으면, 그 함수는 그 타입의 (static) 멤버다. 주인이 정말 없는 것은 `Config/Rules/Lint/CppShapeExceptions.json` 의 `staticOwner` 에 까닭을 적는다.
5. 중괄호 안 헤더 include: X-매크로 표(`.xxx`) · 인라인 본문(`.inl`) 말고는 `#include` 가 이름공간 · 클래스 · 함수 몸통 안에 있으면 안 된다.
   2026-11-05 — 파일을 합치는 일회용 스크립트가 "마지막 include 뒤" 에 헤더를 붙여 클래스 몸통 · 배열 초기값 안에 들어갔고, 다른 헤더가 먼저 끌어와
   `#pragma once` 로 비어 있어 컴파일러가 알리지 않았다.
6. 끊긴 비트필드 묶음: 같은 접근 구역의 비트필드 사이에 다른 칸이 있으면 묶음마다 바이트를 따로 먹는다(전처리 갈래로 끊긴 것은 보지 않는다).
7. 온라인 키트의 프로토콜 표: 키트 서버는 `OnlineProtocolService`, 키트 클라이언트는 `OnlineProtocolClient` 로 선다. 인터페이스(`IOnlineServerService` · `IOnlineClientService`)를 직접 잇는 것은 C++ 로 막을 수 없다 — 테스트의 가짜가 같은 인터페이스를 이어야 한다.
   예외는 없다. 계정 클라이언트의 재접속 게이트도 기반(`OnlineServiceClient::holdSessionRequests`)으로 옮겼다(2026-11-07).
8. 게임 넷 메시지 표: 넷 키트 · 엔진 세션 · 게임의 메시지는 서술자 표(`NetMessageTable` · `NetTableHandler`, `Core/Network/Message/NetMessageTable.h`)로 선다.
   손 종류 상수(`NetKitMessageRange::kX + n`), 손 마스크(`1u << ( NetXMessage::kY - … )`), 종류 손 디스패치(`context._kind ==`)는 마스크에서 종류를 빠뜨리거나
   메시지를 더하고 와이어 버전을 올리지 않는 결함을 다시 낸다(2026-11-07 — 16 곳). 표가 마스크 · 디스패치 · 와이어 해시를 낸다.

9. 기반 클래스는 하나(사용자 2026-11-08 — 다중 상속은 최소로): 서브시스템이 창구 인터페이스를 함께 상속하지 않고 구현을 멤버로 들어 건다
   (`SubsystemScope::exposeMember` · 모듈 창구 `SW_MODULE_PART` · `getPublishedPart`). `std::enable_shared_from_this` 는 세지 않는다(표준 믹스인).
   예외는 `Config/Rules/Lint/CppShapeExceptions.json` 의 `multipleBase` 에 `경로|클래스` → 까닭으로 적는다(플랫폼 COM · 빈 기반 최적화 · 역할 인터페이스 묶음).
   전체 실행(`--all`)은 더는 없는 예외 자리도 위반으로 센다.
10. 상태와 그것을 바꾸는 논리는 한 타입(`docs/agents/Helpers.md` — 규칙 4 의 `.cpp` 짝): `.cpp` 에 정의한 상태 타입(공개 칸, 이름이 `…Internal` 아님)의 공개 칸을
   같은 파일 `…Internal` 의 static 함수가 `const` 아닌 참조 · 포인터(`outX` 이름 말고 — 한 번 채우는 기록은 제외)로 받아 바꾸는 수가 문턱 이상이면 위반이다.
   문턱 `stateMutatorMinFunctions` 과 예외 `stateMutator`(`경로|타입` → 까닭)는 `Config/Rules/Lint/CppShapeExceptions.json`, 전체 실행은 낡은 예외도 센다.
11. 브라우저 JS 는 C++ 안에 쓰지 않는다(`EM_JS` · `EM_ASYNC_JS` · `EM_ASM…`): 매크로 본문을 C++ 포맷터가 C++ 로 다뤄 `=>` · `!==` 를 띄우고, 브라우저 JS 가 링크 뒤
   문법 오류로 멈췄다(2026-11-09 — IDBFS 마운트 · URL 열기). 기능 옆 `Web/*.js` Emscripten 라이브러리(addToLibrary)에 두고 C++ 는 `extern "C"` 로 선언한다
   (Platform/Web.cmake 가 모두 링크에 넘긴다). 예외는 없다.

`py -3 -m Scripts lint cpp-shape [--files …|--all]` 로 돌리고, 커밋 훅(`rules.py` 표)이 staged 파일에 돌린다.
"""

from __future__ import annotations

import argparse
import functools
import json
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from lint.lint_scope import LintScope, make_file_scope, make_whole_scope, read_text  # noqa: E402
from lint.parallel import flat_map_in_processes  # noqa: E402


SKIP_FOLDERS = ("ThirdParty", "Generated", ".gen.")
BOOL_FLAG_EXEMPT = "NOLINT(sw-bool-flag)"
ONLINE_KIT_FOLDER = "Source/GameFramework/Kits/Feature/Online/"
ONLINE_CORE_FOLDER = "Source/GameFramework/Kits/Feature/Online/OnlineCore/"  # 프로토콜 표의 기반(OnlineProtocolService)이 사는 곳 — 기반은 인터페이스를 잇는다
ONLINE_INTERFACE_PATTERN = re.compile(r"public\s+(IOnlineServerService|IOnlineClientService)\b")
ACCESS_KEYWORDS = ("public", "protected", "private")
EXCEPTIONS_PATH = "Config/Rules/Lint/CppShapeExceptions.json"
UNCOUNTED_BASES = ("enable_shared_from_this",)  # 표준 믹스인 — 상속 계층이 아니다


# ------------------------------------------------------------------------------
# 1) 주석 · 문자열을 지운 글(위치는 그대로)
# ------------------------------------------------------------------------------
def strip_code(text: str) -> str:
    """주석 · 문자열 · 문자 상수 · 전처리 줄을 같은 길이의 공백으로 바꿉니다(줄 번호를 지킨다)."""
    out = list(text)
    index, size = 0, len(text)

    def blank(start: int, end: int) -> None:
        for position in range(start, min(end, size)):
            if out[position] != "\n":
                out[position] = " "

    while index < size:
        char = text[index]
        if text.startswith("//", index):
            end = text.find("\n", index)
            end = size if end < 0 else end
            blank(index, end)
            index = end
        elif text.startswith("/*", index):
            end = text.find("*/", index + 2)
            end = size if end < 0 else end + 2
            blank(index, end)
            index = end
        elif char == "R" and text.startswith('R"', index) and (index == 0 or not (text[index - 1].isalnum() or text[index - 1] == "_")):
            match = re.match(r'R"([^(]*)\(', text[index:])
            if match is None:
                index += 1
                continue
            end = text.find(")" + match.group(1) + '"', index)
            end = size if end < 0 else end + len(match.group(1)) + 2
            blank(index + 1, end)
            index = end
        elif char == '"' or (char == "'" and not (index > 0 and (text[index - 1].isalnum() or text[index - 1] == "_"))):
            end = index + 1
            while end < size and text[end] != char and text[end] != "\n":
                end += 2 if text[end] == "\\" else 1
            blank(index + 1, end)
            index = end + 1
        else:
            index += 1
    stripped = "".join(out)
    # 전처리 줄(이어진 줄 포함)도 지운다 — `#define` 몸통의 중괄호가 범위를 흐트러뜨리지 않게
    return re.sub(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*$", lambda m: re.sub(r"[^\n]", " ", m.group(0)), stripped, flags=re.M)


def match_brace(text: str, open_index: int) -> int:
    depth = 0
    for index in range(open_index, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return len(text) - 1


BODY_OPEN = re.compile(r"\s*(?:const\s*|noexcept\s*|override\s*|final\s*|->\s*[\w:<>]+\s*)*\{")  # 인자 목록 `)` 뒤의 함수 본문 시작


def find_param_list_end(code: str, index: int) -> int:
    """인자 목록 안의 @p index 에서 그 목록을 닫는 `)` 위치입니다(없으면 글 끝)."""
    depth = 0
    for position in range(index, len(code)):
        if code[position] == "(":
            depth += 1
        elif code[position] == ")":
            if depth == 0:
                return position
            depth -= 1
    return len(code)


def previous_token(text: str, index: int) -> str:
    position = index - 1
    while position >= 0 and text[position].isspace():
        position -= 1
    if position < 0:
        return ""
    if text[position].isalnum() or text[position] == "_":
        start = position
        while start >= 0 and (text[start].isalnum() or text[start] == "_"):
            start -= 1
        return text[start + 1:position + 1]
    return text[position]


# ------------------------------------------------------------------------------
# 2) 클래스 몸통의 문장 분류
# ------------------------------------------------------------------------------
TYPE_HEAD = re.compile(r"(?:^|[\s;{}])(?:template\s*<.*>\s*)?(class|struct|union|enum(?:\s+class|\s+struct)?)\b\s*(?:alignas\s*\([^)]*\)\s*)?(?:(?:SW_\w+|\[\[[^\]]*\]\])\s*)*(?:\w+::)*(\w*)", re.S)
BODY_PREV_KEYWORDS = ("const", "override", "final", "noexcept", "try", "mutable")


def remove_angle_groups(text: str) -> str:
    """`<…>` 를 지웁니다(템플릿 인자의 괄호가 함수로 읽히지 않게). `operator<` 같은 것은 대상이 아니다(함수로 이미 갈린다)."""
    previous = None
    while previous != text:
        previous = text
        text = re.sub(r"<[^<>]*>", "", text)
    return text


def strip_template_head(text: str) -> str:
    """앞의 `template < … >` 를 짝 맞춰 벗깁니다(기본 인자 `typename = enable_if_t<…>` 의 `=` · 괄호가 문장 본체로 읽히지 않게)."""
    match = re.match(r"template\s*<", text)
    if match is None:
        return text
    depth = 0
    paren = 0  # 괄호 안(`decltype( a << b )`)의 꺾쇠는 세지 않는다
    for index in range(match.end() - 1, len(text)):
        char = text[index]
        if char == "(":
            paren += 1
        elif char == ")":
            paren -= 1
        elif paren == 0 and char == "<":
            depth += 1
        elif paren == 0 and char == ">":
            depth -= 1
            if depth == 0:
                return text[index + 1:].lstrip()
    return text


def classify_statement(statement: str, class_name: str) -> str | None:
    """클래스 깊이 문장의 종류 — ctor · func · var · None(타입 별칭 · friend · static · 매크로 등 순서 밖)."""
    text = re.sub(r"\s+", " ", statement).strip()
    # 앞에 붙는 매크로 · 속성(PROPERTY( … ) · SW_API · [[nodiscard]])을 벗긴다
    while True:
        match = re.match(r"(?:\[\[[^\]]*\]\]|[A-Z][A-Z0-9_]+\b(?:\s*\((?:[^()]|\([^()]*\))*\))?)\s*", text)
        if match is None or match.end() == 0:
            break
        rest = text[match.end():]
        if re.match(r"[A-Z][A-Z0-9_]+\b\s*\(", text) and not rest:
            return None  # 매크로 한 줄(REFLECT_BODY() · SW_DECLARE_… )
        text = rest
    if not text:
        return None
    body = strip_template_head(text)
    if re.match(r"friend\b", body) and not re.match(r"friend\s+(class|struct|union)\b", body) and "(" in remove_angle_groups(body):
        return "func"  # friend 함수(연산자 · 스트림 출력) — 공개 함수 무리
    if re.match(r"(using|typedef|friend|static_assert|namespace|template\s*<[^>]*>\s*(class|struct|using))\b", text):
        return None
    if re.match(r"(class|struct|union|enum(\s+class|\s+struct)?)\s+\w+(\s*:\s*[\w:]+)?$", text):
        return None  # 전방 선언(`struct Entry;`) — 순서 밖(뒤의 선언이 쓴다)
    text = strip_template_head(text)
    text = re.sub(r"^alignas\s*\((?:[^()]|\([^()]*\))*\)\s*", "", text)  # `alignas( … )` 의 괄호는 함수 인자가 아니다
    if re.match(r"(static|inline\s+static|constexpr\s+static)\b", text) or re.match(r"(constexpr|inline)\s+static\b", text):
        return None  # 정적 멤버는 순서 규칙 밖(상수 · 정적 함수)
    if re.match(r"(explicit\s+|constexpr\s+|inline\s+|virtual\s+)*~?" + re.escape(class_name) + r"\s*\(", text):
        return "ctor"
    # 변수 이름 뒤에 붙는 매크로(SW_GUARDED_BY( … ))를 지운다
    text = re.sub(r"\s+SW_[A-Z_]+\s*\((?:[^()]|\([^()]*\))*\)", "", text)
    flat = remove_angle_groups(text)
    flat = re.sub(r"\[[^\[\]]*\]", "[]", flat)  # 배열 크기 안의 괄호(`_arr[static_cast<int32>( … )]`)는 함수 인자가 아니다
    first_paren = flat.find("(")
    if first_paren >= 0 and re.match(r"\(\s*(?:[\w:]+::)?[*&]\s*\w+\s*\)\s*[\(\[]", flat[first_paren:]):
        return "var"  # 함수 포인터 · 멤버 함수 포인터 · 배열 참조 멤버 — 문장의 첫 괄호가 `( *name )` · `( Type::*name )` 이다
    if "(" in flat:
        head = flat.split("(")[0]
        if "operator" in head or ("=" not in head and "{" not in head):
            return "func"
    return "var"


class ClassInfo:
    def __init__(self, name: str, is_struct: bool, line: int, bases: list[str] | None = None) -> None:
        self.name = name
        self.is_struct = is_struct
        self.line = line
        self.bases = bases or []  # 기반 클래스 이름(꺾쇠 인자를 지운 것)
        self.items: list[tuple[str, str, int, str]] = []  # (종류, 접근, 줄, 문장)


def parse_bases(after_name: str) -> list[str]:
    """클래스 이름 뒤 글(`final : public A, private B`)에서 기반 이름 목록을 꺼냅니다. 꺾쇠 인자는 이미 지워져 있다."""
    match = re.search(r"(?<!:):(?!:)(.*)$", after_name, re.S)
    if match is None:
        return []
    list_base = []
    for part in match.group(1).split(","):
        name = re.sub(r"\b(public|protected|private|virtual)\b", "", part).strip()
        if name:
            list_base.append(name)
    return list_base


def get_line_of(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


def parse_class(code: str, start: int, end: int, info: ClassInfo, out_classes: list[ClassInfo]) -> None:
    access = "public" if info.is_struct else "private"
    index = start
    statement_start = start
    while index < end:
        char = code[index]
        if char == "{":
            head = code[statement_start:index]
            token = previous_token(code, index)
            type_head = remove_angle_groups(head)  # 템플릿 인자의 `class T` 는 타입 정의가 아니다
            match = TYPE_HEAD.search(type_head)
            if match and "(" not in type_head[:match.start()] and match.group(1).startswith(("class", "struct", "union")) and "=" not in type_head[match.end():]:
                close = match_brace(code, index)
                nested = ClassInfo(match.group(2), match.group(1) == "struct", get_line_of(code, index), parse_bases(type_head[match.end():]))
                parse_class(code, index + 1, close, nested, out_classes)
                out_classes.append(nested)
                index = close + 1
                while index < end and code[index] in " \t\r\n":
                    index += 1
                if index < end and code[index] == ";":
                    index += 1
                else:
                    # `struct { … } _member;` — 칸이다
                    semi = code.find(";", index)
                    info.items.append(("var", access, get_line_of(code, index), code[index:semi]))
                    index = semi + 1
                statement_start = index
                continue
            if match and match.group(1).startswith("enum"):
                close = match_brace(code, index)
                index = code.find(";", close) + 1
                statement_start = index
                continue
            if (token and (token[0].isalnum() or token[0] == "_") and token not in BODY_PREV_KEYWORDS) or token in (">", "]", "=", ","):
                index = match_brace(code, index) + 1  # 중괄호 초기화 — 문장을 잇는다
                continue
            close = match_brace(code, index)  # 함수 본문
            kind = classify_statement(head, info.name)
            if kind:
                info.items.append((kind, access, get_line_of(code, index), head))
            index = close + 1
            probe = index
            while probe < end and code[probe].isspace():
                probe += 1
            if probe < end and code[probe] == ";":
                index = probe + 1
            statement_start = index
            continue
        if char == ";":
            kind = classify_statement(code[statement_start:index], info.name)
            if kind:
                info.items.append((kind, access, get_line_of(code, index), code[statement_start:index]))
            statement_start = index + 1
        elif char == ":" and code[index + 1:index + 2] != ":" and code[index - 1:index] != ":":
            match = re.search(r"\b(public|private|protected)\s*$", code[statement_start:index])
            if match:
                access = match.group(1)
                info.items.append(("access", access, get_line_of(code, index), ""))
                statement_start = index + 1
        index += 1


def parse_scope(code: str, start: int, end: int, out_classes: list[ClassInfo]) -> None:
    index = start
    statement_start = start
    while index < end:
        char = code[index]
        if char == "{":
            head = code[statement_start:index]
            type_head = remove_angle_groups(head)  # 템플릿 인자의 `class T` 는 타입 정의가 아니다
            match = TYPE_HEAD.search(type_head)
            close = match_brace(code, index)
            if match and match.group(1).startswith(("class", "struct", "union")) and "(" not in type_head[:match.start()] and "=" not in type_head[match.end():] and match.group(2):
                info = ClassInfo(match.group(2), match.group(1) == "struct", get_line_of(code, index), parse_bases(type_head[match.end():]))
                parse_class(code, index + 1, close, info, out_classes)
                out_classes.append(info)
            elif re.search(r"\bnamespace\b[^()]*$|\bextern\s*$", head.strip() and head or ""):
                parse_scope(code, index + 1, close, out_classes)
            index = close + 1
            statement_start = index
            continue
        if char == ";":
            statement_start = index + 1
        index += 1


def collect_classes(code: str) -> list[ClassInfo]:
    classes: list[ClassInfo] = []
    parse_scope(code, 0, len(code), classes)
    return classes


# ------------------------------------------------------------------------------
# 3) 검사
# ------------------------------------------------------------------------------
def check_declaration_order(info: ClassInfo) -> list[tuple[int, str]]:
    problems: list[tuple[int, str]] = []
    first_function = None
    for kind, access, line, _ in info.items:
        if kind in ("ctor", "func") and first_function is None:
            first_function = line
        if kind == "var" and access == "public" and first_function is not None:
            problems.append((line, f"`{info.name}` 의 public 멤버 변수가 함수 뒤에 있다(public 변수는 맨 앞 — {first_function} 줄의 함수보다 먼저)"))
    first_hidden_variable = None
    for kind, access, line, _ in info.items:
        if kind == "var" and access != "public" and first_hidden_variable is None:
            first_hidden_variable = line
        elif kind in ("ctor", "func") and first_hidden_variable is not None:
            problems.append((line, f"`{info.name}` 의 함수가 private/protected 멤버 변수({first_hidden_variable} 줄) 뒤에 있다(멤버 변수는 맨 끝)"))
            break
    section = None
    kinds: set[str] = set()
    section_line = info.line
    for kind, access, line, _ in info.items + [("access", "", 0, "")]:
        if kind == "access":
            if section in ("private", "protected") and {"func", "var"} <= kinds:
                problems.append((section_line, f"`{info.name}` 의 {section} 구역에 함수와 멤버 변수가 섞였다(함수 구역 · 변수 구역을 따로 연다)"))
            section, kinds, section_line = access, set(), line
            continue
        if section is None:
            section = access
        kinds.add("func" if kind == "ctor" else kind)
    return problems


BOOL_MEMBER = re.compile(r"^\s*(?:mutable\s+)?(?:std::)?bool\s+(_b\w+)\s*(?:\{[^{};]*\}|=\s*[^;]+)?\s*;")


def check_bool_flags(info: ClassInfo, raw_lines: list[str]) -> list[tuple[int, str]]:
    problems: list[tuple[int, str]] = []
    for kind, _, line, statement in info.items:
        if kind != "var":
            continue
        text = re.sub(r"\s+", " ", statement).strip()
        text = re.sub(r"^(?:(?:\[\[[^\]]*\]\]|[A-Z][A-Z0-9_]+\b(?:\s*\((?:[^()]|\([^()]*\))*\))?)\s*)*", "", text)
        match = BOOL_MEMBER.match(" " + text + ";")
        if match is None:
            continue
        source_line = raw_lines[line - 1] if 0 < line <= len(raw_lines) else ""
        if BOOL_FLAG_EXEMPT in source_line:
            continue
        problems.append((line, f"`{info.name}::{match.group(1)}` 는 깃발 칸이다 — `uint8 {match.group(1)} : 1`(값은 SW_TRUE / SW_FALSE, 생성자에서). 주소를 써야 하면 그 줄에 `{BOOL_FLAG_EXEMPT}: <이유>`"))
    return problems


def check_bit_field_runs(info: ClassInfo, raw_lines: list[str]) -> list[tuple[int, str]]:
    """같은 접근 구역의 비트필드가 다른 칸으로 끊기면 묶음마다 바이트(와 다음 칸의 정렬)를 따로 먹는다. 사이에 전처리 갈래가 있으면 구성마다 칸이 달라 보지 않는다."""
    problems: list[tuple[int, str]] = []
    last_bit_line: dict[str, int] = {}
    broken_after: dict[str, int] = {}
    for kind, access, line, statement in info.items:
        if kind != "var" or re.search(r"\bstatic\b|\bconstexpr\b", statement):
            continue
        declaration = re.sub(r"\{.*|=.*", "", re.sub(r"\s+", " ", statement)).strip()
        is_bit = re.search(r":\s*\d+\s*$", declaration) is not None
        if is_bit:
            previous = last_bit_line.get(access)
            if previous is not None and access in broken_after:
                between = raw_lines[previous:line - 1]
                if not any(text.lstrip().startswith("#") for text in between):
                    problems.append((line, f"`{info.name}` 의 비트필드 묶음이 {broken_after[access]} 줄의 칸으로 끊겼다 — 같은 접근 구역의 비트필드는 붙여 둔다(생성자 초기화 목록도 같은 순서로)"))
            last_bit_line[access] = line
            broken_after.pop(access, None)
        elif access in last_bit_line:
            broken_after.setdefault(access, line)
    return problems


def check_anonymous_namespaces(code: str) -> list[tuple[int, str]]:
    problems: list[tuple[int, str]] = []
    blocks = list(re.finditer(r"\bnamespace\s*\{", code))
    for match in blocks:
        open_index = code.index("{", match.start())
        close = match_brace(code, open_index)
        if code[open_index + 1:close].strip() == "":
            problems.append((get_line_of(code, match.start()), "빈 익명 네임스페이스 — 지운다"))
    return problems


TYPE_OPEN = re.compile(r"(?<![\w<,])(class|struct)\s+(?:SW_\w+\s+)?(\w+)\s*(?:final\s*)?(?::(?!:)[^{;]*)?\{")


def flatten_body(body: str) -> str:
    """중첩 중괄호 안(함수 본문 · 중첩 타입)을 지운 몸통 — 깊이 0 문장만 남는다."""
    out = []
    depth = 0
    for char in body:
        if char == "{":
            depth += 1
            if depth == 1:
                out.append("{")
            continue
        if char == "}":
            depth -= 1
            if depth == 0:
                out.append("};")
            continue
        if depth == 0:
            out.append(char)
    return "".join(out)


def collect_static_only_types(code: str) -> tuple[dict[str, tuple[int, list[str]]], set[str]]:
    """헤더의 타입 중 static 함수만 든 것(칸 · 인스턴스 함수 없음) → (줄, static 서명 목록)과, 주인이 될 수 있는 같은 헤더의 타입 이름(열거 · 템플릿 · I 인터페이스 제외)."""
    enums = set(re.findall(r"\benum\s+(?:class|struct)?\s*(?:SW_\w+\s+)?(\w+)", code))
    owners: set[str] = set()
    static_only: dict[str, tuple[int, list[str]]] = {}
    for match in TYPE_OPEN.finditer(code):
        name = match.group(2)
        is_template = re.search(r"template\s*<[^;{}]*>\s*$", code[max(0, match.start() - 200):match.start()]) is not None
        if not is_template and name not in enums and re.match(r"I[A-Z]", name) is None:
            owners.add(name)
        open_index = match.end() - 1
        statements = [re.sub(r"\s+", " ", text).strip() for text in flatten_body(code[open_index + 1:match_brace(code, open_index)]).split(";")]
        statics: list[str] = []
        is_static_only = True
        for statement in statements:
            text = re.sub(r"^(?:(?:public|private|protected)\s*:\s*)+", "", statement)
            text = re.sub(r"^(?:\[\[[^\]]*\]\]\s*|[A-Z][A-Z0-9_]+(?:\s*\([^()]*\))?\s+)*", "", text)
            if not text or text.startswith(("{", "}", "using ", "typedef ", "friend ", "static_assert", "enum ", "template<", "template <")) and "(" not in text:
                continue
            if re.match(r"(?:template\s*<[^>]*>\s*)?(?:\[\[[^\]]*\]\]\s*)?(?:inline\s+|constexpr\s+)*static\b", text):
                if "(" in text and re.search(r"=\s*[^=]", text.split("(")[0]) is None:
                    statics.append(text)
                continue
            if text.startswith(("constexpr", "inline constexpr", "struct ", "class ", "union ")):
                continue
            if re.match(r"(?:explicit\s+|constexpr\s+)*~?" + re.escape(name) + r"\s*\(", text) or "= delete" in text or re.match(r"[A-Z][A-Z0-9_]+\s*\(", text):
                continue
            is_static_only = False
            break
        if is_static_only and statics:
            static_only[name] = (get_line_of(code, match.start()), statics)
    return static_only, owners


def check_static_owner(code: str, relative: str, exceptions: dict[str, str]) -> list[tuple[int, str]]:
    """
    주인 우선(`docs/agents/Helpers.md`): 헤더의 static 함수만 든 타입이 같은 헤더의 타입 하나를 함수 서명 절반 이상에서 받으면 그 함수들은 그 타입의 (static) 멤버다.
    이름(`XUtil` · `XScanner` · `XRules` …)은 보지 않는다 — 2026-11-08 `MarkupTagScanner`(데이터 `MarkupToken` 옆)처럼 Util 이 아닌 이름으로 같은 모양이 남았다.
    형식으로는 막을 수 없다(어느 함수가 어느 타입의 일인지는 C++ 이 모른다). 여러 타입에 걸친 알고리즘 · 등록 목록처럼 주인이 정말 없는 것은
    `Config/Rules/Lint/CppShapeExceptions.json` 의 staticOwner 에 까닭과 함께 적는다.
    """
    if not relative.endswith(".h"):
        return []
    static_only, owners = collect_static_only_types(code)
    problems = []
    for name, (line, statics) in static_only.items():
        if f"{relative}|{name}" in exceptions:
            continue
        counts: dict[str, int] = {}
        for signature in statics:
            for owner in sorted(set(re.findall(r"\b([A-Z]\w*)\b", signature)) & owners - {name}):
                counts[owner] = counts.get(owner, 0) + 1
        if not counts:
            continue
        owner, count = max(sorted(counts.items()), key=lambda item: item[1])   # 같은 수면 이름 순 첫 것 — 집합 순서(해시)로 메시지가 실행마다 바뀌었다
        if count * 2 >= len(statics):
            problems.append((line, f"`{name}` 는 static 함수만 들고 그 함수 대부분({count}/{len(statics)})이 같은 헤더의 `{owner}` 를 다룬다 — `{owner}` 의 (static) 멤버로 합친다"
                                   f"(Helpers.md 주인 우선). 주인이 정말 없으면 {EXCEPTIONS_PATH} 의 staticOwner 에 까닭과 함께 적는다"))
    return problems


@functools.lru_cache(maxsize=1)
def load_static_owner_exceptions(project_root: str) -> dict[str, str]:
    """`경로|타입` → 까닭(주인 우선 예외). 파일이나 칸이 없으면 오류로 멈춘다."""
    return json.loads((Path(project_root) / EXCEPTIONS_PATH).read_text(encoding="utf-8"))["staticOwner"]


INCLUDE_LINE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*[<"]([^>"]+)[>"]', re.M)


def check_includes_inside_braces(raw: str, code: str) -> list[tuple[int, str]]:
    problems: list[tuple[int, str]] = []
    depth = 0
    position = 0
    for match in INCLUDE_LINE.finditer(raw):
        depth += code.count("{", position, match.start()) - code.count("}", position, match.start())
        position = match.start()
        if depth > 0 and not match.group(1).endswith((".xxx", ".inl")):
            problems.append((get_line_of(raw, match.start()), f"`{match.group(1)}` include 가 중괄호(이름공간 · 클래스 · 함수) 안에 있다 — 파일 맨 위 include 영역으로 옮긴다"))
    return problems


def check_shared_temp_in_tests(code: str, relative: str) -> list[tuple[int, str]]:
    """테스트의 임시 파일은 케이스별 폴더에 둔다 — 공용 임시 폴더의 고정 이름은 백엔드별 자식 프로세스끼리 부딪힌다."""
    if "/Tests/" not in relative.replace("\\", "/"):
        return []
    index = code.find("getTempDirectory")
    if index < 0:
        return []
    return [(get_line_of(code, index), "테스트는 공용 임시 폴더 대신 test::makeScratchPath 를 쓴다")]


MANUAL_DEPLETION = re.compile(r"\b(_\w*(?:hp|health|durability|sanity)\w*)\s*(?:-=|=\s*MathUtil::max\(\s*0(?:\.0f)?\s*,\s*[\w.\->]*\1\s*-)", re.IGNORECASE)


INLINE_BROWSER_JS = re.compile(r"\b(EM_JS|EM_ASYNC_JS|EM_ASM\w*|MAIN_THREAD_(?:ASYNC_)?EM_ASM\w*)\s*\(")


def check_inline_browser_js(code: str) -> list[tuple[int, str]]:
    """브라우저 JS 를 C++ 매크로 본문에 쓰지 않는다 — 포맷터가 JS 연산자를 띄운다. `Web/*.js` 라이브러리로 옮긴다(규칙 11)."""
    return [(get_line_of(code, match.start()), f"`{match.group(1)}` — 브라우저 JS 는 기능 옆 Web/*.js(addToLibrary)에 두고 C++ 는 extern \"C\" 로 선언한다") for match in INLINE_BROWSER_JS.finditer(code)]


def check_manual_depletion(code: str, relative: str) -> list[tuple[int, str]]:
    """게임 수치(체력 · 내구도 · 정신력)는 `DepletingValue::take` 로 깎는다 — 0 고정과 소진 판정을 손으로 적으면 곳마다 갈린다(음수 체력이 저장 검사에 걸린다)."""
    is_game_code = relative.startswith(("Source/GameFramework/", "Source/Games/")) and "/Tests/" not in relative
    if not is_game_code:
        return []
    return [(get_line_of(code, match.start()), f"`{match.group(1)}` 를 손으로 깎는다 — DepletingValue::take 를 쓴다") for match in MANUAL_DEPLETION.finditer(code)]


def check_online_protocol_table(raw: str, code: str, relative: str) -> list[tuple[int, str]]:
    """온라인 키트의 서버 · 클라이언트는 프로토콜 표로 선다 — 손 switch 와 메서드마다 다른 지표 · 보존이 되돌아오지 않게 한다."""
    normalized = relative.replace("\\", "/")
    if normalized.startswith(ONLINE_KIT_FOLDER) is False or normalized.startswith(ONLINE_CORE_FOLDER) or "/Tests/" in normalized:
        return []
    del raw
    list_problem: list[tuple[int, str]] = []
    for match in ONLINE_INTERFACE_PATTERN.finditer(code):
        list_problem.append((get_line_of(code, match.start()), f"온라인 키트는 {match.group(1)} 를 직접 잇지 않고 OnlineProtocolService · OnlineProtocolClient 로 선다(예외 없음)"))
    return list_problem


NET_MESSAGE_FOLDERS = ("Source/GameFramework/Kits/Feature/Network/", "Source/Engine/Net/", "Source/Games/")
MANUAL_NET_MESSAGE_PATTERNS = (
    (re.compile(r"NetKitMessageRange::k\w+\s*\+\s*\d"), "손으로 적은 메시지 종류 — 표의 서술자(`NetMessageDef`)와 `Table::getKindOf` 를 쓴다"),
    (re.compile(r"1u\s*<<\s*\(\s*\w*Message::k"), "손으로 쓴 종류 마스크 — `NetTableHandler` 나 `Table::makeMask` 를 쓴다"),
    (re.compile(r"context\._kind\s*=="), "종류 손 디스패치 — `NetTableHandler` 의 `receive` 오버로드(표가 나눈다)를 쓴다"),
)


def check_net_message_table(code: str, relative: str) -> list[tuple[int, str]]:
    """게임 넷 메시지는 서술자 표로 선다 — 손 종류 상수 · 손 마스크 · 손 디스패치가 되돌아오지 않게 한다."""
    normalized = relative.replace("\\", "/")
    if normalized.startswith(NET_MESSAGE_FOLDERS) is False or "/Tests/" in normalized:
        return []
    list_problem: list[tuple[int, str]] = []
    for pattern, message in MANUAL_NET_MESSAGE_PATTERNS:
        for match in pattern.finditer(code):
            list_problem.append((get_line_of(code, match.start()), message))
    return list_problem


@functools.lru_cache(maxsize=1)
def load_multiple_base_exceptions(project_root: str) -> dict[str, str]:
    """`경로|클래스` → 까닭. 파일이 없으면 오류로 멈춘다(예외 목록은 필수 데이터다)."""
    return json.loads((Path(project_root) / EXCEPTIONS_PATH).read_text(encoding="utf-8"))["multipleBase"]


def check_multiple_bases(info: ClassInfo, relative: str, exceptions: dict[str, str]) -> list[tuple[int, str]]:
    """기반이 둘 이상인 클래스를 찾는다 — 서브시스템 · 구현은 창구를 멤버로 들어 건다(`exposeMember` · `SW_MODULE_PART`)."""
    counted = [base for base in info.bases if not base.endswith(UNCOUNTED_BASES)]
    if len(counted) < 2 or f"{relative}|{info.name}" in exceptions:
        return []
    return [(info.line, f"`{info.name}` 의 기반이 {len(counted)} 개다({', '.join(counted)}) — 기반은 하나다. 창구 인터페이스는 구현을 멤버로 들어 건다"
                        f"(`SubsystemScope::exposeMember` · `SW_MODULE_PART`). 불가피하면 {EXCEPTIONS_PATH} 의 multipleBase 에 까닭과 함께 적는다")]


FIELD_MUTATION = (r"\s*(?:\.|->)\s*(?:{fields})\b\s*(?:\[[^\]]*\]\s*)*(?:[-+*/%|&^]?=(?!=)|\+\+|--|"
                  r"\.\s*(?:push_back|emplace_back|pop_back|clear|erase|insert|emplace|resize|reserve|append|assign|reset|swap)\s*\()")


def public_field_name(statement: str) -> str | None:
    """칸 문장(`vector<int32> _listX{};` · `uint8 _bY : 1;` · `float32 _arr[4];`)의 이름입니다."""
    text = re.split(r"[={]", re.sub(r"\s+", " ", statement).strip(), maxsplit=1)[0]
    text = re.sub(r"\[[^\]]*\]", "", re.sub(r":\s*\d+\s*$", "", text)).strip()
    match = re.search(r"(\w+)$", text)
    return match.group(1) if match else None


def find_state_mutators(code: str) -> dict[str, tuple[int, str, int]]:
    """
    `.cpp` 의 상태 타입(공개 칸이 있고 이름이 `…Internal` 이 아닌 것) → (줄, 그 공개 칸을 바꾸는 `…Internal` 타입, 바꾸는 static 함수 수).
    `const` 로 받거나 `outX` 로 받는 함수(한 번 채워 돌려주는 기록 — 파서 · 팩토리)는 세지 않는다.
    """
    states: dict[str, tuple[int, set[str]]] = {}
    for info in collect_classes(code):
        fields = {public_field_name(statement) for kind, access, _, statement in info.items if kind == "var" and access == "public"} - {None}
        if info.name and not info.name.endswith("Internal") and fields:
            states[info.name] = (info.line, fields)
    if not states:
        return {}
    state_pattern = re.compile(r"(?<![\w:])(" + "|".join(map(re.escape, states)) + r")\s*[&*]\s*(\w+)\s*(?=[,)=])")
    functions_by_pair: dict[tuple[str, str], set[int]] = {}
    for match in TYPE_OPEN.finditer(code):
        helper = match.group(2)
        if not helper.endswith("Internal"):
            continue
        open_index = match.end() - 1
        body = code[open_index:match_brace(code, open_index)]
        for param in state_pattern.finditer(body):
            state, name = param.group(1), param.group(2)
            if re.search(r"\bconst\s*$", body[:param.start()]) or re.match(r"out[A-Z]", name):
                continue
            opening = BODY_OPEN.match(body, find_param_list_end(body, param.end()) + 1)
            if opening is None:
                continue
            function_body = body[opening.end() - 1:match_brace(body, opening.end() - 1)]
            if re.search(r"(?<![\w.>])" + re.escape(name) + FIELD_MUTATION.format(fields="|".join(map(re.escape, states[state][1]))), function_body):
                functions_by_pair.setdefault((state, helper), set()).add(open_index + opening.end())
    result: dict[str, tuple[int, str, int]] = {}
    for (state, helper), functions in functions_by_pair.items():
        if state not in result or len(functions) > result[state][2]:
            result[state] = (states[state][0], helper, len(functions))
    return result


def check_state_mutators(code: str, relative: str, rules: dict) -> list[tuple[int, str]]:
    """
    상태와 그것을 바꾸는 논리는 한 타입(`docs/agents/Helpers.md` 주인 우선): `.cpp` 의 상태 구조체 공개 칸을 따로 선 `…Internal` 의 static 함수 여럿이
    차례로 바꾸면, 상태를 비공개 칸 + 그 함수들을 멤버로 든 클래스로 합친다(`TerrainBrushToolState`). 규칙 4 는 헤더만 보아 이 모양이 `.cpp` 에 남았다(2026-11-09).
    형식으로는 막을 수 없다(어느 함수가 어느 상태의 일인지 C++ 이 모른다). 문턱(`stateMutatorMinFunctions`)과 예외(`stateMutator`, `경로|타입` → 까닭)는 데이터다.
    """
    if not relative.endswith(".cpp"):
        return []
    problems = []
    for state, (line, helper, count) in find_state_mutators(code).items():
        if count >= rules["stateMutatorMinFunctions"] and f"{relative}|{state}" not in rules["stateMutator"]:
            problems.append((line, f"`{state}` 의 공개 칸을 `{helper}` 의 static 함수 {count} 개가 바꾼다 — 상태와 그것을 바꾸는 논리는 한 타입이다: 칸을 비공개로, 그 함수들을 `{state}` 의 멤버로"
                                   f"(Helpers.md). 불가피하면 {EXCEPTIONS_PATH} 의 stateMutator 에 까닭과 함께 적는다"))
    return problems


@functools.lru_cache(maxsize=1)
def load_state_mutator_rules(project_root: str) -> dict:
    """규칙 10 의 문턱 · 예외입니다. 칸이 없으면 오류로 멈춘다."""
    data = json.loads((Path(project_root) / EXCEPTIONS_PATH).read_text(encoding="utf-8"))
    return {"stateMutatorMinFunctions": data["stateMutatorMinFunctions"], "stateMutator": data["stateMutator"]}


def find_stale_state_mutator_exceptions(project_root: Path) -> list[str]:
    """더는 걸리지 않는(또는 사라진) 규칙 10 예외 자리 — 전체 실행에서만 센다."""
    rules = load_state_mutator_rules(str(project_root))
    list_problem = []
    for key in rules["stateMutator"]:
        relative, state = key.split("|", 1)
        path = project_root / relative
        hit = find_state_mutators(strip_code(path.read_text(encoding="utf-8-sig", errors="ignore"))).get(state) if path.exists() else None
        if hit is None or hit[2] < rules["stateMutatorMinFunctions"]:
            list_problem.append(f"{EXCEPTIONS_PATH}: 예외 `{key}` 는 더는 걸리지 않는다 — 줄을 지운다")
    return list_problem


def find_stale_multiple_base_exceptions(project_root: Path) -> list[str]:
    """더는 다중 기반이 아닌(또는 사라진) 예외 자리 — 전체 실행에서만 센다."""
    list_problem = []
    for key in load_multiple_base_exceptions(str(project_root)):
        relative, class_name = key.split("|", 1)
        path = project_root / relative
        found = False
        if path.exists():
            code = strip_code(path.read_text(encoding="utf-8-sig", errors="ignore"))
            found = any(info.name == class_name and len([b for b in info.bases if not b.endswith(UNCOUNTED_BASES)]) >= 2 for info in collect_classes(code))
        if not found:
            list_problem.append(f"{EXCEPTIONS_PATH}: 예외 `{key}` 는 더는 다중 기반이 아니다 — 줄을 지운다")
    return list_problem


def inspect_file(path: Path, relative: str) -> list[str]:
    raw = read_text(path, errors="ignore", encoding="utf-8-sig")
    if raw is None:
        raise FileNotFoundError(path)   # 부른 쪽이 있는 파일만 넘긴다 — 예전 read_text 처럼 멈춘다
    code = strip_code(raw)
    raw_lines = raw.splitlines()
    list_problem: list[tuple[int, str]] = []
    exceptions = load_multiple_base_exceptions(str(Path(__file__).resolve().parents[2]))
    for info in collect_classes(code):
        list_problem += check_multiple_bases(info, relative, exceptions)
        list_problem += check_declaration_order(info)
        list_problem += check_bool_flags(info, raw_lines)
        list_problem += check_bit_field_runs(info, raw_lines)
    if relative.endswith(".cpp"):
        list_problem += check_anonymous_namespaces(code)
        list_problem += check_state_mutators(code, relative, load_state_mutator_rules(str(Path(__file__).resolve().parents[2])))
    list_problem += check_static_owner(code, relative, load_static_owner_exceptions(str(Path(__file__).resolve().parents[2])))
    list_problem += check_includes_inside_braces(raw, code)
    list_problem += check_shared_temp_in_tests(code, relative)
    list_problem += check_manual_depletion(code, relative)
    list_problem += check_inline_browser_js(code)
    list_problem += check_online_protocol_table(raw, code, relative)
    list_problem += check_net_message_table(code, relative)
    return [f"{relative}:{line}: {message}" for line, message in sorted(set(list_problem))]


def is_target(relative: str) -> bool:
    return relative.startswith("Source/") and relative.endswith((".h", ".cpp")) and not any(skip in relative for skip in SKIP_FOLDERS)


def inspect_listed_file(project_root: Path, file: str) -> list[str]:
    """파일 하나를 봅니다. 대상이 아니면 빈 목록입니다(`check_cpp_shape` 가 프로세스로 넘기므로 모듈 최상위에 둔다)."""
    path = Path(file).resolve()
    try:
        relative = path.relative_to(project_root).as_posix()
    except ValueError:
        return []
    return inspect_file(path, relative) if is_target(relative) and path.exists() else []


def check_cpp_shape(project_root: Path, scope: LintScope) -> list[str]:
    """scope 가운데 Source 의 C++ 파일을 보고 어긋남을 `경로:줄: 설명` 으로 돌려준다. 파일마다 정규식이 오래 돌아 프로세스로 나눈다.
    저장소 전체일 때만 더는 없는 예외 자리를 센다(바뀐 파일만 보고 "쓰이지 않는다" 고 말할 수 없다)."""
    list_violation = flat_map_in_processes(functools.partial(inspect_listed_file, project_root), scope.list_absolute(project_root, (".h", ".cpp")))
    if scope.whole_repo:
        list_violation += find_stale_multiple_base_exceptions(project_root) + find_stale_state_mutator_exceptions(project_root)
    return list_violation


def run_self_check() -> int:
    """규칙 10(.cpp 상태 · `…Internal` 변경자)의 자체 시험입니다."""
    failures = 0

    def expect(condition: bool, message: str) -> None:
        nonlocal failures
        if not condition:
            print(f"[CppShape] self-check: {message}")
            failures += 1

    rules = {"stateMutatorMinFunctions": 2, "stateMutator": {}}
    helper = ("struct CapState\n{\n    vector<int32> _listGroup;\n    int32 _count = 0;\n};\n"
              "struct FooInternal\n{\n    static void group( CapState& inoutState ) { inoutState._listGroup.push_back( 1 ); }\n"
              "    static void count( CapState* pState ) { pState->_count += 1; }\n"
              "    static int32 read( const CapState& state ) { return state._count; }\n};\n")
    expect(len(check_state_mutators(helper, "Source/Engine/Foo.cpp", rules)) == 1, "two Internal statics mutating a .cpp state's public fields are flagged")
    expect(check_state_mutators(helper, "Source/Engine/Foo.h", rules) == [], "headers are rule 4's job")
    expect(check_state_mutators(helper, "Source/Engine/Foo.cpp", {"stateMutatorMinFunctions": 2, "stateMutator": {"Source/Engine/Foo.cpp|CapState": "why"}}) == [], "an exception passes")
    expect(check_state_mutators(helper, "Source/Engine/Foo.cpp", {"stateMutatorMinFunctions": 3, "stateMutator": {}}) == [], "the threshold is data")
    readers = helper.replace("CapState& inoutState ) { inoutState._listGroup.push_back( 1 ); }", "const CapState& state ) { use( state._listGroup ); }")
    expect(check_state_mutators(readers, "Source/Engine/Foo.cpp", rules) == [], "const readers are not counted")
    factory = helper.replace("CapState& inoutState ) { inoutState.", "CapState& outState ) { outState.")
    expect(check_state_mutators(factory, "Source/Engine/Foo.cpp", rules) == [], "an outX fill (parser · factory record) is not counted")
    owned = ("class CapState\n{\npublic:\n    void group();\nprivate:\n    vector<int32> _listGroup;\n};\n"
             "struct FooInternal\n{\n    static void a( CapState& s ) { s._listGroup.clear(); }\n    static void b( CapState& s ) { s._listGroup.clear(); }\n};\n")
    expect(check_state_mutators(owned, "Source/Engine/Foo.cpp", rules) == [], "private fields are not counted")
    print(f"[CppShape] self-check {'OK' if failures == 0 else 'FAIL'}")
    return 0 if failures == 0 else 1


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="C++ 모양 규칙(선언 순서 · 깃발 칸 · 익명 네임스페이스 · 주인 우선 · 중괄호 안 include · .cpp 상태 변경자)을 본다")
    parser.add_argument("--files", nargs="*", default=None, help="볼 파일(없으면 --all)")
    parser.add_argument("--all", action="store_true", help="Source 전체")
    parser.add_argument("--summary", action="store_true", help="종류별 수만")
    parser.add_argument("--self-check", action="store_true", help="규칙 10 의 자체 시험")
    args = parser.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")
    if args.self_check:
        return run_self_check()
    project_root = Path(__file__).resolve().parents[2]
    list_violation = check_cpp_shape(project_root, make_file_scope(project_root, args.files) if args.files else make_whole_scope(project_root))
    if args.summary:
        counts: dict[str, int] = {}
        for line in list_violation:
            message = line.split(": ", 1)[1]
            key = re.sub(r"`[^`]*`", "`…`", message)[:60]
            counts[key] = counts.get(key, 0) + 1
        for key, count in sorted(counts.items(), key=lambda item: -item[1]):
            print(f"{count:6d}  {key}")
        print(f"files {len({line.split(':', 1)[0] for line in list_violation})}")
    else:
        for line in list_violation:
            print(line)
    print(f"[CppShape] {'FAIL' if list_violation else 'OK'} — {len(list_violation)}건")
    return 1 if list_violation else 0


if __name__ == "__main__":
    sys.exit(main())
