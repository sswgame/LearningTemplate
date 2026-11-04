#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/CppShape.py

C++ 모양 규칙 중 컴파일러가 보지 않는 넷을 본다. 규칙 문서만 있고 검사가 없어 어긋남이 퍼졌던 자리다(2026-11-05 — 선언 순서 위반 1240 곳 · bool 깃발 1228 칸).
**왜 형식이 아니라 린트인가.** 넷 다 C++ 문법으로 막을 수 없다(멤버 순서 · 칸 형식의 선택 · 익명 네임스페이스 수 · 함수가 놓인 타입).

1. 선언 순서(`docs/agents/Includes.md`): public 멤버 변수 → 생성자 · 소멸자 → … → private 함수(따로 된 접근 구역) → private 멤버 변수(마지막).
   - public 멤버 변수가 함수 뒤에 온다 · private/protected 멤버 변수 뒤에 함수가 온다 · private/protected 함수와 변수가 한 구역에 섞였다.
2. 깃발 칸(`docs/agents/CppStyle.md` "bit packing"): `bool _bX` 멤버 대신 `uint8 _bX : 1`. 주소를 쓰는 칸(ImGui `&b` · 출력 인자) · 여러 스레드가 따로 쓰는
   이웃 칸(같은 바이트 경합)은 `bool` 로 두고 그 줄에 `NOLINT(sw-bool-flag): <이유>` 를 적는다.
3. 익명 네임스페이스(`docs/agents/Helpers.md`): `.cpp` 에 하나(전처리 갈래가 갈리는 경우는 갈래마다), 빈 블록 없음.
4. 주인 우선(`docs/agents/Helpers.md`): 같은 헤더에 타입 `X`(열거가 아닌)와 `XUtil` 이 함께 있으면 `XUtil` 의 함수는 `X` 의 (static) 멤버다.

`py -3 -m Scripts cpp-shape [--files …|--all]` 로 돌리고, 커밋 훅(`PreCommit.py`)이 staged 파일에 돌린다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Sequence

kSkipFolders = ("ThirdParty", "graphify-out", "Generated", ".gen.")
kBoolFlagExempt = "NOLINT(sw-bool-flag)"
kAccessKeywords = ("public", "protected", "private")


# ------------------------------------------------------------------------------
# 1) 주석 · 문자열을 지운 글(위치는 그대로)
# ------------------------------------------------------------------------------
def stripCode(text: str) -> str:
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


