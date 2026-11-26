"""
소스 파일 · 폴더를 옮기고 저장소의 참조를 한 번에 고친다 — 구조 개편 단계마다 같은 방법으로 옮기려고 둔다. 이름 바꾸기는 `dev/rename_symbol.py`(`rename-symbol`)다.

  py -3 -m Scripts move-source Engine/Object/SceneAnimation=Engine/Animation/Scene Engine/A/B.h=Engine/C/B.h [--dry-run]
  py -3 -m Scripts move-source --base . cmake/Engine=cmake/Modules/Build

move-source: 경로는 `Source/` 기준(폴더면 안의 파일 전부, 하위 폴더 포함). `--base .` 이면 저장소 루트 기준(`cmake/` · `Config/` 처럼 Source 밖 파일). 하는 일:
  1. `git mv`
  2. 추적 중인 텍스트 파일(Source · Tools · cmake · Scripts · docs · 스킬 · 최상위 CMake)에서 옮긴 파일의 전체 경로(`Engine/X/Y.h`)와
     폴더 접두어(`Engine/X/` — CMake 정규식 · 주석)를 새 경로로
  3. 같은 폴더 짧은 include(`#include "Y.h"`)가 옮긴 뒤 다른 폴더를 가리키게 되면 전체 경로로
  4. 고친 C++ 파일을 clang-format 으로(include 순서 · 묶음 · 정렬 칸)
"""

from __future__ import annotations

import argparse
import re
import time
from pathlib import Path

from common.command import Command, ScriptContext
from common.exit_code import ExitCode
from common.git import run_git
from dev.rename_scope import CPP_SUFFIX, C_LIKE_SUFFIX, TEXT_SUFFIX, list_tracked_text, read_text, rewrite_unmarked_lines, write_text

BARE_INCLUDE = re.compile(r'^(\s*#\s*include\s*")([^"/]+)(")', re.M)


def expand_moves(source: Path, list_pair: list[str]) -> dict[str, str]:
    """`old=new` 쌍을 파일 단위 사상(Source 기준 posix 경로)으로 편다. 없는 원본 · 이미 있는 대상은 오류다."""
    map_file: dict[str, str] = {}
    for pair in list_pair:
        old, sep, new = pair.partition("=")
        if not sep or not old or not new:
            raise ValueError(f"'{pair}' 은 old=new 모양이 아니다")
        old, new = old.strip("/"), new.strip("/")
        old_path = source / old
        if old_path.is_file():
            map_file[old] = new
        elif old_path.is_dir():
            for file in sorted(p for p in old_path.rglob("*") if p.is_file()):
                relative = file.relative_to(old_path).as_posix()
                map_file[f"{old}/{relative}"] = f"{new}/{relative}"
        else:
            raise FileNotFoundError(f"{old_path} 이 없다")
    for new in map_file.values():
        if (source / new).exists():
            raise FileExistsError(f"{source / new} 이 이미 있다")
    return map_file


def folder_prefixes(list_pair: list[str], source: Path) -> list[tuple[str, str]]:
    """폴더 이동의 접두어 쌍(긴 것 먼저 — 안쪽 폴더가 바깥 폴더 규칙에 먼저 먹히지 않게)."""
    list_prefix = []
    for pair in list_pair:
        old, _, new = pair.partition("=")
        if (source / old.strip("/")).is_dir():
            list_prefix.append((old.strip("/") + "/", new.strip("/") + "/"))
    return sorted(list_prefix, key=lambda item: -len(item[0]))


def rewrite_bare_includes(text: str, old_folder: str, new_folder: str, source: Path, map_file: dict[str, str]) -> str:
    """파일이 `oldFolder` 에서 `newFolder` 로 갔을 때, 짧은 include 가 다른 폴더를 가리키게 되면 전체 경로로 바꾼다."""

    def replace(match: re.Match[str]) -> str:
        name = match.group(2)
        old_target = f"{old_folder}/{name}" if old_folder else name
        if not (source / old_target).is_file():
            return match.group(0)  # pch.h · 생성 헤더처럼 이 폴더 밖에서 풀리는 이름
        new_target = map_file.get(old_target, old_target)
        if new_target.rpartition("/")[0] == new_folder:
            return match.group(0)
        return f"{match.group(1)}{new_target}{match.group(3)}"

    return BARE_INCLUDE.sub(replace, text)


