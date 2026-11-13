"""
에셋 파일 · 폴더를 옮기고(이름 바꾸기 포함) 그것을 가리키는 참조를 한 번에 고친다 — 언리얼 Fix Up Redirectors 를 옮기면서 바로 하는 것과 같다.

  py -3 -m Scripts move-asset common/prefabs/props/bow.prefab.xml common/prefabs/weapons/bow.prefab.xml [--dry-run]
  py -3 -m Scripts move-asset game/empty/materials game/empty/surfaces

경로는 리소스 id(`Content/` 뒤 — 앞에 `Content/` 를 붙여도 된다)다. 하는 일:
  1. `git mv`
  2. 참조 파일(Content 의 글 에셋 · 테스트 CMake)에서 경로 칸(`Config/Dev/AssetReferenceFields.json` — 커밋 전 검사 · `App --check-asset-refs` ·
     에디터 이름 바꾸기와 같은 목록)의 값만 새 경로로. 폴더면 그 아래 경로 전부, 원본(`models_raw/x.glb`)이면 그 임포트 산출물 경로(`models/x…`)도.
     칸 밖의 글(주석 · 문서 · 출처 표)은 고치지 않는다 — 끝에 남은 옛 경로를 찍는다.
  3. 옛 id 도메인의 리다이렉트 표(`Content/<도메인>/redirects.xml`)에 옮긴 파일마다 줄을 더한다 — 저장소 밖(세이브 · 서버 · 배포된 팩)에 적힌 옛 id 가 산다
     (언리얼이 옮긴 자리에 리다이렉터를 남기는 것과 같다). 규칙은 에디터 옮기기와 같다(`add_redirects` = C++ `AssetRedirect::addToXMLText`).
  4. 작업 트리 전체를 검사해 끊어진 참조가 남았으면 알린다(실패 코드).
임포트 산출물은 커밋하지 않으므로 원본을 옮긴 뒤에는 `App --import-models` 등으로 다시 임포트한다.
"""

from __future__ import annotations

import argparse
import time
from pathlib import Path

from common.exit_code import ExitCode
from common.git import run_git
from common.paths import get_project_root
from lint.asset_references import (FieldConfig, add_redirects, check_working_tree, extract_references, is_referrer, CONTENT_FOLDER, REDIRECT_FILE_NAME, list_index_files, load_config,
                                  make_redirect_table_id)

NAME_CHARACTERS = set("abcdefghijklmnopqrstuvwxyz0123456789_.-/")
# 옮긴 뒤 옛 경로가 칸 밖에 남았는지 훑는 글 파일 — 경로 칸의 글 에셋 · 문서 · 출처 표 · 테스트 CMake.
LEFTOVER_SUFFIX = (".xml", ".json", ".material", ".md", ".txt", ".cpp", ".h")


def to_asset_id(argument: str) -> str:
    normalized = argument.replace("\\", "/").strip("/")
    if normalized.lower().startswith(CONTENT_FOLDER.lower() + "/"):
        normalized = normalized[len(CONTENT_FOLDER) + 1:]
    return normalized


def make_rules(old_id: str, new_id: str, is_folder: bool, config: FieldConfig) -> list[tuple[str, str, bool]]:
    """(옛 접두, 새 접두, 폴더인가) 규칙들 — 옮긴 것 자신과, 원본이면 그 임포트 산출물 자리."""
    list_rule = [(old_id.lower(), new_id.lower(), is_folder)]
    for imported in config.list_imported:
        marker = "/" + imported.source_folder + "/"
        if marker not in "/" + old_id or marker not in "/" + new_id:
            continue
        old_imported = ("/" + old_id).replace(marker, "/" + imported.folder + "/", 1)[1:]
        new_imported = ("/" + new_id).replace(marker, "/" + imported.folder + "/", 1)[1:]
        if not is_folder:
            if not old_id.lower().endswith(imported.list_source_extension) or not new_id.lower().endswith(imported.list_source_extension):
                continue
            old_imported = old_imported.rsplit(".", 1)[0]
            new_imported = new_imported.rsplit(".", 1)[0]
        list_rule.append((old_imported.lower(), new_imported.lower(), True))   # 산출물은 `<이름>.mesh` · `<이름>/…` — 접두로 바꾼다
    return list_rule


