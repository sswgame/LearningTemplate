"""
Scripts/lint/name_collection.py

이름 모으기 — 이름 린트(`naming_words.py`)와 전수 집계(`naming-census` 명령)가 같은 것을 본다: C++ 선언, 스크립트 선언, 파일 · 폴더 이름과 그 낱말.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Iterator, Sequence

from lint.cpp_code import strip_comments_and_strings
from lint.lint_scope import LintScope, read_texts

EXTENSIONS = (".h", ".cpp", ".inl")
ANY_WORD = re.compile(r"[A-Z]+(?![a-z])|[A-Z]?[a-z0-9]+")


def split_words(name: str, function: bool = False) -> list[str]:
    """식별자 하나의 단어들 — 앞 `_` 과 머리(`k` 상수 · `b` 불 · `s` 정적, 변수면 `arr` · `list` · `map` · `set` 컬렉션)를 떼고 낙타 · 밑줄로 가른다.
    함수 이름은 컬렉션 머리를 떼지 않는다(`setAxisTags` 의 `set` 은 동사다)."""
    words: list[str] = []
    for part in name.strip("_").split("_"):
        words.extend(ANY_WORD.findall(part))
    prefixes = ("k", "b", "s", "g", "m", "p") if function else ("k", "b", "s", "g", "m", "p", "arr", "list", "map", "set")
    if len(words) > 1 and words[0] in prefixes:
        words = words[1:]
    return words


# 선언 모으기 — 낱말 표(`_words`)와 전수 집계(`py -3 -m Scripts naming-census`)가 같은 것을 본다. 쓰는 자리(호출 · 참조)가 아니라 선언만 본다:
# 우리가 짓지 않은 바깥 이름(Win32 · Vulkan · ImGui)은 쓰기만 하므로 대부분 빠지고, 바깥 가상 함수를 덮어쓰는 자리는 `_externalPatterns` 가 가린다.
NOT_NAME = {"return", "else", "new", "delete", "throw", "case", "goto", "co_return", "co_yield", "co_await", "and", "or", "not", "typename", "class", "struct",
            "enum", "union", "using", "namespace", "operator", "sizeof", "if", "while", "for", "switch", "do", "catch", "decltype", "alignof", "alignas", "static_assert",
            "public", "private", "protected", "const", "constexpr", "override", "final", "noexcept", "volatile", "default", "mutable", "static", "inline", "virtual",
            "explicit", "friend", "template", "typedef", "extern", "auto", "void", "defined", "this", "nullptr", "true", "false", "requires", "export", "import"}
TYPE_DECLARATION = re.compile(r"\b(?:class|struct|union|enum(?:\s+class|\s+struct)?)\s+(?:(?:SW_\w+|alignas\s*\([^)]*\)|\[\[[^\]]*\]\])\s+)*([A-Za-z_]\w*)")
ENUM_CLASS_DECLARATION = re.compile(r"\benum\s+(?:class|struct)\s+(?:(?:SW_\w+|\[\[[^\]]*\]\])\s+)*([A-Za-z_]\w*)")
ENUM_BODY = re.compile(r"\benum\b[^;{()]*\{([^}]*)\}")
ALIAS_DECLARATION = re.compile(r"\busing\s+([A-Za-z_]\w*)\s*=|\btypedef\b[^;{}]*?\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*;")
MACRO_DECLARATION = re.compile(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)", re.M)
FUNCTION_DECLARATION = re.compile(r"\b([A-Za-z_]\w*)[\s\*&>]+((?:[A-Za-z_]\w*::)*)~?([A-Za-z_]\w*)\s*\(")
VARIABLE_DECLARATION = re.compile(r"\b([A-Za-z_]\w*)[\s\*&>]+([A-Za-z_]\w*)\s*(?=[;=\{\[,\)]|:(?!:))")
MACRO_NAME = re.compile(r"[A-Z0-9_]+")
LETTERS = re.compile(r"[A-Za-z]+")
PARAMETER_PREFIXES = ("out", "inout", "outb", "inoutb", "arr", "list", "map", "set")
DECLARATION_KINDS = ("type", "enumerator", "alias", "function", "member", "constant", "variable", "macro", "command", "file", "folder")


def classify_variable(name: str) -> str:
    if name.startswith(("_", "m_", "s_")) or re.match(r"m[A-Z]", name):
        return "member"
    if re.match(r"k[A-Z]", name):
        return "constant"
    return "variable"


def collect_declarations(code: str) -> list[tuple[str, str]]:
    """주석 · 문자열을 뺀 코드의 (종류, 이름) — 형식 · 열거자 · 별칭 · 함수 · 멤버 · 상수 · 변수(지역 · 인자 · 전역) · 매크로.
    shortcut: 정규식이라 `Type value( args )` 직접 초기화는 함수로 센다 — 낱말 검사는 종류와 무관해 판정이 바뀌지 않는다, 종류별 수가 중요해지면 clang 색인으로 바꾼다."""
    list_declaration: list[tuple[str, str]] = []
    for match in TYPE_DECLARATION.finditer(code):
        if match.group(1) not in NOT_NAME:
            list_declaration.append(("type", match.group(1)))
    for match in ENUM_BODY.finditer(code):
        for item in match.group(1).split(","):
            found = re.match(r"\s*([A-Za-z_]\w*)", item)
            if found:
                list_declaration.append(("enumerator", found.group(1)))
    for match in ALIAS_DECLARATION.finditer(code):
        name = match.group(1) or match.group(2)
        if name not in NOT_NAME:
            list_declaration.append(("alias", name))
    for match in MACRO_DECLARATION.finditer(code):
        list_declaration.append(("macro", match.group(1)))
    for match in FUNCTION_DECLARATION.finditer(code):
        before, name = match.group(1), match.group(3)
        if before not in NOT_NAME - {"const", "constexpr", "static", "inline", "virtual", "explicit", "friend", "auto", "void", "extern"} \
                and name not in NOT_NAME and MACRO_NAME.fullmatch(name) is None:
            list_declaration.append(("function", name))
    for match in VARIABLE_DECLARATION.finditer(code):
        before, name = match.group(1), match.group(2)
        if before not in NOT_NAME - {"const", "constexpr", "static", "inline", "auto", "mutable", "volatile", "extern"} and name not in NOT_NAME \
                and MACRO_NAME.fullmatch(name) is None:
            list_declaration.append((classify_variable(name), name))
    return list_declaration


def collect_path_names(relative: str, root_depth: int = 1) -> list[tuple[str, str]]:
    """경로의 (file, 파일 줄기) 와 (folder, 폴더 이름)들 — 뿌리 마디(`Source` · `Scripts` · `.claude/hooks` …, @p root_depth 개)는 뺀다."""
    parts = relative.split("/")[root_depth:]
    return [("folder", part) for part in parts[:-1]] + [("file", parts[-1].split(".")[0])] if parts else []


# 스크립트 · 빌드 이름 — Python(Scripts · 훅 · 스킬 스크립트) · 셸 · CMake. 파이썬은 PEP 8(docs/agents/NamingCpp.md: 모듈 · 함수 snake_case ·
# 클래스 PascalCase · 상수 UPPER_CASE)의 꼴도 보고, CMake 는 함수 꼴(sw_snake_case)과 낱말을 본다(옵션 SW_ 는 낱말만). CLI 명령(`Command` 의 `name`)과 린트 규칙 id(`LintRule` 의 `id`)도 이름이다.
SCRIPT_ROOTS = ("Scripts", ".claude/hooks", ".claude/skills", "cmake")
SCRIPT_EXTENSIONS = (".py", ".sh", ".cmake")
PYTHON_FUNCTION = re.compile(r"^[ \t]*def[ \t]+([A-Za-z_]\w*)", re.M)
PYTHON_CLASS = re.compile(r"^[ \t]*class[ \t]+([A-Za-z_]\w*)", re.M)
PYTHON_CONSTANT = re.compile(r"^(_?[A-Z][A-Z0-9_]*|_?k[A-Z]\w*)[ \t]*[:=]", re.M)   # 옛 kPascalCase 도 모아 꼴 검사가 막는다
PYTHON_SHAPES = {"function": (re.compile(r"_*[a-z0-9_]+"), "함수는 snake_case"), "type": (re.compile(r"_?[A-Z][A-Za-z0-9]*"), "클래스는 PascalCase"),
               "constant": (re.compile(r"_?[A-Z][A-Z0-9_]*"), "모듈 상수는 UPPER_CASE"), "file": (re.compile(r"_*[a-z0-9_]+"), "모듈 파일은 snake_case"),
               "folder": (re.compile(r"[a-z0-9_]+"), "패키지 폴더는 snake_case")}
COMMAND_NAME = re.compile(r"^    (?:name|id) = \"([a-z][a-z0-9-]*)\"", re.M)
SHELL_FUNCTION = re.compile(r"^[ \t]*(?:function[ \t]+)?([A-Za-z_]\w*)[ \t]*\(\)", re.M)
CMAKE_FUNCTION = re.compile(r"\b(?:function|macro)\s*\(\s*([A-Za-z_]\w*)", re.I)
CMAKE_OPTION = re.compile(r"\boption\s*\(\s*([A-Za-z_]\w*)", re.I)
CMAKE_FUNCTION_SHAPE = re.compile(r"sw_[a-z0-9]+(?:_[a-z0-9]+)*")
CMAKE_OUTSIDE_ROOTS = ("Source", "Test", "ThirdParty")   # cmake/ 밖의 CMake — 함수 꼴만 본다(파일 · 폴더 이름은 그 뿌리의 규칙이 본다)
FOREIGN_CMAKE_PART = "vcpkg-port"                        # 받아 온 vcpkg 포트는 남의 코드다


def collect_script_declarations(path: str, text: str) -> list[tuple[str, str]]:
    """스크립트 파일 하나의 (종류, 이름) — 함수 · 클래스 · 모듈 상수 · CLI 명령(파이썬), 함수(셸), 함수 · 매크로 · 옵션(CMake)."""
    if path.endswith(".py"):
        return ([("function", name) for name in PYTHON_FUNCTION.findall(text) if not name.startswith("__")] + [("type", name) for name in PYTHON_CLASS.findall(text)]
                + [("constant", name) for name in PYTHON_CONSTANT.findall(text)] + [("command", name) for name in COMMAND_NAME.findall(text)])
    if path.endswith(".sh"):
        return [("function", name) for name in SHELL_FUNCTION.findall(text)]
    return [("function", name) for name in CMAKE_FUNCTION.findall(text)] + [("macro", name) for name in CMAKE_OPTION.findall(text)]


def is_cmake_path(path: str) -> bool:
    return path.endswith((".cmake", "CMakeLists.txt"))


def split_lower_words(name: str, kind: str) -> list[str]:
    """낱말 표의 열쇠 — `split_words` 의 낱말을 숫자에서 다시 가르고(`Vector3D` → vector · d) 소문자로, 숫자만인 마디와 한 글자는 뺀다."""
    words: list[str] = []
    for word in split_words(name, function=kind == "function"):
        words.extend(piece.lower() for piece in LETTERS.findall(word) if len(piece) > 1)
    while kind != "function" and len(words) > 1 and words[0] in PARAMETER_PREFIXES:   # 출력 인자 머리(`outbChanged` · `inoutListCrumb`)
        words = words[1:]
    return words


def is_external_name(name: str, patterns: Sequence[re.Pattern]) -> bool:
    return any(pattern.match(name) for pattern in patterns)


def list_file_names(path: str, text: str) -> list[tuple[str, str]]:
    """파일 하나가 선언한 (종류, 이름) — C++ 은 @p text 가 주석 · 문자열을 지운 글(전처리 줄은 둔 것)이다."""
    if path.startswith("Source/") and path.endswith(EXTENSIONS):
        return collect_declarations(text)
    if is_script(path):
        return collect_script_declarations(path, text)
    return [("function", name) for name in CMAKE_FUNCTION.findall(text)]


def declares_names(path: str) -> bool:
    """`list_file_names` 로 볼 파일 — Source C++, 스크립트, cmake 밖의 CMake(받아 온 포트 제외)."""
    return ((path.startswith("Source/") and path.endswith(EXTENSIONS)) or is_script(path) or path == "CMakeLists.txt"
            or (is_cmake_path(path) and path.startswith(tuple(root + "/" for root in CMAKE_OUTSIDE_ROOTS)) and FOREIGN_CMAKE_PART not in path.split("/")))


def is_script(path: str) -> bool:
    return path.startswith(tuple(root + "/" for root in SCRIPT_ROOTS)) and (path.endswith(SCRIPT_EXTENSIONS) or path.endswith("CMakeLists.txt"))


def list_path_names(path: str) -> list[tuple[str, str]]:
    """경로의 (file · folder, 이름) — Source 의 모든 파일과 스크립트."""
    if path.startswith("Source/"):
        return collect_path_names(path)
    if is_script(path):
        return [(kind, name) for kind, name in collect_path_names(path, 2 if path.startswith(".claude/") else 1) if name != "CMakeLists"]
    return []


def iterate_names(project_root: Path, scope: LintScope) -> Iterator[tuple[str, str, str]]:
    """범위 안의 (종류, 이름, 파일) — 선언과 경로 이름."""
    for path, text in read_texts(project_root, [path for path in scope.list_file if declares_names(path)]):
        code = strip_comments_and_strings(text) if path.endswith(EXTENSIONS) else text
        for kind, name in list_file_names(path, code):
            yield kind, name, path
    for path in scope.list_file:
        for kind, name in list_path_names(path):
            yield kind, name, path