def list_renamed_file_stem(source: Path, map_file: dict[str, str]) -> list[tuple[str, str]]:
    """이름을 바꾼 설정 · 데이터 파일(C 계열 · 파이썬 밖)의 줄기 쌍 — `Config/Server/Old.json` → `Config/Server/New.json` 이면 (`Config/Server/Old`, `Config/Server/New`).
    곁 이름(`Old.online.json` — `OnlineServerConfig::makeConfigPath`)과 확장자 없는 이름(`"Config/Server/Old"`)이 같은 줄기로 적힌다.
    옛 폴더에 같은 줄기로 시작하는 옮기지 않는 파일이 있으면 그 파일을 가리키는 글이라 두지 않는다. 옮기기 전에 잰다."""
    list_stem = []
    moved_set = set(map_file)
    for old, new in map_file.items():
        old_folder, _, old_name = old.rpartition("/")
        new_folder, _, new_name = new.rpartition("/")
        old_stem, _, old_rest = old_name.partition(".")
        new_stem, _, new_rest = new_name.partition(".")
        if Path(old).suffix.lower() in C_LIKE_SUFFIX | {".py"} or old_stem == new_stem or old_rest != new_rest or not old_folder:
            continue
        list_sibling = [f"{old_folder}/{path.name}" for path in (source / old_folder).glob(f"{old_stem}.*") if path.is_file()]
        if any(sibling not in moved_set for sibling in list_sibling):
            continue
        list_stem.append((f"{old_folder}/{old_stem}", f"{new_folder}/{new_stem}"))
    return list_stem


def make_path_rule(map_file: dict[str, str], list_folder_prefix: list[tuple[str, str]], root_base: bool = False,
                   list_stem: list[tuple[str, str]] = ()) -> tuple[re.Pattern[str], object]:
    """파일 경로 · 폴더 접두어를 **한 번 훑기** 규칙 하나로 묶는다(긴 것 먼저).
    차례로 적용하면 앞 규칙이 바꾼 글을 뒤 규칙이 다시 바꿨다 — 폴더를 자기 아래로 옮기면(`A=A/Server`) 파일 규칙이 만든 `A/Server/X.h` 를
    폴더 규칙 `A/` 가 다시 `A/Server/Server/X.h` 로 바꿨다. 정규식 하나의 sub 는 바꾼 자리를 다시 보지 않는다.
    전체 경로는 낱말 경계로 — `Engine/A/B.h` 가 `MyEngine/A/B.h` 안에서 바뀌지 않게(`Source/Engine/A/B.h` 처럼 앞에 `/` 가 오는 것은 바꾼다)."""
    map_new: dict[str, str] = {}
    list_alternative: list[tuple[int, str]] = []
    for old, new in map_file.items():
        map_new[old] = new
        list_alternative.append((len(old), re.escape(old) + r"(?![\w.])"))
    for old, new in list_folder_prefix:
        map_new[old] = new
        list_alternative.append((len(old), re.escape(old)))
        bare_old = old.rstrip("/")
        if "/" in bare_old:   # 끝 `/` 없는 폴더 이름(`<item>Source/Games/Old</item>` — 로컬라이제이션 codeRoots). 한 낱말 폴더(`Engine`)는 일반 낱말과 겹쳐 두지 않는다
            map_new[bare_old] = new.rstrip("/")
            list_alternative.append((len(bare_old), re.escape(bare_old) + r"(?![\w./-])"))
    for old, new in list_stem:
        map_new[old] = new
        list_alternative.append((len(old), re.escape(old) + r"(?=\.[\w-]|[^\w./-]|$)"))   # 곁 이름 · 확장자 없는 이름 — 같은 줄기의 파일 경로 자체는 위의 긴 규칙이 먼저 받는다
    list_alternative.sort(key=lambda item: -item[0])
    # 루트 기준 경로(`cmake/Modules/…`)는 바깥 경로 가운데(`emscripten/cmake/Modules/Platform/`)와 겹친다 — `낱말/` 뒤는 받지 않고 `${ROOT}/` 뒤만 받는다.
    # Source 기준은 `Source/Engine/…` 처럼 앞에 폴더가 붙는 것이 정상이라 그대로 둔다.
    root_guard = r"(?<![\w.-]/)" if root_base else ""
    pattern = re.compile(r"(?<![\w.-])" + root_guard + "(?:" + "|".join(part for _, part in list_alternative) + ")")
    return pattern, lambda match: map_new[match.group(0)]


def make_file_tag_rules(map_file: dict[str, str]) -> list[tuple[re.Pattern[str], object]]:
    """`@file` 은 최상위 폴더 없이 적은 곳도 있다(`@file Utility/X.h`). 폴더가 하나 남는 것만 받는다 — 최상위 폴더를 옮기면(`IntegrationTests/CMakeLists.txt`)
    남는 것이 파일 이름뿐이라 다른 폴더의 같은 이름(뿌리의 `@file CMakeLists.txt`)까지 바꿨다."""
    list_rule = []
    for old, new in map_file.items():
        short_old = old.partition("/")[2]
        if "/" in short_old:
            list_rule.append((re.compile(r"(@file\s+)" + re.escape(short_old) + r"\b"), lambda match, new=new: match.group(1) + new))
    return list_rule


