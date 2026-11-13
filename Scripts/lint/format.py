#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/format.py

C++ · 셰이더 소스를 고친다 — 고치는 명령은 `format` 하나다: C++ 는 include 순서 · 중복(규칙은 `include_order.py`)을 고친 뒤 clang-format, 셰이더는
clang-format. 린트(include-order · format)의 안내도 이 명령을 가리킨다(`include_order.FIX_COMMAND`) — 안내 명령과 고치는 명령이 갈리지 않는다.
대상 고르기(`--files` > `--all` > git 변경분 > 전체)와 clang-format 일괄 실행(`run_clang_format_batch`, 커밋 훅도 부른다)이 한 자리다.

  py -3 -m Scripts format [--files ...|--all]            include 순서 + clang-format 적용(C++ 와 셰이더)

파일을 고쳐 쓰되 줄 끝 · BOM 은 건드리지 않는다.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.clang_format import resolve_clang_format  # noqa: E402
from common.exit_code import ExitCode  # noqa: E402
from common.file_collect import collect_repository_files, collect_source_files, get_lint_search_dirs, resolve_file_arguments  # noqa: E402
from common.git import list_git_files  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from common.project_constants import CPP_ALL_EXTENSIONS, NOT_OUR_DIR_NAMES, SHADER_SOURCE_EXTENSIONS  # noqa: E402
from common.run_process import run_process  # noqa: E402
from lint.include_order import fix_text  # noqa: E402
from lint.parallel import flat_map_concurrent, run_until_non_zero  # noqa: E402

INCLUDE_TAG = "FormatIncludeOrder"
CLANG_FORMAT_TAG = "FormatClangFormat"


# --- git 이 알려 주는 대상 ------------------------------------------------------

def get_modified_cpp_files(root: Path | None = None,
                        extensions: set[str] | None = None,
                        include_untracked: bool = True) -> list[Path]:
    """
    Git 작업 트리에서 수정(Tracked)되었거나 새로 생성된(Untracked) C++ 소스/헤더 파일 목록을 반환합니다.
    """
    target_extensions = extensions if extensions is not None else CPP_ALL_EXTENSIONS
    project_root = root or get_project_root()

    # 1. 수정된 파일 (Staged + Unstaged)
    file_set = list_git_files(project_root, ["diff", "--name-only", "HEAD"], target_extensions)
    # 2. 새로 추가된 파일 (Untracked)
    if include_untracked:
        file_set |= list_git_files(project_root, ["ls-files", "--others", "--exclude-standard"], target_extensions)
    return sorted(file_set)


# --- clang-format 일괄 실행 -----------------------------------------------------

def run_clang_format_batch(files: Sequence[Path | str],
                        *,
                        check_only: bool = False,
                        cwd: Path | None = None,
                        clang_format_path: str | None = None,
                        batch_size: int = 64) -> int:
    """
    지정된 파일 목록에 대해 clang-format을 배치 단위로 병렬 실행합니다.

    Args:
        files: 포맷팅할 파일 경로 목록
        checkOnly: True이면 --dry-run --Werror 모드로 검사만 수행, False이면 -i 모드로 파일 직접 수정
        cwd: 실행 기준 디렉터리 (기본값: 프로젝트 루트)
        clang_format_path: 사용할 clang-format 실행 파일 경로 (None이면 자동 탐색)
        batch_size: 1회 배치당 처리할 파일 개수 (기본 64개)

    Returns:
        모든 배치가 성공하면 0, 실패 시 해당 프로세스 종료 코드 반환
    """
    if not files:
        return ExitCode.PASSED
    project_root = cwd or get_project_root()
    clang_format_exe = clang_format_path or resolve_clang_format()
    if not clang_format_exe:
        sys.stderr.write(
            "[FormatClangFormat] clang-format을 찾을 수 없습니다. "
            "'py -3 Scripts/setup/environment.py'를 먼저 실행해주세요.\n"
        )
        return ExitCode.VIOLATION

    file_path_strings = [str(f) for f in files]
    batches = [
        file_path_strings[start_index : start_index + batch_size]
        for start_index in range(0, len(file_path_strings), batch_size)
    ]

    def _run_single_batch(batch: list[str]) -> int:
        command = [clang_format_exe]
        if check_only:
            command.extend(["--dry-run", "--Werror"])
        else:
            command.append("-i")
        command.extend(batch)
        return run_process(command, cwd=project_root, capture=False).return_code

    # clang-format 은 자식 프로세스라 코어 수를 넘겨 띄우면 서로 경합한다 (Parallel 의 프로세스 정책).
    return run_until_non_zero(_run_single_batch, batches)


