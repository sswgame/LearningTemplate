#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/include_order.py

include 순서 규칙과 그 검사. 규칙은 `inspect_text`(판정 + 고친 글)와 `fix_text`(고침)이고, 검사(`check_include_order`)와 `format.py` 의 자동 수정이
같은 판정을 부른다. 검사는 보고만 한다 — 고치는 것은 `format.py`.

규칙:
1. .cpp 파일의 첫 번째 include는 무조건 "pch.h" 이어야 합니다.
2. 프로젝트 헤더 ("...") 인클루드가 시스템/외부 헤더 (<...>) 인클루드보다 먼저 와야 합니다.

헤더 조회표는 훑기 전에 한 번 만들어 자식 프로세스로 같이 보낸다.

  python Scripts/lint/include_order.py [--root <repo>] [--files <path> ...]    (= `py -3 -m Scripts lint include-order`)

종료 코드: 0 통과(또는 `--files` 로 좁혀 볼 파일 없음), 1 위반, 2 읽을 수 없는 파일.
"""

from __future__ import annotations

import argparse
import functools
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.exit_code import ExitCode  # noqa: E402
from common.file_collect import collect_repository_files, resolve_file_arguments  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from common.project_constants import CPP_ALL_EXTENSIONS, CPP_SOURCE_EXTENSIONS, GENERATED_REL_DIRS, LINT_TARGET_BASE_DIR_NAMES, LINT_TARGET_REL_DIRS, NOT_OUR_DIR_NAMES  # noqa: E402
from lint.lint_scope import LintScope, list_repo_files, read_bytes  # noqa: E402
from lint.parallel import flat_map_in_processes  # noqa: E402

CHECK_TAG = "CheckIncludeOrder"

#: 위반 안내가 시키는 고치는 명령 — `Scripts/__main__.py` 의 `format`(`format.main_format`)이다. 자체 검사가 이 명령이 실제로 include 를 고치는지 본다.
FIX_COMMAND = "py -3 -m Scripts format --files"

_INCLUDE_RE = re.compile(r'^\s*#\s*include\s+([<"])([^>"]+)[>"]', re.MULTILINE)


def _build_header_lookup(repository_root: Path, base_name: str) -> dict[str, str]:
    """
    `baseName`(Source · Test · Tools) 아래 헤더를 이름 · 소문자 상대 경로로 찾는 표입니다. 이름이 겹치는 헤더(pch.h 등)는 모호하므로 뺍니다.

    폴더를 걷지 않고 git 이 아는 파일 목록(`lint_scope.list_repo_files`, 한 번 읽어 모든 규칙이 함께 쓴다)에서 고른다 — 커밋 훅이 파일 셋만 볼 때도
    저장소를 걷지 않는다. 내려받은 외부 도구 · 생성물 폴더는 뺀다(`collect_repository_files` 와 같은 규칙). 이 표는 따옴표 include 의
    경로 복원에만 쓰이고 외부 헤더는 꺾쇠로 include 하므로 결과는 같습니다.
    """
    head = base_name + "/"
    lookup_map: dict[str, str] = {}
    name_counts: dict[str, int] = {}
    for path in list_repo_files(repository_root.resolve()):
        if not path.startswith(head) or not path.lower().endswith(".h") or any(part in NOT_OUR_DIR_NAMES for part in path.split("/")[:-1])                 or any(path.startswith(generated + "/") for generated in GENERATED_REL_DIRS):
            continue
        rel = path[len(head):]
        name = rel.rpartition("/")[2]
        name_counts[name] = name_counts.get(name, 0) + 1
        lookup_map[name] = rel
        lookup_map[rel.lower()] = rel
        lookup_map[name.lower()] = rel
    # Remove ambiguous duplicates (like pch.h)
    return {k: v for k, v in lookup_map.items() if not (k in name_counts and name_counts[k] > 1)}


def build_header_lookup_map(repository_root: Path) -> tuple[dict[str, str], dict[str, str], dict[str, str]]:
    # 뿌리는 Source 하나다(프로그램도 Source/Programs) — 옛 Test · Tools 표 자리는 비운다.
    return (_build_header_lookup(repository_root, "Source"), {}, {})


_CONDITIONAL_OPEN_RE = re.compile(r'^\s*#\s*(?:if|ifdef|ifndef)\b(.*)$')
_CONDITIONAL_BRANCH_RE = re.compile(r'^\s*#\s*(?:elif|else)\b(.*)$')
_CONDITIONAL_END_RE = re.compile(r'^\s*#\s*endif\b')
_CONDITION_MACRO_RE = re.compile(r'\b[A-Za-z_]\w*\b')
_NOT_CONDITION_MACRO = frozenset({"defined", "__has_include"})


def _condition_family(macro_name: str) -> str:
    """플랫폼 매크로는 하나의 사슬(`#if WINDOWS / #elif LINUX`)로 다루므로 같은 계열로 묶는다."""
    if macro_name.startswith("SW_PLATFORM_"):
        return "SW_PLATFORM_*"
    return macro_name


def find_split_conditional_includes(lines: list[str], relative_file_path: str) -> list[str]:
    """
    include 를 담은 최상위 `#if` 블록 둘이 같은 조건 계열을 물으면 위반이다.
    순서 검사(위)는 첫 `#if` 에서 멈추므로 조건부 include 는 그 밖에 있다 — 같은 조건으로 프로젝트 헤더 블록과
    시스템 헤더 블록을 따로 여는 일이 그래서 생겼다. 한 사슬의 갈래 안에 프로젝트 헤더, 빈 줄, 시스템 헤더 순으로 둔다.
    """
    list_block: list[tuple[int, set[str]]] = []
    list_open: list[list] = []   # [시작 줄, 계열 집합, include 포함 여부]
    for line_index, line in enumerate(lines):
        open_match = _CONDITIONAL_OPEN_RE.match(line)
        if open_match:
            list_open.append([line_index + 1, set(), False])
            condition = open_match.group(1).split("//")[0]
        else:
            branch_match = _CONDITIONAL_BRANCH_RE.match(line)
            if branch_match and list_open:
                condition = branch_match.group(1).split("//")[0]
            elif _CONDITIONAL_END_RE.match(line) and list_open:
                block = list_open.pop()
                if not list_open and block[2] and block[1]:
                    list_block.append((block[0], block[1]))
                continue
            else:
                if _INCLUDE_RE.match(line):
                    for block in list_open:
                        block[2] = True
                continue
        for macro_name in _CONDITION_MACRO_RE.findall(condition):
            if macro_name not in _NOT_CONDITION_MACRO and not macro_name.isdigit():
                list_open[-1][1].add(_condition_family(macro_name))

    violations_list = []
    map_first_line: dict[str, int] = {}
    for start_line, families in list_block:
        for family in sorted(families):
            if family in map_first_line:
                violations_list.append(f"{relative_file_path}:{start_line}: {family} 로 include 를 고르는 #if 블록이 {map_first_line[family]} 줄에도 있습니다 — "
                                      f"한 사슬로 합치고 갈래 안에 프로젝트 헤더 → 시스템 헤더 순으로 두십시오")
            else:
                map_first_line[family] = start_line
    return violations_list


def _compute_include_order(text: str, relative_file_path: str,
                                source_header_map: dict[str, str] | None,
                                test_header_map: dict[str, str] | None,
                                tools_header_map: dict[str, str] | None) -> tuple[str, list[str]]:
    """규칙대로 정리한 글과, 정리로 풀 수 없는 위반(같은 조건의 include 블록 둘 등). 글은 줄끝 `\n` 으로 받는다."""
    violations_list = []
    is_cpp = Path(relative_file_path).suffix.lower() in CPP_SOURCE_EXTENSIONS
    
    lines = text.split('\n')
    violations_list.extend(find_split_conditional_includes(lines, relative_file_path))
    in_if_directive_level = 0
    boundary_index = -1
    
    # 1. 상단 인클루드 영역의 경계선(boundary) 찾기
    # 첫 번째 #if 나 매크로 정의, namespace, class 등이 나타나는 곳을 경계로 삼음
    for line_index, line in enumerate(lines):
        stripped = line.strip().lstrip('\ufeff')
        if stripped.startswith('#if'):
            if boundary_index == -1:
                boundary_index = line_index
            in_if_directive_level += 1
        elif stripped.startswith('#endif'):
            in_if_directive_level = max(0, in_if_directive_level - 1)
        elif in_if_directive_level == 0:
            if stripped and not stripped.startswith('//') and not stripped.startswith('/*') and not stripped.startswith('*') and not stripped.startswith('#include') and not stripped.startswith('#pragma'):
                if boundary_index == -1:
                    boundary_index = line_index

    if boundary_index == -1:
        boundary_index = len(lines)

    # 경계 줄에 빈 줄 없이 붙은 주석(doc 블록 · 구획 주석)은 그 코드의 것이다 — 아래 본문으로 넘긴다. 상단 영역에 두면 "include 영역과
    # 본문 사이 한 줄" 이 주석과 그것이 설명하는 코드 사이에 끼어 doc 블록이 떨어진다 규칙과 부딪친다).
    while boundary_index > 0 and boundary_index < len(lines):
        previous = lines[boundary_index - 1].strip()
        if previous == "" or previous.startswith(('#include', '#pragma', '#if', '#endif', '#else', '#elif')):
            break
        if previous.startswith(('//', '/*', '*')) is False:
            break
        boundary_index -= 1

    top_lines = lines[:boundary_index]
    bottom_lines = lines[boundary_index:]

    # 3. 상단 영역에서 모든 인클루드 추출 및 정렬/그룹핑
    non_include_lines = []
    include_lines = []
    first_include_index = -1

    for line in top_lines:
        include_match = _INCLUDE_RE.match(line)
        if include_match:
            if first_include_index == -1:
                first_include_index = len(non_include_lines)
            include_lines.append(line)
        else:
            if line.strip() != "":  # 주석이나 pragma 유지, 빈 줄은 어차피 나중에 추가/포매팅됨
                non_include_lines.append(line)

    if first_include_index == -1:
        first_include_index = len(non_include_lines)

    seen_includes = set()
    pch_line = None
    matching_header_line = None
    local_includes_list = []
    system_includes_list = []

    base_file_name = Path(relative_file_path).stem

    for line in include_lines:
        include_match = _INCLUDE_RE.match(line)
        include_type = include_match.group(1)
        include_name = include_match.group(2)

        # <sw/...> 같은 프로젝트 자동 생성 헤더는 System Include가 아니므로 "" 로 변환
        if include_type == '<' and include_name.startswith("sw/"):
            include_type = '"'
            line = f'#include "{include_name}"'

        # 상대 경로 및 대소문자 정규화: Source/, Test/, Tools/ 상대 경로 복원 및 대소문자 교정
        if include_type == '"' and not include_name.endswith(".xxx") and include_name != "pch.h":
            include_lower = include_name.lower()
            if source_header_map and include_lower in source_header_map:
                normalized_path = source_header_map[include_lower]
                line = f'#include "{normalized_path}"'
                include_name = normalized_path
            elif test_header_map and "Test" in relative_file_path and include_lower in test_header_map:
                normalized_path = test_header_map[include_lower]
                line = f'#include "{normalized_path}"'
                include_name = normalized_path
            elif tools_header_map and "Tools" in relative_file_path and include_lower in tools_header_map:
                normalized_path = tools_header_map[include_lower]
                line = f'#include "{normalized_path}"'
                include_name = normalized_path

        # 중복 제거
        # 닫는 괄호를 **먼저 변수로 뽑는다.** f-string 식 안의 백슬래시는 Python 3.12(PEP 701) 부터
        # 허용된 것이고, CI 러너(ubuntu-22.04)의 `python3` 는 3.10 이라 SyntaxError 로 죽는다 —
        # 그리고 그 죽는 자리가 **CMake configure** 라 리눅스 CI 가 통째로 멈췄다.
        closing_bracket = ">" if include_type == "<" else '"'
        include_full = f"{include_type}{include_name}{closing_bracket}"
        if not include_name.endswith(".xxx"):
            if include_full in seen_includes:
                continue
            seen_includes.add(include_full)

        if include_name == "pch.h":
            pch_line = line
        elif include_type == '<':
            system_includes_list.append(line)
        else:
            # 매칭 헤더 판별 (대소문자 무시 비교). 단, .cpp 파일에서만 적용
            if is_cpp and Path(include_name).stem.lower() == base_file_name.lower():
                matching_header_line = line
            else:
                local_includes_list.append(line)

    # 로컬 인클루드는 알파벳순 정렬, 시스템/서드파티(<...>) 인클루드는 선언 순서 유지 (헤더 간 순서 의존성 보존)
    local_includes_list.sort()

    if is_cpp and not pch_line:
        if "ThirdParty" not in relative_file_path and "Tools/vcpkg" not in relative_file_path:
            pch_line = '#include "pch.h"'
            violations_list.append(f'{relative_file_path}: "pch.h"가 누락되어 자동 추가했습니다.')

    sorted_includes_list = []
    if pch_line:
        sorted_includes_list.append(pch_line)
        sorted_includes_list.append("")

    if matching_header_line:
        sorted_includes_list.append(matching_header_line)
        sorted_includes_list.append("")

    # 로컬 인클루드 폴더(루트) 기준으로 한 줄씩 띄우기
    last_root_folder = None
    for line in local_includes_list:
        include_match = _INCLUDE_RE.match(line)
        include_name = include_match.group(2)
        parts = include_name.split('/')
        root_folder = parts[0] if len(parts) > 1 else ""

        if last_root_folder is not None and root_folder != last_root_folder:
            sorted_includes_list.append("")
        
        sorted_includes_list.append(line)
        last_root_folder = root_folder

    if system_includes_list:
        if local_includes_list and sorted_includes_list and sorted_includes_list[-1] != "":
            sorted_includes_list.append("")
        for line in system_includes_list:
            sorted_includes_list.append(line)

    while sorted_includes_list and sorted_includes_list[-1] == "":
        sorted_includes_list.pop()

    # 최종 상단 텍스트 조합
    if sorted_includes_list and non_include_lines[first_include_index:]:
        final_top_lines = non_include_lines[:first_include_index] + sorted_includes_list + [""] + non_include_lines[first_include_index:]
    else:
        final_top_lines = non_include_lines[:first_include_index] + sorted_includes_list + non_include_lines[first_include_index:]

    while final_top_lines and final_top_lines[-1] == "":
        final_top_lines.pop()

    while bottom_lines and bottom_lines[0] == "":
        bottom_lines.pop(0)

    # 마지막 include(또는 상단 헤더 영역)와 하단 본문(namespace 등) 사이에 항상 1줄 띄우기
    if final_top_lines and bottom_lines:
        final_top_lines.append("")

    # 4. (기존 검사 로직은 위에서 자동 추가로 대체됨)

    # 변경사항이 있다면 파일에 쓰기
    new_text = "\n".join(final_top_lines + bottom_lines)
    if text.endswith("\n") and not new_text.endswith("\n"):
        new_text += "\n"

    return new_text, violations_list


#: 헤더 조회표(저장소 루트 → 표 셋) — 픽서는 파일마다 변환을 부르므로 한 번 만들어 둔다.
_s_map_header_lookup: dict[Path, tuple[dict[str, str], dict[str, str], dict[str, str]]] = {}


def inspect_text(raw_text: str, relative: str, header_maps: tuple[dict[str, str], dict[str, str], dict[str, str]]) -> tuple[list[str], str | None]:
    """
    파일 하나의 판정과 고친 글 — 게이트 `CheckIncludeOrder` 와 픽서(`fix_text`)가 **같은 이 함수**를 부른다. `rawText` 는 줄 끝을 건드리지 않고 읽은 원본이다.
    BOM 을 뗀 글로 판정하고, 고친 글은 원본의 BOM · 줄 끝으로 되돌린다. 줄 끝만 섞인 파일은 위반이 아니지만 고친 글은 줄 끝을 하나로 맞춘다.
    돌려주는 것은 `(위반 목록, 고친 글 — 고칠 것이 없으면 None)`.
    """
    text = raw_text.replace("\r\n", "\n").replace("\r", "\n")
    line_ending = "\r\n" if "\r\n" in raw_text else "\n"
    body = text.removeprefix("\ufeff")
    new_body, list_violation = _compute_include_order(body, relative, *header_maps)
    if new_body != body:
        list_violation.append(f"{relative}: Include 순서/중복 문제가 발견되었습니다. "
                             f"(`{FIX_COMMAND} {relative}` 로 고친다)")
    restored = ("\ufeff" if text.startswith("\ufeff") else "") + new_body.replace("\n", line_ending)
    return list_violation, (restored if restored != raw_text else None)


def fix_text(text: str, relative_file_path: str) -> tuple[str, bool]:
    """픽서 변환 — 게이트와 같은 `inspect_text`. 헤더 조회표는 이 저장소의 것이다. 돌려주는 것은 `(고친 글, 바뀌었는가)`."""
    repository_root = get_project_root().resolve()
    if repository_root not in _s_map_header_lookup:
        _s_map_header_lookup[repository_root] = build_header_lookup_map(repository_root)
    _, fixed_text = inspect_text(text, relative_file_path, _s_map_header_lookup[repository_root])
    return (text, False) if fixed_text is None else (fixed_text, True)


def _inspect_file(header_maps: tuple, repository_root: Path, path: Path) -> list[str]:
    """파일 하나의 위반(읽을 수 없으면 `!` 로 시작하는 한 줄). 훑는 사이에 지워진 파일은 건너뛴다."""
    relative = path.resolve().relative_to(repository_root).as_posix()
    try:
        data = read_bytes(path)
        if data is None:
            return []
        raw_text = data.decode("utf-8")
    except (OSError, UnicodeDecodeError) as exception:
        return [f"!{relative}: 읽기 실패: {exception}"]
    return inspect_text(raw_text, relative, header_maps)[0]


def check_include_order(repository_root: Path, list_file_argument: Sequence[str] | None) -> tuple[list[str], int]:
    """`list_file_argument`(None 이면 전체)의 C++ 파일을 검사한다. 돌려주는 것은 `(위반 줄 목록, 본 파일 수)`."""
    repository_root = repository_root.resolve()
    suffixes = tuple(sorted(CPP_ALL_EXTENSIONS))
    if list_file_argument is not None:
        list_path = resolve_file_arguments(repository_root, list_file_argument, list_scan_root=LINT_TARGET_REL_DIRS, suffixes=suffixes)
    else:
        list_path = collect_repository_files(repository_root, LINT_TARGET_REL_DIRS, suffixes=suffixes, excluded_dir_names=NOT_OUR_DIR_NAMES)
    if not list_path:
        return [], 0
    worker = functools.partial(_inspect_file, build_header_lookup_map(repository_root), repository_root)
    return flat_map_in_processes(worker, list_path), len(list_path)


def check_include_order_scope(repository_root: Path, scope: LintScope) -> list[str]:
    """`rules.py`: scope 의 C++ 파일의 위반 줄 — 읽지 못한 파일(`!`)도 위반이다."""
    list_file = scope.list_absolute(repository_root, tuple(sorted(CPP_ALL_EXTENSIONS)))
    return [line.lstrip("!") for line in check_include_order(repository_root, list_file)[0]] if list_file else []


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Include 순서 검사")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트(기본: 이 스크립트의 저장소)")
    parser.add_argument("--files", nargs="*", default=None, help="검사할 파일 (생략 시 전체)")
    args = parser.parse_args(argv)

    list_violation, checked = check_include_order(args.root or get_project_root(), args.files)
    if args.files is not None and checked == 0:
        print(f"[{CHECK_TAG}] SKIPPED (검사할 파일 없음)")
        return ExitCode.PASSED
    list_unreadable = [line for line in list_violation if line.startswith("!")]
    if list_unreadable:
        print(f"[{CHECK_TAG}] FAIL — 읽을 수 없는 파일 {len(list_unreadable)}개")
        for line in list_unreadable:
            print(f"  - {line[1:]}")
        return ExitCode.CHECK_INVALID
    if list_violation:
        print(f"[{CHECK_TAG}] FAIL — Include 순서 규칙 위반 {len(list_violation)}건")
        for line in list_violation:
            print(f"  - {line}")
        return ExitCode.VIOLATION
    print(f"[{CHECK_TAG}] OK ({checked} files)")
    return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