class MoveSourceCommand(Command):
    name = "move-source"
    help = "소스 파일 · 폴더를 옮기고 include · CMake · 문서 참조를 고친다(Source 기준 old=new)"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("pair", nargs="+", help="--base 기준 old=new (파일 또는 폴더)")
        parser.add_argument("--base", default="Source", help="경로의 기준 폴더(저장소 루트 기준, 기본 Source — 루트는 `.`)")
        parser.add_argument("--dry-run", action="store_true", help="옮길 파일 목록만 찍는다")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        root = context.root
        source = root / args.base   # pathlib 가 `.` 을 접는다
        base_prefix = "" if source == root else source.relative_to(root).as_posix() + "/"
        map_file = expand_moves(source, args.pair)
        # 폴더 접두어는 옮기기 전에 잰다 — 옮긴 뒤에는 옛 폴더가 없어 규칙이 비고, 폴더 이동의 설정 · 문서 경로(`Source/X/Old/`)가 그대로 남았다.
        list_folder_prefix = folder_prefixes(args.pair, source)
        list_stem = list_renamed_file_stem(source, map_file)
        if args.dry_run:
            for old, new in map_file.items():
                print(f"{old} -> {new}")
            return ExitCode.PASSED

        # 짧은 include 는 옮기기 전 자리에서 풀어야 하므로 내용 재작성을 먼저 계산한다.
        map_moved_text: dict[str, str] = {}
        for old, new in map_file.items():
            path = source / old
            if path.suffix.lower() in CPP_SUFFIX:
                map_moved_text[new] = rewrite_bare_includes(read_text(path), old.rpartition("/")[0], new.rpartition("/")[0], source, map_file)
        list_neighbour_text: dict[Path, str] = {}
        moved_set = set(map_file)
        for path in list_tracked_text(root):
            relative = path.relative_to(source).as_posix() if path.is_relative_to(source) else None
            if relative is None or relative in moved_set or path.suffix.lower() not in CPP_SUFFIX:
                continue
            folder = relative.rpartition("/")[0]
            text = read_text(path)
            rewritten = rewrite_bare_includes(text, folder, folder, source, map_file)
            if rewritten != text:
                list_neighbour_text[path] = rewritten

        for old, new in map_file.items():
            (source / new).parent.mkdir(parents=True, exist_ok=True)
            for _ in range(20):  # IDE 의 git 이 잠깐 잡는 index.lock 은 기다리면 풀린다 — 반쯤 옮긴 채 멈추지 않게
                result = run_git(["mv", f"{base_prefix}{old}", f"{base_prefix}{new}"], cwd=root)
                if result.succeeded or "index.lock" not in result.stderr:
                    break
                time.sleep(0.5)
            if not result.succeeded:
                raise RuntimeError(f"git mv {old} 실패: {result.stderr.strip()}")
        for new, text in map_moved_text.items():
            write_text(source / new, text)
        for path, text in list_neighbour_text.items():
            write_text(path, text)
        for old in map_file:
            folder = (source / old).parent
            while folder != source and folder.is_dir() and not any(folder.iterdir()):
                folder.rmdir()
                folder = folder.parent

        list_rule = [make_path_rule(map_file, list_folder_prefix, root_base=base_prefix == "", list_stem=list_stem)]
        list_rule += make_file_tag_rules(map_file)
        changed_cpp_set = {source / new for new in map_moved_text} | set(list_neighbour_text)
        # 옮긴 파일은 새 자리로 훑는다 — 훑는 뿌리 자체를 옮기면(`Test` → `IntegrationTests`) 뿌리 목록에 새 이름이 없어 그 안의 참조가 그대로 남았다.
        list_moved_text = [source / new for new in map_file.values() if Path(new).suffix.lower() in TEXT_SUFFIX]
        for path in sorted(set(list_tracked_text(root)) | set(list_moved_text)):
            text = read_text(path)
            def rewrite_paths(part: str) -> str:
                for pattern, replacement in list_rule:
                    part = pattern.sub(replacement, part)
                return part

            rewritten = rewrite_unmarked_lines(text, rewrite_paths)
            if rewritten != text:
                write_text(path, rewritten)
                if path.suffix.lower() in CPP_SUFFIX:
                    changed_cpp_set.add(path)
        list_changed_cpp = sorted(changed_cpp_set)

        from lint.format_files import run_clang_format_batch

        run_clang_format_batch(list_changed_cpp, cwd=root)  # 경로가 바뀌면 include 순서와 정렬 칸이 어긋난다
        print(f"[move-source] {len(map_file)} 파일을 옮기고 C++ {len(list_changed_cpp)} 파일의 참조 · include 순서를 고쳤다")
        return ExitCode.PASSED