# --- 대상 고르기 ----------------------------------------------------------------

def add_file_arguments(parser: argparse.ArgumentParser) -> None:
    """대상 파일을 고르는 인자 — 픽서들이 같은 철자를 쓴다."""
    parser.add_argument(
        "--files",
        nargs="*",
        default=None,
        help="대상 C++ 파일 목록 (생략 시 Git 변경 파일, 없으면 전체 대상) ",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="프로젝트 전체 C++ 파일에 대해 실행",
    )


@dataclass(frozen=True)
class FixerFileKind:
    """
    C++ 가 아닌 파일(셰이더)을 고치는 픽서의 대상 — 확장자(대소문자 무시)와, 내려가지 않는 폴더 이름(빌드 산출물 · 남의 코드).
    """

    suffixes: tuple[str, ...] = ()
    excluded_dir_names: frozenset[str] = NOT_OUR_DIR_NAMES

    def is_target(self, path: Path, repository_root: Path) -> bool:
        if path.suffix.lower() not in self.suffixes:
            return False
        try:
            relative = path.resolve().relative_to(repository_root.resolve()).as_posix()
        except ValueError:
            return False
        return not any(part in self.excluded_dir_names for part in relative.split("/")[:-1])


def select_fixer_target_files(args: argparse.Namespace, repository_root: Path, tag: str, file_kind: FixerFileKind | None = None) -> list[Path]:
    """
    무엇을 고칠지 고릅니다: 지정한 파일(`--files` — `common.file_collect.resolveFileArguments`) > `--all` > Git 변경 파일 > 전체.
    `file_kind` 가 없으면 C++ 소스, 있으면 그 종류(셰이더).

    마지막 폴백("변경된 파일이 없으면 전체")이 이 규칙의 핵심이다. 깨끗한 트리에서 돌려도
    아무 일도 안 하는 대신 전체를 본다 — 처음 받은 저장소에서도 한 번에 맞춰진다.
    """
    if file_kind is not None:
        return _select_file_kind_targets(args, repository_root, tag, file_kind)

    if args.files:
        return resolve_file_arguments(repository_root, args.files, suffixes=CPP_ALL_EXTENSIONS)

    if args.all:
        return collect_source_files(get_lint_search_dirs(repository_root))

    list_modified = get_modified_cpp_files(repository_root)
    if list_modified:
        print(f"[{tag}] Git 변경 파일 {len(list_modified)}개 감지.", file=sys.stderr)
        return list_modified

    list_all = collect_source_files(get_lint_search_dirs(repository_root))
    print(f"[{tag}] 변경된 파일이 없어 전체 {len(list_all)}개 파일 대상 실행.", file=sys.stderr)
    return list_all


