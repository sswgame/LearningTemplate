#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/naming_words.py

이름이 업계 용어(언리얼 · 유니티 · Godot · 일반 프로그래밍)를 쓰는지 표(`Config/Rules/Lint/NamingWords.json`)와 대조한다.

**왜 린트인가.** 이름이 업계 용어인지는 형식이 말할 수 없다. 표 없이 눈에 띈 이름만 고치면 `drain` · `Since` 처럼 같은 일에 새 낱말이
계속 들어온다(2026-11-07). 예전 검사는 함수 첫 단어와 형식 끝 단어만 봐서, 이름 가운데의 금지어(`hasPendingWork` · `XMLFields::readProperties`)와
3인칭 문장형 술어(`shouldDrawBatchInPass`) · 동사 없는 게터(`materialGroupFor`)가 그대로 통과했다(2026-11-12 Source 전체 점검 약 470 건).

규칙:
- function · suffix — 함수 첫 단어는 `_functionWords`, 형식 끝 단어는 `_typeSuffixes` 에 있어야 한다.
- verb — 함수 첫 단어가 금지 동사(`_banned`)면 허용 표에 있어도 실패한다(예전에는 허용 표에 없을 때 안내로만 썼다).
- term — 금지 명사(`_bannedTerms`)는 모든 식별자(함수 · 형식 · 멤버 · 지역)의 **모든 단어**에서 본다. 표준 합성어(`WorkArea` — Win32 `SPI_GETWORKAREA`)는 `_keepTerms`.
- predicate — `bool` 을 돌려주는 함수의 첫 단어가 3인칭 동사(끝이 s)면 `_predicateWords`(언리얼 `Is · Has · Can · Should · Uses · Needs · Requires · Supports …`)여야 한다.
- lookup — 끝 단어가 `For` · `Of` 인 함수는 조회 동사(`_lookupWords` — get · find · compute …)로 시작해야 한다(`materialGroupFor` 는 "찾거나 만든다" 를 숨겼다).
- word — 모든 선언 이름(형식 · 열거자 · 별칭 · 함수 · 멤버 · 상수 · 변수 · 매크로)과 `Source` 의 파일 · 폴더 이름, 스크립트(`Scripts` · `.claude/hooks` ·
  `.claude/skills` · `cmake` 의 함수 · 클래스 · 상수 · CLI 명령 · 규칙 id · CMake 함수 · 옵션 · 파일 · 폴더 이름)의 **모든 낱말**이 낱말 표
  (`_words` 의 a 업계 · b 게임)에 있어야 한다. 앞 규칙들은 함수 첫 단어 · 형식 끝 단어만 허용 표로 봐서, 멤버 · 파일 이름 · 이름 가운데의 조어
  (`SceneWalkKey` · `aimAtMark` · `parkHistory`)가 들어왔다(2026-11-13). 문맥에 따라 맞기도 틀리기도 한 낱말은 `_scopedWords` 가 쓸 경로를 정한다
  (게임의 walk 는 맞고 엔진의 walk 는 순회다). 바깥 철자(`PFN_` · `VK_` · ImGui 깃발)는 `_externalPatterns`, 받아 온 스킬은 `_externalPaths` 가 뺀다.
  파이썬 이름의 PEP 8 꼴(모듈 · 함수 snake_case, 클래스 PascalCase, 상수 UPPER_CASE — docs/agents/NamingScripts.md)도 여기서 막는다. 전수 집계는 `py -3 -m Scripts naming-census`.
- 이름 하나씩의 예외는 `_keep`, 전수 개명한 옛 이름은 `_bannedNames`(주석 · 문자열 밖 코드만 본다).

verb · term · predicate · lookup · word 는 기준선(`Config/Rules/Lint/NamingBaseline.json`)을 둔다 — 지금 있는 이름만 적혀 있고 **줄기만 한다**:
새 위반은 실패, 고쳐 사라진 이름이 기준선에 남아도 실패(`--lower-baseline` 으로 뺀다). 영역별 이전이 기준선을 0 으로 줄인다.