def rewrite_path(path: str, list_rule: list[tuple[str, str, bool]]) -> str | None:
    for old_prefix, new_prefix, is_prefix in list_rule:
        if path == old_prefix:
            return new_prefix
        if is_prefix and (path.startswith(old_prefix + "/") or path.startswith(old_prefix + ".")):
            return new_prefix + path[len(old_prefix):]
    return None


def rewrite_references(relative_path: str, text: str, list_rule: list[tuple[str, str, bool]], config: FieldConfig) -> tuple[str, int]:
    """참조 칸의 값 가운데 규칙에 맞는 낱말만 바꿉니다. 뒤에서부터 바꿔 앞 자리가 밀리지 않게 한다."""
    list_edit = []
    for reference in extract_references(relative_path, text, config):
        if (rewritten := rewrite_path(reference.path, list_rule)) is not None:
            list_edit.append((reference.start, reference.end, rewritten))
    for start, end, rewritten in sorted(list_edit, reverse=True):
        text = text[:start] + rewritten + text[end:]
    return text, len(list_edit)


def git_move(root: Path, old_path: str, new_path: str) -> None:
    (root / new_path).parent.mkdir(parents=True, exist_ok=True)
    for _ in range(20):  # IDE 의 git 이 잠깐 잡는 index.lock 은 기다리면 풀린다 — 반쯤 옮긴 채 멈추지 않게
        result = run_git(["mv", old_path, new_path], cwd=root)
        if result.succeeded or "index.lock" not in result.stderr:
            break
        time.sleep(0.5)
    if not result.succeeded:
        raise RuntimeError(f"git mv {old_path} {new_path} 실패: {result.stderr.strip()}")
    folder = (root / old_path).parent
    content = root / CONTENT_FOLDER
    while folder != content and folder.is_dir() and not any(folder.iterdir()):   # 비워진 옛 폴더를 남기지 않는다
        folder.rmdir()
        folder = folder.parent


def list_moved_files(root: Path, old_id: str, new_id: str, is_folder: bool) -> list[tuple[str, str]]:
    """옮긴 파일마다 (옛 id, 새 id) — 폴더면 옮긴 뒤 그 아래 추적 파일 전부. 표 파일 자신은 리다이렉트할 것이 아니다."""
    if not is_folder:
        return [(old_id, new_id)]
    prefix = f"{CONTENT_FOLDER}/{new_id}/"
    return [(old_id + "/" + path[len(prefix):], path[len(CONTENT_FOLDER) + 1:]) for path in list_index_files(root)
            if path.startswith(prefix) and not path.endswith("/" + REDIRECT_FILE_NAME)]


def write_redirects(root: Path, list_move: list[tuple[str, str]]) -> list[str]:
    """옛 id 도메인의 표마다 줄을 더해 쓰고 `git add` 한다. 쓴 표 경로 목록을 돌려준다."""
    map_move_by_table: dict[str, list[tuple[str, str]]] = {}
    for move in list_move:
        map_move_by_table.setdefault(make_redirect_table_id(move[0]), []).append(move)
    list_written = []
    for table_id, list_table_move in map_move_by_table.items():
        table_path = root / CONTENT_FOLDER / table_id
        text = add_redirects(table_path.read_text(encoding="utf-8") if table_path.is_file() else None, list_table_move)
        table_path.parent.mkdir(parents=True, exist_ok=True)
        table_path.write_text(text, encoding="utf-8", newline="\n")
        result = run_git(["add", f"{CONTENT_FOLDER}/{table_id}"], cwd=root)
        if not result.succeeded:
            raise RuntimeError(f"git add {CONTENT_FOLDER}/{table_id} 실패: {result.stderr.strip()}")
        list_written.append(f"{CONTENT_FOLDER}/{table_id}")
    return list_written