def _select_file_kind_targets(args: argparse.Namespace, repository_root: Path, tag: str, file_kind: FixerFileKind) -> list[Path]:
    """`select_fixer_target_files` 의 C++ 가 아닌 갈래 — 같은 순서(`--files` > `--all` > Git 변경 > 전체), 같은 제외."""
    if args.files:
        list_file = resolve_file_arguments(repository_root, args.files, suffixes=file_kind.suffixes, excluded_dir_names=file_kind.excluded_dir_names)
        return [path for path in list_file if file_kind.is_target(path, repository_root)]

    if not args.all:
        list_modified = [path for path in get_modified_cpp_files(repository_root, extensions=set(file_kind.suffixes))
                        if file_kind.is_target(path, repository_root)]
        if list_modified:
            print(f"[{tag}] Git 변경 파일 {len(list_modified)}개 감지.", file=sys.stderr)
            return list_modified

    list_all = [path for path in collect_repository_files(repository_root, ("",), suffixes=file_kind.suffixes, excluded_dir_names=file_kind.excluded_dir_names)
               if file_kind.is_target(path, repository_root)]
    if not args.all:
        print(f"[{tag}] 변경된 파일이 없어 전체 {len(list_all)}개 파일 대상 실행.", file=sys.stderr)
    return list_all


# --- include 순서 -----------------------------------------------------------------

class FormatIncludeOrderFixer:
    """include 순서 픽서 — 파일 하나를 읽어 `fix_text` 로 고친다."""

    def process_file(self, file_path: Path, check_only: bool = False) -> list[str]:
        try:
            relative_path = file_path.resolve().relative_to(get_project_root().resolve()).as_posix()
        except ValueError:
            relative_path = file_path.as_posix()
        try:
            with file_path.open("r", encoding="utf-8", errors="strict", newline="") as file:
                content = file.read()
        except UnicodeDecodeError as error:
            # 고쳐 쓰면 UTF-8 이 아닌 바이트가 사라진다 — 읽지 못한 파일은 손대지 않고 알린다.
            return [f"[{INCLUDE_TAG}] {file_path}: UTF-8 이 아니라 건너뜁니다({error.reason}, 바이트 {error.start}) — 파일을 UTF-8 로 저장하십시오"]
        except OSError as error:
            return [f"[{INCLUDE_TAG}] {file_path} 읽기 실패: {error}"]

        formatted, changed = fix_text(content, relative_path)
        if not changed:
            return []
        if check_only:
            return [f"[{INCLUDE_TAG}] {file_path}: include 순서 · 중복이 규칙과 다르다"]
        try:
            with file_path.open("w", encoding="utf-8", newline="") as file:
                file.write(formatted)
        except OSError as error:
            return [f"[{INCLUDE_TAG}] {file_path} 쓰기 실패: {error}"]
        return [f"[{INCLUDE_TAG}] {file_path}: include 순서를 고쳤다"]

    def process_files(self, files: Sequence[Path], check_only: bool = False, max_workers: int | None = None) -> list[str]:
        return flat_map_concurrent(lambda path: self.process_file(path, check_only), list(files), worker_count=max_workers)


# --- clang-format -----------------------------------------------------------------

SHADER_FILE_KIND = FixerFileKind(suffixes=tuple(sorted(SHADER_SOURCE_EXTENSIONS)))


def main_format(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="C++ 는 include 순서 · 중복을 고친 뒤 clang-format, 셰이더는 clang-format 을 적용합니다.")
    add_file_arguments(parser)
    args = parser.parse_args(argv)

    root = get_project_root()
    list_cpp = select_fixer_target_files(args, root, CLANG_FORMAT_TAG)
    list_shader = select_fixer_target_files(args, root, CLANG_FORMAT_TAG, SHADER_FILE_KIND)
    file_list = list_cpp + list_shader

    if not file_list:
        sys.stderr.write(f"[{CLANG_FORMAT_TAG}] 포맷팅 대상 C++ · 셰이더 파일이 없습니다.\n")
        return ExitCode.PASSED

    for message in FormatIncludeOrderFixer().process_files(list_cpp):   # include 를 먼저 — 줄을 옮긴 뒤 clang-format 이 정렬한다
        print(message)
    print(f"[{CLANG_FORMAT_TAG}] {len(file_list)}개 파일(셰이더 {len(list_shader)}개)에 대해 clang-format 적용 중...", file=sys.stderr)
    return run_clang_format_batch(file_list, check_only=False, cwd=root)