def matchBrace(text: str, openIndex: int) -> int:
    depth = 0
    for index in range(openIndex, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return len(text) - 1


def previousToken(text: str, index: int) -> str:
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
kTypeHead = re.compile(r"(?:^|[\s;{}])(?:template\s*<.*>\s*)?(class|struct|union|enum(?:\s+class|\s+struct)?)\b\s*(?:alignas\s*\([^)]*\)\s*)?(?:(?:SW_\w+|\[\[[^\]]*\]\])\s*)*(?:\w+::)*(\w*)", re.S)
kBodyPrevKeywords = ("const", "override", "final", "noexcept", "try", "mutable")


def removeAngleGroups(text: str) -> str:
    """`<…>` 를 지웁니다(템플릿 인자의 괄호가 함수로 읽히지 않게). `operator<` 같은 것은 대상이 아니다(함수로 이미 갈린다)."""
    previous = None
    while previous != text:
        previous = text
        text = re.sub(r"<[^<>]*>", "", text)
    return text


def classifyStatement(statement: str, className: str) -> str | None:
    """클래스 깊이 문장의 종류 — ctor · func · var · None(타입 별칭 · friend · static · 매크로 등 순서 밖)."""
    text = re.sub(r"\s+", " ", statement).strip()
    # 앞에 붙는 매크로 · 속성(PROPERTY( … ) · SW_API · [[nodiscard]])을 벗긴다
    while True:
        match = re.match(r"(?:\[\[[^\]]*\]\]|[A-Z][A-Z0-9_]+(?:\s*\((?:[^()]|\([^()]*\))*\))?)\s*", text)
        if match is None or match.end() == 0:
            break
        rest = text[match.end():]
        if re.match(r"[A-Z][A-Z0-9_]+\s*\(", text) and not rest:
            return None  # 매크로 한 줄(REFLECT_BODY() · SW_DECLARE_… )
        text = rest
    if not text:
        return None
    if re.match(r"(using|typedef|friend|static_assert|namespace|template\s*<[^>]*>\s*(class|struct|using))\b", text):
        return None
    text = re.sub(r"^template\s*<.*?>\s*", "", text)
    if re.match(r"(static|inline\s+static|constexpr\s+static)\b", text) or re.match(r"(constexpr|inline)\s+static\b", text):
        return None  # 정적 멤버는 순서 규칙 밖(상수 · 정적 함수)
    if re.match(r"(explicit\s+|constexpr\s+|inline\s+|virtual\s+)*~?" + re.escape(className) + r"\s*\(", text):
        return "ctor"
    # 변수 이름 뒤에 붙는 매크로(SW_GUARDED_BY( … ))를 지운다
    text = re.sub(r"\s+SW_[A-Z_]+\s*\((?:[^()]|\([^()]*\))*\)", "", text)
    flat = removeAngleGroups(text)
    if re.match(r"^[\w:\s*&]+\(\s*[*&]\s*\w+\s*\)\s*[\(\[]", flat):
        return "var"  # 함수 포인터 · 배열 참조 멤버
    if "(" in flat:
        head = flat.split("(")[0]
        if "operator" in head or ("=" not in head and "{" not in head):
            return "func"
    return "var"


class ClassInfo:
    def __init__(self, name: str, isStruct: bool, line: int) -> None:
        self.name = name
        self.isStruct = isStruct
        self.line = line
        self.items: list[tuple[str, str, int, str]] = []  # (종류, 접근, 줄, 문장)


def lineOf(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


def parseClass(code: str, start: int, end: int, info: ClassInfo, outClasses: list[ClassInfo]) -> None:
    access = "public" if info.isStruct else "private"
    index = start
    statementStart = start
    while index < end:
        char = code[index]
        if char == "{":
            head = code[statementStart:index]
            token = previousToken(code, index)
            match = kTypeHead.search(head)
            if match and "(" not in head[:match.start()] and match.group(1).startswith(("class", "struct", "union")) and "=" not in head[match.end():]:
                close = matchBrace(code, index)
                nested = ClassInfo(match.group(2), match.group(1) == "struct", lineOf(code, index))
                parseClass(code, index + 1, close, nested, outClasses)
                outClasses.append(nested)
                index = close + 1
                while index < end and code[index] in " \t\r\n":
                    index += 1
                if index < end and code[index] == ";":
                    index += 1
                else:
                    # `struct { … } _member;` — 칸이다
                    semi = code.find(";", index)
                    info.items.append(("var", access, lineOf(code, index), code[index:semi]))
                    index = semi + 1
                statementStart = index
                continue
            if match and match.group(1).startswith("enum"):
                close = matchBrace(code, index)
                index = code.find(";", close) + 1
                statementStart = index
                continue
            if (token and (token[0].isalnum() or token[0] == "_") and token not in kBodyPrevKeywords) or token in (">", "]", "=", ","):
                index = matchBrace(code, index) + 1  # 중괄호 초기화 — 문장을 잇는다
                continue
            close = matchBrace(code, index)  # 함수 본문
            kind = classifyStatement(head, info.name)
            if kind:
                info.items.append((kind, access, lineOf(code, index), head))
            index = close + 1
            probe = index
            while probe < end and code[probe].isspace():
                probe += 1
            if probe < end and code[probe] == ";":
                index = probe + 1
            statementStart = index
            continue
        if char == ";":
            kind = classifyStatement(code[statementStart:index], info.name)
            if kind:
                info.items.append((kind, access, lineOf(code, index), code[statementStart:index]))
            statementStart = index + 1
        elif char == ":" and code[index + 1:index + 2] != ":" and code[index - 1:index] != ":":
            match = re.search(r"\b(public|private|protected)\s*$", code[statementStart:index])
            if match:
                access = match.group(1)
                info.items.append(("access", access, lineOf(code, index), ""))
                statementStart = index + 1
        index += 1


def parseScope(code: str, start: int, end: int, outClasses: list[ClassInfo]) -> None:
    index = start
    statementStart = start
    while index < end:
        char = code[index]
        if char == "{":
            head = code[statementStart:index]
            match = kTypeHead.search(head)
            close = matchBrace(code, index)
            if match and match.group(1).startswith(("class", "struct", "union")) and "(" not in head[:match.start()] and "=" not in head[match.end():] and match.group(2):
                info = ClassInfo(match.group(2), match.group(1) == "struct", lineOf(code, index))
                parseClass(code, index + 1, close, info, outClasses)
                outClasses.append(info)
            elif re.search(r"\bnamespace\b[^()]*$|\bextern\s*$", head.strip() and head or ""):
                parseScope(code, index + 1, close, outClasses)
            index = close + 1
            statementStart = index
            continue
        if char == ";":
            statementStart = index + 1
        index += 1


def collectClasses(code: str) -> list[ClassInfo]:
    classes: list[ClassInfo] = []
    parseScope(code, 0, len(code), classes)
    return classes


# ------------------------------------------------------------------------------
# 3) 검사
# ------------------------------------------------------------------------------
def checkDeclarationOrder(info: ClassInfo) -> list[tuple[int, str]]:
    problems: list[tuple[int, str]] = []
    firstFunction = None
    for kind, access, line, _ in info.items:
        if kind in ("ctor", "func") and firstFunction is None:
            firstFunction = line
        if kind == "var" and access == "public" and firstFunction is not None:
            problems.append((line, f"`{info.name}` 의 public 멤버 변수가 함수 뒤에 있다(public 변수는 맨 앞 — {firstFunction} 줄의 함수보다 먼저)"))
    firstHiddenVariable = None
    for kind, access, line, _ in info.items:
        if kind == "var" and access != "public" and firstHiddenVariable is None:
            firstHiddenVariable = line
        elif kind in ("ctor", "func") and firstHiddenVariable is not None:
            problems.append((line, f"`{info.name}` 의 함수가 private/protected 멤버 변수({firstHiddenVariable} 줄) 뒤에 있다(멤버 변수는 맨 끝)"))
            break
    section = None
    kinds: set[str] = set()
    sectionLine = info.line
    for kind, access, line, _ in info.items + [("access", "", 0, "")]:
        if kind == "access":
            if section in ("private", "protected") and {"func", "var"} <= kinds:
                problems.append((sectionLine, f"`{info.name}` 의 {section} 구역에 함수와 멤버 변수가 섞였다(함수 구역 · 변수 구역을 따로 연다)"))
            section, kinds, sectionLine = access, set(), line
            continue
        if section is None:
            section = access
        kinds.add("func" if kind == "ctor" else kind)
    return problems


kBoolMember = re.compile(r"^\s*(?:mutable\s+)?(?:std::)?bool\s+(_b\w+)\s*(?:\{[^{};]*\}|=\s*[^;]+)?\s*;")


def checkBoolFlags(info: ClassInfo, rawLines: list[str]) -> list[tuple[int, str]]:
    problems: list[tuple[int, str]] = []
    for kind, _, line, statement in info.items:
        if kind != "var":
            continue
        text = re.sub(r"\s+", " ", statement).strip()
        text = re.sub(r"^(?:(?:\[\[[^\]]*\]\]|[A-Z][A-Z0-9_]+(?:\s*\((?:[^()]|\([^()]*\))*\))?)\s*)*", "", text)
        match = kBoolMember.match(" " + text + ";")
        if match is None:
            continue
        sourceLine = rawLines[line - 1] if 0 < line <= len(rawLines) else ""
        if kBoolFlagExempt in sourceLine:
            continue
        problems.append((line, f"`{info.name}::{match.group(1)}` 는 깃발 칸이다 — `uint8 {match.group(1)} : 1`(값은 SW_TRUE / SW_FALSE, 생성자에서). 주소를 써야 하면 그 줄에 `{kBoolFlagExempt}: <이유>`"))
    return problems


def checkAnonymousNamespaces(code: str) -> list[tuple[int, str]]:
    problems: list[tuple[int, str]] = []
    blocks = list(re.finditer(r"\bnamespace\s*\{", code))
    for match in blocks:
        openIndex = code.index("{", match.start())
        close = matchBrace(code, openIndex)
        if code[openIndex + 1:close].strip() == "":
            problems.append((lineOf(code, match.start()), "빈 익명 네임스페이스 — 지운다"))
    return problems


def checkUtilBesideOwner(code: str, relative: str) -> list[tuple[int, str]]:
    if not relative.endswith(".h"):
        return []
    enums = set(re.findall(r"\benum\s+(?:class|struct)\s+(?:SW_\w+\s+)?(\w+)", code))
    types = {}
    for match in re.finditer(r"(?<!enum )\b(?:class|struct)\s+(?:SW_\w+\s+)?(\w+)\s*(?:final\s*)?(?::[^{;:][^{;]*)?\{", code):
        types.setdefault(match.group(1), lineOf(code, match.start()))
    problems = []
    for name, line in types.items():
        owner = name[:-4]
        if name.endswith("Util") and owner in types and owner not in enums:
            problems.append((line, f"`{name}` 가 같은 헤더의 `{owner}` 옆에 있다 — `{owner}` 의 값을 다루는 함수는 `{owner}` 의 (static) 멤버다(Helpers.md 주인 우선)"))
    return problems


def inspectFile(path: Path, relative: str) -> list[str]:
    raw = path.read_text(encoding="utf-8-sig", errors="ignore")
    code = stripCode(raw)
    rawLines = raw.splitlines()
    listProblem: list[tuple[int, str]] = []
    for info in collectClasses(code):
        listProblem += checkDeclarationOrder(info)
        listProblem += checkBoolFlags(info, rawLines)
    if relative.endswith(".cpp"):
        listProblem += checkAnonymousNamespaces(code)
    listProblem += checkUtilBesideOwner(code, relative)
    return [f"{relative}:{line}: {message}" for line, message in sorted(set(listProblem))]


def isTarget(relative: str) -> bool:
    return relative.startswith("Source/") and relative.endswith((".h", ".cpp")) and not any(skip in relative for skip in kSkipFolders)


def checkCppShape(projectRoot: Path, listFile: Sequence[str]) -> list[str]:
    """@p listFile 가운데 Source 의 C++ 파일을 보고 어긋남을 `경로:줄: 설명` 으로 돌려준다."""
    listViolation: list[str] = []
    for file in listFile:
        path = Path(file).resolve()
        try:
            relative = path.relative_to(projectRoot).as_posix()
        except ValueError:
            continue
        if isTarget(relative) and path.exists():
            listViolation += inspectFile(path, relative)
    return listViolation


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="C++ 모양 규칙(선언 순서 · 깃발 칸 · 익명 네임스페이스 · 주인 우선)을 본다")
    parser.add_argument("--files", nargs="*", default=None, help="볼 파일(없으면 --all)")
    parser.add_argument("--all", action="store_true", help="Source 전체")
    parser.add_argument("--summary", action="store_true", help="종류별 수만")
    args = parser.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")
    projectRoot = Path(__file__).resolve().parents[2]
    if args.files:
        listFile = args.files
    else:
        listFile = [str(path) for path in (projectRoot / "Source").rglob("*") if path.suffix in (".h", ".cpp")]
    listViolation = checkCppShape(projectRoot, listFile)
    if args.summary:
        counts: dict[str, int] = {}
        for line in listViolation:
            message = line.split(": ", 1)[1]
            key = re.sub(r"`[^`]*`", "`…`", message)[:60]
            counts[key] = counts.get(key, 0) + 1
        for key, count in sorted(counts.items(), key=lambda item: -item[1]):
            print(f"{count:6d}  {key}")
        print(f"files {len({line.split(':', 1)[0] for line in listViolation})}")
    else:
        for line in listViolation:
            print(line)
    print(f"[CppShape] {'FAIL' if listViolation else 'OK'} — {len(listViolation)}건")
    return 1 if listViolation else 0


if __name__ == "__main__":
    sys.exit(main())