def report_leftovers(root: Path, old_id: str) -> None:
    """칸 밖에 옛 경로가 남은 추적 글 파일(주석 · 출처 표 · 문서)을 알립니다. 고치지 않는다 — 경로인지 사람이 읽고 정한다."""
    lowered = old_id.lower()
    list_leftover = []
    for relative_path in list_index_files(root):
        if not relative_path.lower().endswith(LEFTOVER_SUFFIX) or relative_path.endswith("/" + REDIRECT_FILE_NAME) or not (root / relative_path).is_file():
            continue   # 리다이렉트 표는 옛 id 를 적으려고 있다
        if lowered in (root / relative_path).read_bytes().decode("utf-8", "replace").lower():
            list_leftover.append(relative_path)
    for relative_path in list_leftover:
        print(f"  [남은 옛 경로 — 칸 밖] {relative_path}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts move-asset", description="에셋을 옮기고 경로 칸의 참조를 고친다")
    parser.add_argument("old", help="옛 리소스 id(파일 또는 폴더)")
    parser.add_argument("new", help="새 리소스 id")
    parser.add_argument("--dry-run", action="store_true", help="고칠 파일만 찍는다")
    args = parser.parse_args(argv)

    root = get_project_root()
    config = load_config(root)
    old_id, new_id = to_asset_id(args.old), to_asset_id(args.new)
    old_path, new_path = f"{CONTENT_FOLDER}/{old_id}", f"{CONTENT_FOLDER}/{new_id}"
    if not set(new_id) <= NAME_CHARACTERS:
        print(f"[move-asset] '{new_id}' 는 리소스 이름 규칙(소문자 · 숫자 · '_' · '-' · '.' · '/')에 맞지 않습니다")
        return ExitCode.CHECK_INVALID
    if not (root / old_path).exists():
        print(f"[move-asset] '{old_path}' 가 없습니다")
        return ExitCode.CHECK_INVALID
    if (root / new_path).exists():
        print(f"[move-asset] '{new_path}' 가 이미 있습니다 — 아무것도 옮기지 않았습니다")
        return ExitCode.CHECK_INVALID
    is_folder = (root / old_path).is_dir()
    list_rule = make_rules(old_id, new_id, is_folder, config)

    if not args.dry_run:
        git_move(root, old_path, new_path)
    fixed_count = 0
    for relative_path in list_index_files(root):
        if not is_referrer(relative_path, config) or not (root / relative_path).is_file():
            continue
        text = (root / relative_path).read_bytes().decode("utf-8", "surrogateescape")
        rewritten, edit_count = rewrite_references(relative_path, text, list_rule, config)
        if edit_count == 0:
            continue
        fixed_count += 1
        print(f"  {relative_path}: {edit_count}")
        if not args.dry_run:
            (root / relative_path).write_bytes(rewritten.encode("utf-8", "surrogateescape"))
    print(f"[move-asset] {old_id} -> {new_id}: 참조 파일 {fixed_count} 개{' (dry run — 아무것도 바꾸지 않았다)' if args.dry_run else ''}")
    if not args.dry_run:
        for table_path in write_redirects(root, list_moved_files(root, old_id, new_id, is_folder)):
            print(f"  [리다이렉트] {table_path}")
        report_leftovers(root, old_id)
    if args.dry_run:
        return ExitCode.PASSED

    list_problem = check_working_tree(root)
    for problem in list_problem:
        print(f"  - {problem}")
    if list_problem:
        print(f"[move-asset] 끊어진 참조 {len(list_problem)}건이 남았습니다 — 목록에 없는 칸이면 그 칸을 읽는 구조체에 PROPERTY( AssetPath ) 로 선언한다(Config/Dev/AssetReferenceFields.generated.json)")
        return ExitCode.VIOLATION
    return ExitCode.PASSED