`py -3 -m Scripts lint naming-words [--self-check | --write-baseline | --lower-baseline]` 로 돌리고, 커밋 훅(`rules.py` 표)이 돌린다.
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
from lint.lint_scope import LintScope, is_under, make_whole_scope, read_texts  # noqa: E402

CONFIG_PATH = "Config/Rules/Lint/NamingWords.json"
BASELINE_PATH = "Config/Rules/Lint/NamingBaseline.json"
RATCHET_RULES = ("verb", "term", "predicate", "lookup", "word")
EXTENSIONS = (".h", ".cpp", ".inl")
COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
STRING = re.compile(r'R"\((?:.|\n)*?\)"|"(?:[^"\\\n]|\\.)*"')
FUNCTION = re.compile(r"\b_?([a-z][a-z0-9]*)[A-Z]\w*\s*\(")
TYPE = re.compile(r"\b(?:class|struct|enum class)\s+(?:SW_\w+\s+)?([A-Z]\w+)")
BOOL_FUNCTION = re.compile(r"\bbool\s+(?:\w+::)*(_?[a-z][A-Za-z0-9]*)\s*\(")
IDENTIFIER = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
WORD = re.compile(r"[A-Z][a-z0-9]+|[A-Z]+(?![a-z])")
ANY_WORD = re.compile(r"[A-Z]+(?![a-z])|[A-Z]?[a-z0-9]+")


def strip_code(text: str) -> str:
    return STRING.sub('""', COMMENT.sub("", text))


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


# 스크립트 · 빌드 이름 — Python(Scripts · 훅 · 스킬 스크립트) · 셸 · CMake. 파이썬은 PEP 8(docs/agents/NamingScripts.md: 모듈 · 함수 snake_case ·
# 클래스 PascalCase · 상수 UPPER_CASE)의 꼴도 보고, CMake(sw_camelCase · SW_ 옵션)는 낱말만 본다. CLI 하위 명령(`Subcommand("…"`)과 린트 규칙 id(`Rule("…"`)도 이름이다.
SCRIPT_ROOTS = ("Scripts", ".claude/hooks", ".claude/skills", "cmake")
SCRIPT_EXTENSIONS = (".py", ".sh", ".cmake")
PYTHON_FUNCTION = re.compile(r"^[ \t]*def[ \t]+([A-Za-z_]\w*)", re.M)
PYTHON_CLASS = re.compile(r"^[ \t]*class[ \t]+([A-Za-z_]\w*)", re.M)
PYTHON_CONSTANT = re.compile(r"^(_?[A-Z][A-Z0-9_]*|_?k[A-Z]\w*)[ \t]*[:=]", re.M)   # 옛 kPascalCase 도 모아 꼴 검사가 막는다
PYTHON_SHAPES = {"function": (re.compile(r"_*[a-z0-9_]+"), "함수는 snake_case"), "type": (re.compile(r"_?[A-Z][A-Za-z0-9]*"), "클래스는 PascalCase"),
               "constant": (re.compile(r"_?[A-Z][A-Z0-9_]*"), "모듈 상수는 UPPER_CASE"), "file": (re.compile(r"_*[a-z0-9_]+"), "모듈 파일은 snake_case"),
               "folder": (re.compile(r"[a-z0-9_]+"), "패키지 폴더는 snake_case")}
COMMAND_NAME = re.compile(r"\b(?:Subcommand|Rule)\(\s*\"([a-z][a-z0-9-]*)\"")
SHELL_FUNCTION = re.compile(r"^[ \t]*(?:function[ \t]+)?([A-Za-z_]\w*)[ \t]*\(\)", re.M)
CMAKE_FUNCTION = re.compile(r"\b(?:function|macro)\s*\(\s*([A-Za-z_]\w*)", re.I)
CMAKE_OPTION = re.compile(r"\boption\s*\(\s*([A-Za-z_]\w*)", re.I)


