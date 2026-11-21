"""
Scripts/dev/disk_path_join.py

경로를 글로 잇고 다시 디스크 경로 형식으로 올리는 왕복(`FileUtil::toDiskPath( FileUtil::joinPath( 형식, … ) )`)을 형식 조립(`AbsolutePath::join` ·
`getParent`)으로 바꾼다. 형식 → 글 → 형식 복사 · 검사를 덜고, 형식 조립은 `..` 로 이 경로 위에 오르지 못한다(`Core/Path/AbsolutePath.h`).

  py -3 -m Scripts disk-path-join [경로…]          # 바꾼다(경로 없으면 Source)
  py -3 -m Scripts disk-path-join --check [경로…]  # 형식으로 바꿀 수 있는 남은 감싸기를 찍고, 있으면 실패
  py -3 -m Scripts disk-path-join --self-check     # 변환 자체 테스트

- 형식인 식: 가장 가까운 선언이 `AbsolutePath` 인 이름 · `*이름`(optional<AbsolutePath>) · `ProcessRoots` 칸 · 형식을 돌려주는 만드는 쪽(`PRODUCERS` —
  `test::makeScratchPath` · `ScopedScratchDomain::makeDiskPath` …) · 형식 식의 `.join( … )` · `.getParent()`. 그 `.str()` 은 형식으로 되돌린다.
- 바꾸는 자리: `toDiskPath( 형식 )` → `형식`, `toDiskPath( FileUtil::joinPath( 형식, 상대 ) )` → `형식.join( 상대 )`(겹친 joinPath 도),
  `toDiskPath( FileUtil::getDirectoryPart( 형식 ) )` → `형식.getParent()`. 상대가 `..` 를 담은 글 상수면 두고 알린다(`join` 은 위로 오르면 빈 경로다).
- 함수 안의 `const string 이름 = <형식으로 만들 수 있는 식>;` 은 `const AbsolutePath 이름 = …;` 로 바꾸고, 그 블록 안의 다른 쓰임은 `이름.str()` 로 적어
  뜻을 지킨다(감싸기 안의 쓰임은 다음 바퀴에 풀린다). 같은 블록에서 이름을 다시 선언하면 바꾸지 않는다.
- 형식 문맥 밖의 만드는 쪽 부르기(글이 필요한 인자 · `return` · 고칠 수 없는 선언)는 `.str()` 을 붙인다 — 만드는 쪽의 반환 형식을 바꾼 뒤 컴파일되게 한다.
- 병합 뒤 충돌 파일에 다시 돌린다(이미 바꾼 자리는 그대로). 남는 감싸기는 진짜 문자열 입구(설정 · 명령줄 · 매개변수 글)다.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.git import run_git_or_raise  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from dev.byte_reader_values import find_declaration, find_function_body, last_name, match_paren, split_top  # noqa: E402
from dev.disk_path_args import IO_PATH_ARGS, ROOT_FIELD  # noqa: E402
from lint.cpp_shape import strip_code  # noqa: E402

TAG = "[disk-path-join]"
SKIP_PARTS = ("/ThirdParty/", "/Generated/", ".gen.")
CPP_SUFFIX = (".h", ".cpp", ".inl")
EXCLUDED_FILES = ("Source/Core/File/FileUtil.h", "Source/Core/File/FileUtil.cpp", "Source/Core/Path/AbsolutePath.h", "Source/Core/Path/AbsolutePath.cpp")

QUALIFIER = r"(?:(?:::\s*)?(?:sw\s*::\s*)?FileUtil\s*::\s*)"
WRAP_CALL = re.compile(r"(?<![\w.>])" + QUALIFIER + r"?toDiskPath\s*\(")
JOIN_CALL = re.compile(r"\s*" + QUALIFIER + r"joinPath\s*\(")
PARENT_CALL = re.compile(r"\s*" + QUALIFIER + r"getDirectoryPart\s*\(")
FREE_PRODUCERS = ("makeScratchPath",)   # `test::` 의 형식을 돌려주는 자유 함수
MEMBER_PRODUCERS = {"makeDiskPath": ("ScopedScratchDomain", "EditorScratchDomain"), "makeDiskFolder": ("ScopedScratchDomain",)}
FREE_PRODUCER_CALL = re.compile(r"(?<![\w.>])(?:(?:::\s*)?(?:sw\s*::\s*)?test\s*::\s*)?(" + "|".join(FREE_PRODUCERS) + r")\s*\(")
MEMBER_PRODUCER_CALL = re.compile(r"\b(\w+)\s*\.\s*(" + "|".join(MEMBER_PRODUCERS) + r")\s*\(")
TYPED_MEMBERS = ("str", "c_str", "empty", "join", "getParent")   # 형식에도 있는 이름 — `.str()` 을 끼우지 않는다
DECLARATION = re.compile(r"\bconst\s+(sw\s*::\s*)?(?:string|auto)\s*&?\s*(\w+)\s*=\s*")
TYPE_WORD_BEFORE = re.compile(r"(?:[A-Za-z_][\w:]*(?:<[^<>;]*>)?)\s*[&*]?\s*$")
NOT_TYPE_WORDS = ("return", "else", "case", "co_return", "throw", "delete", "new")


def find_close(code: str, open_index: int) -> int:
    return match_paren(code, open_index)


def strip_str(text: str) -> str | None:
    """`X.str()` 이면 X, 아니면 None."""
    match = re.fullmatch(r"(.+?)\s*\.\s*str\s*\(\s*\)", text, re.S)
    return match.group(1).strip() if match else None


def is_producer_call(code: str, text: str, position: int) -> bool:
    """`text` 가 통째로 형식을 돌려주는 만드는 쪽 부르기인가."""
    match = FREE_PRODUCER_CALL.match(text)
    if match is None:
        match = MEMBER_PRODUCER_CALL.match(text)
        if match is None:
            return False
        declared = find_declaration(code, match.group(1), position)
        if declared is None or last_name(declared[0]) not in MEMBER_PRODUCERS[match.group(2)]:
            return False
    open_index = match.end() - 1
    return match_paren(text, open_index) == len(text) - 1


def typed_base(code: str, text: str, position: int, depth: int = 0) -> str | None:
    """`text` 가 형식이면 그 형식 식 글(`.str()` 은 뗀다), 아니면 None. `position` 은 선언을 찾을 위치."""
    text = text.strip()
    if depth > 8 or not text:
        return None
    inner = strip_str(text)
    if inner is not None:
        return typed_base(code, inner, position, depth + 1)
    if ROOT_FIELD.search(text) and re.fullmatch(r"[\w:().\s]+", text):
        return text
    if re.fullmatch(r"\*?\s*\w+", text):
        dereferenced = text.startswith("*")
        name = text.lstrip("*").strip()
        declared = find_declaration(code, name, position)
        if declared is None or "AbsolutePath" not in declared[0]:
            return None
        return text if ("optional" in declared[0]) == dereferenced else None
    if is_producer_call(code, text, position):
        return text
    member = re.fullmatch(r"(.+?)\s*\.\s*(join\s*\(.*\)|getParent\s*\(\s*\))", text, re.S)
    if member is not None and typed_base(code, member.group(1), position, depth + 1) == member.group(1).strip():
        return text
    return convert_path_call(code, text, position, depth + 1)


def convert_path_call(code: str, text: str, position: int, depth: int = 0) -> str | None:
    """`FileUtil::joinPath( 형식, 상대 )` → `형식.join( 상대 )`, `FileUtil::getDirectoryPart( 형식 )` → `형식.getParent()`. 바꿀 수 없으면 None."""
    for pattern, kind in ((JOIN_CALL, "join"), (PARENT_CALL, "parent")):
        match = pattern.match(text)
        if match is None:
            continue
        open_index = match.end() - 1
        close = match_paren(text, open_index)
        if close != len(text.rstrip()) - 1:
            return None
        pieces = [text[a:b].strip() for a, b in split_top(text, open_index + 1, close, (",",))]
        if kind == "join" and len(pieces) == 2:
            base = typed_base(code, pieces[0], position, depth + 1)
            if base is None or re.search(r'"[^"]*\.\.[^"]*"', pieces[1]):
                return None   # 글 상수의 `..` — join 은 위로 오르면 빈 경로라 뜻이 바뀐다
            return f"{base}.join( {pieces[1]} )"
        if kind == "parent" and len(pieces) == 1:
            base = typed_base(code, pieces[0], position, depth + 1)
            return f"{base}.getParent()" if base is not None else None
        return None
    return None


@dataclass(frozen=True)
class Change:
    start: int
    end: int
    text: str


def rewrite_wrapped_calls(raw: str, code: str) -> list[Change]:
    """`toDiskPath( 형식 )` · `toDiskPath( joinPath( 형식, … ) )` · `toDiskPath( getDirectoryPart( 형식 ) )` 바꿈들(안쪽 것 먼저 — 겹치면 바깥 것은 다음 바퀴)."""
    list_change: list[Change] = []
    for match in WRAP_CALL.finditer(code):
        open_index = match.end() - 1
        close = match_paren(code, open_index)
        if close < 0:
            continue
        argument = code[open_index + 1:close].strip()
        if WRAP_CALL.search(argument):
            continue   # 안쪽 감싸기부터
        argument_raw = raw[open_index + 1:close].strip()
        base = typed_base(code, argument, match.start())
        if base is None:
            continue
        # 글 상수가 섞인 자리는 원문(주석 · 글 포함)에서 같은 변환을 다시 만든다
        text = typed_base(raw, argument_raw, match.start()) if argument_raw != argument else base
        if text is None:
            continue
        if any(change.start < close and match.start() < change.end for change in list_change):
            continue
        list_change.append(Change(match.start(), close + 1, text))
    return list_change


def find_block_end(code: str, position: int) -> int:
    """`position` 을 담은 블록의 닫는 `}` 위치."""
    depth = 0
    for index in range(position, len(code)):
        char = code[index]
        if char == "{":
            depth += 1
        elif char == "}":
            if depth == 0:
                return index
            depth -= 1
    return len(code)


def is_capture_use(code: str, index: int) -> bool:
    """람다 붙잡기 목록(`[root]` · `[&root]`) 안의 이름인가."""
    depth = 0
    for cursor in range(index - 1, max(index - 200, -1), -1):
        char = code[cursor]
        if char in ")]}":
            depth += 1
        elif char in "([{":
            if depth == 0:
                if char != "[":
                    return False
                close = match_paren(code, cursor)
                return close > 0 and re.match(r"\s*(?:\(|\{|mutable\b)", code[close + 1:]) is not None
            depth -= 1
        elif char == ";" and depth == 0:
            return False
    return False


def rewrite_declarations(raw: str, code: str) -> str | None:
    """`const string 이름 = 형식으로 만들 수 있는 식;` 하나를 형식 선언으로 바꾼 글. 없으면 None."""
    for match in DECLARATION.finditer(code):
        body_start, _ = find_function_body(code, match.start())
        if body_start < 0:
            continue
        name = match.group(2)
        qualifier = "sw::" if match.group(1) else ""   # `sw::string` 을 쓰던 이름공간 밖 파일은 `sw::AbsolutePath`
        init_start = match.end()
        init_end = init_start
        depth = 0
        while init_end < len(code):
            char = code[init_end]
            if char in "([{":
                depth += 1
            elif char in ")]}":
                depth -= 1
                if depth < 0:
                    break
            elif char == ";" and depth == 0:
                break
            init_end += 1
        if init_end >= len(code) or code[init_end] != ";":
            continue
        init_code = code[init_start:init_end].strip()
        init_raw = raw[init_start:init_end].strip()
        base = typed_base(code, init_code, match.start())
        if base is None:
            continue
        new_init = typed_base(raw, init_raw, match.start()) if init_raw != init_code else base
        if new_init is None:
            continue
        scope_end = find_block_end(code, init_end)
        uses: list[int] = []
        redeclared = False
        for use in re.finditer(r"(?<![\w.>:])" + re.escape(name) + r"\b", code[init_end:scope_end]):
            index = init_end + use.start()
            before = code[max(0, index - 300):index].rstrip()
            if TYPE_WORD_BEFORE.search(before) and re.search(r"(\w+)\s*[&*]?\s*$", before).group(1) not in NOT_TYPE_WORDS and not before.endswith((",", "(")):
                redeclared = True   # 같은 이름의 새 선언(가림) — 바꾸지 않는다
                break
            uses.append(index)
        if redeclared:
            continue
        edits: list[tuple[int, int, str]] = []
        for index in uses:
            after = code[index + len(name):]
            member = re.match(r"\s*\.\s*(\w+)", after)
            if member is not None and member.group(1) in TYPED_MEMBERS:
                continue
            if is_capture_use(code, index):
                continue
            wrap_open = re.search(r"(?<![\w.>])" + QUALIFIER + r"?toDiskPath\s*\(\s*$", code[max(0, index - 300):index])
            if wrap_open is not None and re.match(r"\s*\)", after):
                continue   # 다음 바퀴에 감싸기째 풀린다
            edits.append((index + len(name), index + len(name), ".str()"))
        type_start = match.start()
        edits.append((type_start, init_end, f"const {qualifier}AbsolutePath {name} = {new_init}"))
        for start, end, text in sorted(edits, key=lambda edit: edit[0], reverse=True):
            raw = raw[:start] + text + raw[end:]
        return raw
    return None


def get_enclosing_call(code: str, index: int) -> tuple[str, int] | None:
    """`index` 가 어떤 부르기의 인자 하나 통째의 시작이면 (부른 이름 글 — 한정 포함, 인자 번호), 아니면 None."""
    depth = 0
    argument_index = 0
    for cursor in range(index - 1, -1, -1):
        char = code[cursor]
        if char in ")]}":
            depth += 1
        elif char in "[{":
            if depth == 0:
                return None
            depth -= 1
        elif char == "(":
            if depth > 0:
                depth -= 1
                continue
            name = re.search(r"((?:[A-Za-z_]\w*\s*::\s*)*[A-Za-z_]\w*)\s*$", code[max(0, cursor - 300):cursor])
            return (re.sub(r"\s+", "", name.group(1)), argument_index) if name else None
        elif char == "," and depth == 0:
            argument_index += 1
        elif char == ";" and depth == 0:
            return None
    return None


def is_io_argument(code: str, start: int, end: int) -> bool:
    """`[start, end)` 가 FileUtil 디스크 I/O 의 경로 인자 자리(형식 문맥)인가."""
    enclosing = get_enclosing_call(code, start)
    if enclosing is None:
        return False
    name, argument_index = enclosing
    function = name.split("::")[-1]
    if "FileUtil" not in name or function not in IO_PATH_ARGS or argument_index not in IO_PATH_ARGS[function]:
        return False
    return re.match(r"\s*[,)]", code[end:]) is not None


def is_string_context(code: str, start: int, end: int, before: str) -> bool:
    """글이 틀림없이 필요한 자리 — 고칠 수 없는 글 선언의 초기값 · FileUtil 글 함수의 인자(`joinPath` …)."""
    if re.search(r"(?<!const )\b(?:sw\s*::\s*)?string\s*&?\s*\w+\s*(?:=|\{)\s*$", before) is not None:
        return True
    enclosing = get_enclosing_call(code, start)
    if enclosing is None or re.match(r"\s*[,)]", code[end:]) is None:
        return False
    name, _ = enclosing
    function = name.split("::")[-1]
    return "FileUtil" in name and function not in IO_PATH_ARGS and function != "toDiskPath"


def append_producer_str(raw: str, code: str, final: bool, migrate: bool) -> str:
    """형식 문맥 밖의 만드는 쪽 부르기에 `.str()` 을 붙인다(만드는 쪽의 반환 형식을 바꾼 뒤 컴파일되게).
    첫 바퀴(`final` 거짓)는 감싸기 인자 · 글 선언의 초기값을 두고(다음 바퀴가 형식으로 바꾼다), 끝 바퀴는 형식으로 바꾸지 못한 글 선언의 초기값만 본다.
    `migrate` 는 반환 형식을 바꾸는 한 번의 이전이다 — 형식 문맥(감싸기 · 형식 선언 · I/O 인자 · 형식 멤버) 밖은 모두 붙인다. 다시 돌릴 때는 글이 틀림없는
    자리만 붙인다(감싸기를 푼 형식 인자 — `save( 형식 )` — 를 글 자리로 잘못 보지 않게)."""
    edits: list[int] = []
    for pattern in (FREE_PRODUCER_CALL, MEMBER_PRODUCER_CALL):
        for match in pattern.finditer(code):
            if pattern is MEMBER_PRODUCER_CALL:
                declared = find_declaration(code, match.group(1), match.start())
                if declared is None or last_name(declared[0]) not in MEMBER_PRODUCERS[match.group(2)]:
                    continue
            before = code[max(0, match.start() - 300):match.start()].rstrip()
            word = re.search(r"(\w+)\s*[&*]?\s*$", before)
            if word is not None and TYPE_WORD_BEFORE.search(before) and word.group(1) not in NOT_TYPE_WORDS and not before.endswith((",", "(", "=", "?", ":")):
                continue   # 선언 · 정의 머리
            close = match_paren(code, match.end() - 1)
            if close < 0:
                continue
            after = code[close + 1:]
            member = re.match(r"\s*\.\s*(\w+)", after)
            if member is not None and member.group(1) in TYPED_MEMBERS:
                continue
            if re.search(r"\bconst\s+AbsolutePath\s+\w+\s*=\s*$", before) or re.search(r"\bAbsolutePath\s+\w+\s*[={(]\s*$", before):
                continue
            is_wrapped = re.search(r"(?<![\w.>])" + QUALIFIER + r"?toDiskPath\s*\(\s*$", before) is not None and re.match(r"\s*\)", after) is not None
            is_declaration_init = re.search(r"\bconst\s+(?:sw\s*::\s*)?(?:string|auto)\s*&?\s*\w+\s*=\s*$", before) is not None and re.match(r"\s*;", after) is not None
            if is_wrapped or (is_declaration_init is False if final else is_declaration_init):
                continue
            if final is False and (is_io_argument(code, match.start(), close + 1) or (migrate is False and not is_string_context(code, match.start(), close + 1, before))):
                continue
            edits.append(close + 1)
    for index in sorted(set(edits), reverse=True):
        raw = raw[:index] + ".str()" + raw[index:]
    return raw


def rewrite_text(raw: str, migrate: bool = False) -> tuple[str, int]:
    """바꾼 글과 줄인 감싸기 수. 감싸기 · 선언을 더 바뀌지 않을 때까지 번갈아 바꾸고 끝에 만드는 쪽 부르기에 `.str()` 을 붙인다."""
    before = len(WRAP_CALL.findall(strip_code(raw)))
    raw = append_producer_str(raw, strip_code(raw), False, migrate)
    for _ in range(2000):
        code = strip_code(raw)
        changes = rewrite_wrapped_calls(raw, code)
        if changes:
            for change in sorted(changes, key=lambda c: c.start, reverse=True):
                raw = raw[:change.start] + change.text + raw[change.end:]
            continue
        rewritten = rewrite_declarations(raw, code)
        if rewritten is None:
            break
        raw = rewritten
    raw = append_producer_str(raw, strip_code(raw), True, migrate)
    return raw, before - len(WRAP_CALL.findall(strip_code(raw)))


def find_left(raw: str) -> list[int]:
    """`--check`: 형식으로 바꿀 수 있는 남은 감싸기의 줄."""
    code = strip_code(raw)
    return [raw.count("\n", 0, change.start) + 1 for change in rewrite_wrapped_calls(raw, code)]


def is_target(relative: str) -> bool:
    return (relative.startswith("Source/") and relative.endswith(CPP_SUFFIX) and relative not in EXCLUDED_FILES
            and not any(part in relative for part in SKIP_PARTS))


def list_files(project_root: Path, list_path: Sequence[str]) -> list[Path]:
    output = run_git_or_raise(["ls-files", "--", *(list_path or ["Source"])], cwd=project_root)
    return [project_root / relative for relative in output.splitlines() if is_target(relative)]


SELF_CHECK_CASES: dict[str, str] = {
    # 형식 이름 · ProcessRoots 칸 · .str() 을 형식 조립으로
    'void f( const AbsolutePath& root )\n{\n    ok = FileUtil::exists( FileUtil::toDiskPath( FileUtil::joinPath( root, "a/b" ) ) );\n}\n':
        'void f( const AbsolutePath& root )\n{\n    ok = FileUtil::exists( root.join( "a/b" ) );\n}\n',
    'void f()\n{\n    ok = FileUtil::exists( FileUtil::toDiskPath( FileUtil::joinPath( ProcessRoots::get()._project.str(), "x" ) ) );\n}\n':
        'void f()\n{\n    ok = FileUtil::exists( ProcessRoots::get()._project.join( "x" ) );\n}\n',
    'void f( const AbsolutePath& root )\n{\n    ok = FileUtil::isDirectory( FileUtil::toDiskPath( FileUtil::getDirectoryPart( FileUtil::joinPath( root.str(), "a" ) ) ) );\n}\n':
        'void f( const AbsolutePath& root )\n{\n    ok = FileUtil::isDirectory( root.join( "a" ).getParent() );\n}\n',
    # 글 매개변수는 진짜 문자열 입구다 — 그대로
    'void f( const string& root )\n{\n    ok = FileUtil::exists( FileUtil::toDiskPath( FileUtil::joinPath( root, "a" ) ) );\n}\n':
        'void f( const string& root )\n{\n    ok = FileUtil::exists( FileUtil::toDiskPath( FileUtil::joinPath( root, "a" ) ) );\n}\n',
    # 글 상수의 `..` 는 join 이 거절하므로 두고 알린다
    'void f( const AbsolutePath& root )\n{\n    ok = FileUtil::exists( FileUtil::toDiskPath( FileUtil::joinPath( root, "../x" ) ) );\n}\n':
        'void f( const AbsolutePath& root )\n{\n    ok = FileUtil::exists( FileUtil::toDiskPath( FileUtil::joinPath( root, "../x" ) ) );\n}\n',
    # 만드는 쪽에서 선언까지 형식이 퍼지고, 다른 쓰임은 .str() 로 뜻을 지킨다
    'void f()\n{\n    const string root = test::makeScratchPath( "case" );\n    const string file = FileUtil::joinPath( root, "a.txt" );\n'
    '    ok = FileUtil::writeTextFile( FileUtil::toDiskPath( file ), "x" ) && check( file ) && FileUtil::exists( FileUtil::toDiskPath( root ) );\n'
    '    auto fn = [root]() { return root.size(); };\n}\n':
        'void f()\n{\n    const AbsolutePath root = test::makeScratchPath( "case" );\n    const AbsolutePath file = root.join( "a.txt" );\n'
        '    ok = FileUtil::writeTextFile( file, "x" ) && check( file.str() ) && FileUtil::exists( root );\n'
        '    auto fn = [root]() { return root.str().size(); };\n}\n',
    # 형식 문맥 밖의 만드는 쪽 · 고칠 수 없는 선언은 .str()
    'void f( ScopedScratchDomain& domain )\n{\n    string path = test::makeScratchPath( "a" );\n    use( domain.makeDiskPath( "b" ) );\n'
    '    ok = FileUtil::exists( FileUtil::toDiskPath( domain.makeDiskPath( "c" ) ) );\n}\nstring g() { return makeScratchPath( "z" ); }\n':
        'void f( ScopedScratchDomain& domain )\n{\n    string path = test::makeScratchPath( "a" ).str();\n    use( domain.makeDiskPath( "b" ).str() );\n'
        '    ok = FileUtil::exists( domain.makeDiskPath( "c" ) );\n}\nstring g() { return makeScratchPath( "z" ).str(); }\n',
    # 다른 형식의 같은 이름 · 선언 머리는 그대로
    'void f( Index& index )\n{\n    use( index.makeDiskPath( "b" ) );\n}\nSW_API AbsolutePath makeScratchPath( string_view name );\n':
        'void f( Index& index )\n{\n    use( index.makeDiskPath( "b" ) );\n}\nSW_API AbsolutePath makeScratchPath( string_view name );\n',
    # 이름공간 밖 파일의 `sw::string` 은 `sw::AbsolutePath`
    'void f()\n{\n    const sw::string root = sw::test::makeScratchPath( "a" );\n    ok = sw::FileUtil::exists( sw::FileUtil::toDiskPath( root ) );\n}\n':
        'void f()\n{\n    const sw::AbsolutePath root = sw::test::makeScratchPath( "a" );\n    ok = sw::FileUtil::exists( root );\n}\n',
    # 같은 블록에서 다시 선언(가림)하면 바꾸지 않는다
    'void f()\n{\n    const string root = test::makeScratchPath( "a" );\n    { string root = "x"; use( root ); }\n}\n':
        'void f()\n{\n    const string root = test::makeScratchPath( "a" ).str();\n    { string root = "x"; use( root ); }\n}\n',
}


SAFE_CASES: dict[str, str] = {
    # 다시 돌릴 때(이전 아님)는 글이 틀림없는 자리만 .str() — 감싸기를 푼 형식 인자(`save( 형식 )`)는 그대로
    'void f()\n{\n    string path = test::makeScratchPath( "a" );\n    save( makeScratchPath( "b" ) );\n'
    '    const string p = FileUtil::normalizeSeparators( makeScratchPath( "c" ) );\n}\n':
        'void f()\n{\n    string path = test::makeScratchPath( "a" ).str();\n    save( makeScratchPath( "b" ) );\n'
        '    const string p = FileUtil::normalizeSeparators( makeScratchPath( "c" ).str() );\n}\n',
}


def run_self_check() -> int:
    failures = 0
    for cases, migrate in ((SELF_CHECK_CASES, True), (SAFE_CASES, False)):
        for source, want in cases.items():
            got, _ = rewrite_text(source, migrate)
            again, _ = rewrite_text(got)
            if got != want or again != got:
                print(f"{TAG} self-check (migrate={migrate}):\n{source}\n  got\n{got}\n  want\n{want}\n  again\n{again}")
                failures += 1
    print(f"{TAG} self-check {'OK' if failures == 0 else 'FAIL'}")
    return ExitCode.PASSED if failures == 0 else ExitCode.VIOLATION


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts disk-path-join", description="toDiskPath( joinPath( 형식, … ) ) 왕복을 AbsolutePath 조립으로 바꾼다")
    parser.add_argument("paths", nargs="*", help="볼 경로(저장소 기준, 없으면 Source)")
    parser.add_argument("--check", action="store_true", help="바꾸지 않고 형식으로 바꿀 수 있는 남은 감싸기를 찍는다 — 있으면 실패")
    parser.add_argument("--self-check", action="store_true", help="변환 자체 테스트")
    parser.add_argument("--migrate-producers", action="store_true",
                        help="만드는 쪽(PRODUCERS)의 반환을 형식으로 바꾸는 한 번의 이전 — 형식 문맥 밖의 부르기에 모두 .str() 을 붙인다(다시 돌릴 때는 빼고 글이 틀림없는 자리만)")
    args = parser.parse_args(argv)
    if args.self_check:
        return run_self_check()
    project_root = get_project_root()
    list_problem: list[str] = []
    changed_files = 0
    total = 0
    for path in list_files(project_root, args.paths):
        with path.open(encoding="utf-8", newline="") as file:
            raw = file.read()
        has_producer = FREE_PRODUCER_CALL.search(raw) is not None or MEMBER_PRODUCER_CALL.search(raw) is not None
        if "toDiskPath" not in raw and not has_producer:
            continue
        relative = path.relative_to(project_root).as_posix()
        if args.check:
            list_problem += [f"{relative}:{line}: 형식 조립으로 바꿀 수 있는 감싸기 — `py -3 -m Scripts disk-path-join {relative}`" for line in find_left(raw)]
            continue
        new_raw, done = rewrite_text(raw, args.migrate_producers)
        total += done
        if new_raw != raw:
            with path.open("w", encoding="utf-8", newline="") as file:
                file.write(new_raw)
            changed_files += 1
    for line in list_problem:
        print(line)
    if args.check:
        print(f"{TAG} 남은 자리 {len(list_problem)}")
    else:
        print(f"{TAG} 바꾼 파일 {changed_files} · 줄인 감싸기 {total}")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