def collect_script_declarations(path: str, text: str) -> list[tuple[str, str]]:
    """스크립트 파일 하나의 (종류, 이름) — 함수 · 클래스 · 모듈 상수 · CLI 명령(파이썬), 함수(셸), 함수 · 매크로 · 옵션(CMake)."""
    if path.endswith(".py"):
        return ([("function", name) for name in PYTHON_FUNCTION.findall(text) if not name.startswith("__")] + [("type", name) for name in PYTHON_CLASS.findall(text)]
                + [("constant", name) for name in PYTHON_CONSTANT.findall(text)] + [("command", name) for name in COMMAND_NAME.findall(text)])
    if path.endswith(".sh"):
        return [("function", name) for name in SHELL_FUNCTION.findall(text)]
    return [("function", name) for name in CMAKE_FUNCTION.findall(text)] + [("macro", name) for name in CMAKE_OPTION.findall(text)]


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


def is_third_person_verb(word: str) -> bool:
    """3인칭 단수 동사 꼴(끝이 s)이면 true — s 로 끝나는 명사 · 명령형(`focus` · `axis` · `atlas` · `class`)은 뺀다."""
    return word.endswith("s") and not word.endswith(("ss", "us", "is", "as", "os"))


def collect_words(code: str) -> tuple[list[tuple[str, str]], list[tuple[str, str]]]:
    """코드(주석 · 문자열을 뺀 글)의 (함수 앞 단어, 이름) 과 (형식 끝 단어, 이름) 을 돌려준다."""
    list_function = [(match.group(1), match.group(0).split("(")[0].strip()) for match in FUNCTION.finditer(code)]
    list_suffix: list[tuple[str, str]] = []
    for match in TYPE.finditer(code):
        words = WORD.findall(match.group(1))
        if words:
            list_suffix.append((words[-1], match.group(1)))
    return list_function, list_suffix


def find_term_word(name: str, banned_terms: dict, keep_terms: Sequence[str]) -> str | None:
    """@p name 의 단어 가운데 금지 명사가 있으면 그 단어(표 철자)입니다. 표준 합성어(`_keepTerms`)는 먼저 지운다."""
    stripped = name
    for term in keep_terms:
        stripped = re.sub(re.escape(term), "", stripped, flags=re.I)
    lowered = {term.lower(): term for term in banned_terms}
    for word in split_words(stripped):
        if word.lower() in lowered:
            return lowered[word.lower()]
    return None


def classify_names(list_source: Iterable[tuple[str, str]], table: dict) -> tuple[list[str], dict[str, dict[str, str]]]:
    """(기준선 없는 문제 목록, 규칙 → {이름: 경로 · 안내}) — 기준선 규칙의 위반은 이름으로 모아 기준선과 견준다."""
    allowed_function = set(table["_functionWords"])
    allowed_suffix = set(table["_typeSuffixes"])
    banned_verbs = table["_banned"]
    banned_terms = table.get("_bannedTerms", {})
    keep_terms = table.get("_keepTerms", [])
    predicate_words = set(table.get("_predicateWords", []))
    lookup_words = set(table.get("_lookupWords", []))
    keep = table["_keep"]
    banned_names = table.get("_bannedNames", {})
    banned_pattern = re.compile(r"\b(" + "|".join(map(re.escape, banned_names)) + r")\b") if banned_names else None

    list_problem: list[str] = []
    seen: set[str] = set()
    ratchet: dict[str, dict[str, str]] = {rule: {} for rule in RATCHET_RULES}

    def record(rule: str, name: str, path: str, hint: str) -> None:
        if name not in keep and name not in ratchet[rule]:
            ratchet[rule][name] = f"{path}: {hint}"

    for path, text in list_source:
        code = strip_code(text)
        if banned_pattern is not None:
            for match in banned_pattern.finditer(code):
                name = match.group(1)
                if f"name:{name}" not in seen:
                    seen.add(f"name:{name}")
                    list_problem.append(f"{path}: 옛 이름 '{name}' — {banned_names[name]}")
        list_function, list_suffix = collect_words(code)
        for word, name in list_function:
            if name in keep:
                continue
            if word in banned_verbs:
                record("verb", name, path, f"함수 앞 단어 '{word}'({name}) 는 금지 동사다 — {banned_verbs[word]}")
            elif word not in allowed_function and name not in seen:
                seen.add(name)
                list_problem.append(f"{path}: 함수 앞 단어 '{word}'({name}) 가 표에 없다 — 업계 용어와 대조한 뒤 {CONFIG_PATH} 의 _functionWords 에 올린다")
            words = split_words(name, function=True)
            if len(words) > 1 and words[-1] in ("For", "Of") and words[0] not in lookup_words:
                record("lookup", name, path, f"'{name}' 는 동사 없는 For · Of 게터다 — get · find · findOrAdd · compute 로 시작한다(_lookupWords)")
        for word, name in list_suffix:
            if word not in allowed_suffix and name not in keep and name not in seen:
                seen.add(name)
                hint = banned_terms.get(word, f"업계 용어와 대조한 뒤 {CONFIG_PATH} 의 _typeSuffixes 에 올린다")
                list_problem.append(f"{path}: 형식 끝 단어 '{word}'({name}) 가 표에 없다 — {hint}")
        for match in BOOL_FUNCTION.finditer(code):
            name = match.group(1)
            words = split_words(name, function=True)
            first = words[0] if words else ""
            if is_third_person_verb(first) and first not in predicate_words:
                record("predicate", name, path, f"bool 함수 '{name}' 가 3인칭 문장형('{first}')이다 — is · has · can · should · uses · needs · requires · supports 로(_predicateWords, 언리얼 Is/Has/Uses/Requires…)")
        if banned_terms:
            for match in IDENTIFIER.finditer(code):
                name = match.group(0)
                if name in keep or name in ratchet["term"]:
                    continue
                term = find_term_word(name, banned_terms, keep_terms)
                if term is not None:
                    record("term", name, path, f"'{name}' 에 금지 명사 '{term}' 가 있다 — {banned_terms[term]}")
    return list_problem, ratchet


def compare_baseline(ratchet: dict[str, dict[str, str]], baseline: dict[str, list[str]], whole_repo: bool = True) -> list[str]:
    list_problem: list[str] = []
    for rule in RATCHET_RULES:
        allowed = set(baseline.get(rule, []))
        for name, where in sorted(ratchet[rule].items()):
            if name not in allowed:
                list_problem.append(f"[{rule}] {where}")
        for name in sorted(allowed - set(ratchet[rule])) if whole_repo else ():   # 고친 이름은 저장소 전체를 봐야 안다
            list_problem.append(f"[{rule}] 기준선의 '{name}' 가 더는 없다 — 고친 이름을 기준선에서 뺀다(`py -3 Scripts/lint/naming_words.py --lower-baseline`)")
    return list_problem


def read_sources(project_root: Path, scope: LintScope) -> list[tuple[str, str]]:
    return read_texts(project_root, scope.select(("Source",), EXTENSIONS))


def collect_source_names(project_root: Path, scope: LintScope) -> list[tuple[str, str, str]]:
    """범위 안의 (종류, 이름, 파일) — `Source` 의 C++ 선언과 모든 파일의 파일 · 폴더 이름, 스크립트 뿌리(`SCRIPT_ROOTS`)의 선언과 스크립트 파일 · 폴더 이름."""
    list_name = [(kind, name, path) for path, text in read_sources(project_root, scope) for kind, name in collect_declarations(strip_code(text))]
    for path in scope.select(("Source",)):
        list_name.extend((kind, name, path) for kind, name in collect_path_names(path))
    list_script = scope.select(SCRIPT_ROOTS, SCRIPT_EXTENSIONS) + [path for path in scope.select(SCRIPT_ROOTS) if path.endswith("CMakeLists.txt")]
    for path, text in read_texts(project_root, list_script):
        list_name.extend((kind, name, path) for kind, name in collect_script_declarations(path, text))
        depth = 2 if path.startswith(".claude/") else 1
        list_name.extend((kind, name, path) for kind, name in collect_path_names(path, depth) if name != "CMakeLists")
    return list_name


def read_table(project_root: Path) -> dict:
    return json.loads((project_root / CONFIG_PATH).read_text(encoding="utf-8"))


def read_baseline(project_root: Path) -> dict[str, list[str]]:
    path = project_root / BASELINE_PATH
    if path.exists() is False:
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def write_baseline(project_root: Path, baseline: dict[str, list[str]]) -> None:
    data = {"_comment": "NamingWords 기준선 — 지금 남은 위반 이름(규칙마다). 줄기만 한다: 새 위반은 린트가 막고, 고친 이름은 --lower-baseline 으로 뺀다. 영역별 이전이 0 으로 줄인다."}
    for rule in RATCHET_RULES:
        data[rule] = sorted(set(baseline.get(rule, [])))
    (project_root / BASELINE_PATH).write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def classify_words(list_name: Iterable[tuple[str, str, str]], table: dict) -> dict[str, str]:
    """word 규칙 — 선언 이름 · 파일 · 폴더 이름의 **모든 낱말**이 낱말 표(`_words` 의 a 업계 · b 게임)에 있는가. 이름 → 경로 · 안내.
    `_scopedWords` 의 낱말은 그 경로 아래에서만 받는다(게임의 walk 는 맞고 엔진의 walk 는 순회다). 표준 합성어(`_keepTerms`) · 바깥 이름(`_externalPatterns`) · `_keep` 은 뺀다."""
    words = table["_words"]
    known = set(words["a"]) | set(words["b"])
    scoped = table["_scopedWords"]
    externals = [re.compile(pattern) for pattern in table["_externalPatterns"]]
    # 파이썬 snake_case 이름도 같은 합성어로 본다(`RequiredNameFields` 는 `required_name_fields` 도 덮는다)
    keep_terms = [re.compile(re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_?", re.escape(term)), re.I) for term in table.get("_keepTerms", [])]
    keep = table["_keep"]
    external_paths = table["_externalPaths"]
    found: dict[str, str] = {}
    for kind, name, path in list_name:
        if name in keep or name in found or is_external_name(name, externals) or is_under(path, external_paths):
            continue
        shape = PYTHON_SHAPES.get(kind) if path.endswith(".py") else None
        if shape is not None and shape[0].fullmatch(name) is None:
            found[name] = f"{path}: 파이썬 이름 '{name}' 가 PEP 8 꼴이 아니다 — {shape[1]}(docs/agents/NamingScripts.md). 바깥 덮어쓰기(setUp · do_POST)는 _keep"
            continue
        stripped = name
        for term in keep_terms:
            stripped = term.sub("_", stripped)
        for word in split_lower_words(stripped, kind):
            if word in known:
                continue
            if word in scoped:
                if is_under(path, scoped[word]["paths"]):
                    continue
                found[name] = f"{path}: '{name}' 의 낱말 '{word}' 는 {' · '.join(scoped[word]['paths'])} 에서만 쓴다 — {scoped[word]['hint']}"
            else:
                found[name] = (f"{path}: '{name}' 의 낱말 '{word}' 가 낱말 표에 없다 — 언리얼 · 유니티 · Godot · 일반 용어와 대조한 뒤 {CONFIG_PATH} 의 "
                               f"_words(a 업계 · b 게임)에 올리거나 업계 낱말로 바꾼다")
            break
    return found


def collect_violations(project_root: Path, scope: LintScope) -> tuple[list[str], dict[str, dict[str, str]]]:
    table = read_table(project_root)
    list_problem, ratchet = classify_names(read_sources(project_root, scope), table)
    ratchet["word"] = classify_words(collect_source_names(project_root, scope), table)
    return list_problem, ratchet


def check_naming_words(project_root: Path, scope: LintScope) -> list[str]:
    list_problem, ratchet = collect_violations(project_root, scope)
    return list_problem + compare_baseline(ratchet, read_baseline(project_root), scope.whole_repo)


def run_self_check() -> None:
    table = {"_functionWords": ["get", "has", "find", "draws", "material", "set", "focus", "starts"], "_typeSuffixes": ["Component", "Result"], "_banned": {"drain": "flush 또는 poll"},
             "_bannedTerms": {"Verdict": "Result", "Work": "Job"}, "_keepTerms": ["WorkArea"], "_predicateWords": ["is", "has", "uses", "starts"], "_lookupWords": ["get", "find"],
             "_keep": {"MailboxServiceResult": "이유"}}
    clean = 'struct FooComponent {}; int getValue( int x ); struct MailboxServiceResult {}; // drainEvents() hasWork\nconst char* s = "drainEvents() Verdict"; bool hasValue(); bool setAxisTags(); bool focusPanel(); bool startsWith(); RECT readWorkArea; int getSlotFor( int x );'
    problems, ratchet = classify_names([("a.h", clean)], table)
    assert problems == [] and all(not names for names in ratchet.values()), (problems, ratchet)
    bad = 'void drainEvents( int x ); struct FooVerdict {}; bool hasPendingWork(); bool drawsBatch(); int materialGroupFor( int x ); int _verdictCount;'
    problems, ratchet = classify_names([("a.h", bad)], table)
    assert len(problems) == 1 and "'Verdict'" in problems[0], problems   # 형식 끝 단어 Verdict 는 표에 없다
    assert set(ratchet["verb"]) == {"drainEvents"}, ratchet
    assert set(ratchet["term"]) == {"FooVerdict", "hasPendingWork", "_verdictCount"}, ratchet
    assert set(ratchet["predicate"]) == {"drawsBatch"}, ratchet
    assert set(ratchet["lookup"]) == {"materialGroupFor"}, ratchet
    baseline = {"verb": ["drainEvents"], "term": ["FooVerdict", "hasPendingWork", "_verdictCount", "goneName"], "predicate": ["drawsBatch"], "lookup": []}
    ratchet_problems = compare_baseline(ratchet, baseline)
    assert any("materialGroupFor" in line for line in ratchet_problems), ratchet_problems   # 기준선에 없는 새 위반
    assert any("goneName" in line for line in ratchet_problems), ratchet_problems   # 고쳐 사라진 이름은 빼라고 한다
    assert len(ratchet_problems) == 2, ratchet_problems
    table_names = dict(table, _bannedNames={"CombatHealth": "HealthStateMachine"})
    assert classify_names([("a.h", "// CombatHealth\nconst char* s = \"CombatHealth\";")], table_names)[0] == [], "주석 · 문자열의 옛 이름은 본다고 하지 않는다"
    assert len(classify_names([("a.h", "CombatHealth health; int getCombatHealthRatio();")], table_names)[0]) == 1, "통째 이름만 막는다"

    code = strip_code("#define SW_SCENE_WATCH 1\nenum class Mode { Idle, Walk };\nusing Alias = int;\nstruct SceneWalkKey {};\n"
                     "void hoverMark( int outbChanged, const Foo& inoutListCrumb ) { float _walkSpeed = 1; const int kMaxCount = 2; }")
    declared = set(collect_declarations(code))
    for item in (("macro", "SW_SCENE_WATCH"), ("enumerator", "Walk"), ("alias", "Alias"), ("type", "SceneWalkKey"), ("function", "hoverMark"),
                 ("variable", "outbChanged"), ("variable", "inoutListCrumb"), ("member", "_walkSpeed"), ("constant", "kMaxCount")):
        assert item in declared, (item, declared)
    assert split_lower_words("outbChanged", "variable") == ["changed"] and split_lower_words("inoutListCrumb", "variable") == ["crumb"]
    assert split_lower_words("D3D12Vector3D", "type") == ["vector"] and split_lower_words("setAxisTags", "function") == ["set", "axis", "tags"]
    assert collect_path_names("Source/Games/Walk/WalkPath.h") == [("folder", "Games"), ("folder", "Walk"), ("file", "WalkPath")]
    table_words = {"_words": {"a": ["scene", "key", "speed", "area", "mark", "hover", "rc", "at"], "b": ["ghost"]}, "_keep": {},
                  "_scopedWords": {"walk": {"paths": ["Source/Games"], "hint": "순회는 iterate"}}, "_externalPatterns": [r"^PFN_"], "_externalPaths": [".claude/skills/ponytail"], "_keepTerms": ["WorkArea"]}
    found = classify_words([("type", "SceneWalkKey", "Source/Engine/a.h"), ("member", "_walkSpeed", "Source/Games/b.h"), ("member", "_ghostKey", "Source/a.h"),
                           ("variable", "rcWorkArea", "Source/a.h"), ("alias", "PFN_vkWeirdo", "Source/a.h"), ("function", "aimAtMark", "Source/a.h"),
                           ("folder", "Walk", "Source/Games/Walk/x.h")], table_words)
    assert set(found) == {"SceneWalkKey", "aimAtMark"} and "iterate" in found["SceneWalkKey"] and "'aim'" in found["aimAtMark"], found
    script = "MAX_SCENE = 1\nkSceneKey = 2\nclass SceneKey:\n    def hover_mark(self) -> None: ...\n    def hoverScene(self) -> None: ...\n"
    found = classify_words([(kind, name, "Scripts/lint/scene_key.py") for kind, name in collect_script_declarations("Scripts/lint/scene_key.py", script)]
                           + [("file", "SceneKey", "Scripts/lint/SceneKey.py"), ("file", "SceneKey", "cmake/SceneKey.cmake")], table_words | {"_words": {"a": ["max", "scene", "key", "hover", "mark"], "b": []}})
    assert set(found) == {"kSceneKey", "hoverScene", "SceneKey"} and "Scripts/lint/SceneKey.py" in found["SceneKey"], found   # PEP 8 꼴 — CMake 파일 이름은 보지 않는다


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="이름을 업계 용어 표와 대조한다")
    parser.add_argument("--self-check", action="store_true")
    parser.add_argument("--write-baseline", action="store_true", help="지금 위반을 모두 기준선으로 쓴다(처음 한 번 — 새 규칙을 들일 때)")
    parser.add_argument("--lower-baseline", action="store_true", help="고쳐 사라진 이름을 기준선에서 뺀다(늘리지 않는다)")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        run_self_check()
        print("[NamingWords] self-check OK")
        return ExitCode.PASSED
    project_root = get_project_root()
    if arguments.write_baseline or arguments.lower_baseline:
        _, ratchet = collect_violations(project_root, make_whole_scope(project_root))
        current = {rule: list(ratchet[rule]) for rule in RATCHET_RULES}
        if arguments.lower_baseline:
            old = read_baseline(project_root)
            current = {rule: [name for name in old.get(rule, []) if name in ratchet[rule]] for rule in RATCHET_RULES}
        write_baseline(project_root, current)
        print(f"[NamingWords] 기준선: " + ", ".join(f"{rule} {len(current[rule])}" for rule in RATCHET_RULES))
        return ExitCode.PASSED
    list_problem = check_naming_words(project_root, make_whole_scope(project_root))
    for problem in list_problem:
        print(f"  - {problem}")
    print(f"[NamingWords] {'FAIL' if list_problem else 'OK'} ({len(list_problem)}건)")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
