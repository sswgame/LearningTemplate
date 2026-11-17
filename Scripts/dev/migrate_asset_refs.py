"""
에셋 참조 칸(`AssetRef<T>`)의 옛 경로 글을 GUID 참조(`guid:<32자>`)로 한 번에 바꿔 쓴다 — 에셋 GUID 2 단계의 전수 이전(확장 → **이전** → 수축).

  py -3 -m Scripts migrate-asset-refs          # 바꿀 참조 수와 못 바꾸는 참조만 찍는다(아무것도 쓰지 않는다)
  py -3 -m Scripts migrate-asset-refs --fix    # 바꿔 쓴다(못 바꾸는 참조가 하나라도 있으면 아무것도 쓰지 않는다)

- 어느 칸이 `AssetRef<T>` 인가는 리플렉션 파서가 낸 표(`Config/Dev/AssetReferenceFields.generated.json` 의 주인마다 `listAssetRefField`)가 정한다.
  `DataPath` · `PROPERTY( AssetPath )` 글 칸(`listPathTextField` — `_base` 등)은 경로 글 그대로 둔다. 같은 이름이 두 꼴로 쓰이면 글만으로 가를 수 없어 멈춘다(칸 이름을 바꾼다).
  꼴별 칸이 없는 주인(그 게임을 아직 새 파서로 짓지 않았다)이 있으면 멈춘다 — 모르는 칸을 경로로 남기거나 짐작해 바꾸지 않는다.
- 경로 → GUID 는 저작 색인(작업 트리의 머리 `<?asset guid?>` · `.meta`, `asset-ids` 와 같은 규칙 · C++ `AssetDatabase`)으로 찾는다. 색인에 결함(겹친 GUID · 머리 없음)이
  있으면 `asset-ids --fix` 먼저. 색인에 없는 경로는 오류다 — 단, 임포트 산출물(`models/x.mesh` — 원본 GUID → 산출물 경로 줄은 아직 없다)은 경로 글로 남기고 수를 찍는다.
- 런타임은 옮기는 동안 두 꼴을 다 읽는다(`AssetRefBase::assign`). 이 도구를 돌린 뒤 경로 글 읽기를 걷는다(수축 — docs/NEXT.md).
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass, field
from pathlib import Path

from common.exit_code import ExitCode
from common.paths import get_project_root
from lint.asset_identity import collect_issues, list_worktree_ids, load_rules, read_worktree_texts
from lint.asset_references import CONTENT_FOLDER, GENERATED_PATH, FieldConfig, extract_references, is_referrer, list_index_files, load_config

GUID_PREFIX = "guid:"   # C++ `AssetRegistry::kReferencePrefix` 와 같은 머리


@dataclass(frozen=True)
class RefFieldTable:
    """GUID 로 쓰는 칸 이름(`AssetRef<T>`)과, 그 표를 믿을 수 없게 만드는 것들."""

    set_ref_field: frozenset[str]
    list_owner_without_kind: tuple[str, ...]   # 꼴별 칸이 없는 주인 — 그 구성을 새 파서로 지어야 한다
    list_conflict: tuple[str, ...]             # 한 이름이 `AssetRef<T>` 칸이기도 경로 글 칸이기도 하다


@dataclass
class MigrationResult:
    """글 하나를 바꾼 결과."""

    text: str
    rewrite_count: int = 0
    derived_count: int = 0                                 # 임포트 산출물을 가리켜 경로 글로 남긴 참조
    list_unresolved: list[str] = field(default_factory=list)


def load_ref_field_table(generated: dict) -> RefFieldTable:
    """파서가 낸 표의 꼴별 칸을 모읍니다. 표 꼴이 틀리면 예외입니다."""
    set_ref: set[str] = set()
    set_path_text: set[str] = set()
    list_missing: list[str] = []
    for owner in generated["listOwner"]:
        if "listAssetRefField" not in owner or "listPathTextField" not in owner:
            list_missing.append(owner["owner"] or "(engine)")
            continue
        set_ref.update(owner["listAssetRefField"])
        set_path_text.update(owner["listPathTextField"])
    return RefFieldTable(frozenset(set_ref), tuple(list_missing), tuple(sorted(set_ref & set_path_text)))


def is_derived(asset_id: str, list_derived_folder: tuple[str, ...]) -> bool:
    """임포트 산출물 폴더 아래(`.../models/...`)면 True — 정체성은 원본의 것이고 GUID → 산출물 경로 줄은 아직 없다."""
    return any(f"/{folder}/" in f"/{asset_id.lower()}" for folder in list_derived_folder)


def migrate_text(relative_path: str, text: str, config: FieldConfig, set_ref_field: frozenset[str], map_path_to_guid: dict[str, str],
                 list_derived_folder: tuple[str, ...]) -> MigrationResult:
    """글 하나의 `AssetRef<T>` 칸 경로를 GUID 참조로 바꿉니다. 이미 GUID 인 값 · 다른 칸은 그대로 둡니다. 뒤에서부터 바꿔 앞 자리가 밀리지 않습니다."""
    result = MigrationResult(text)
    list_reference = sorted(extract_references(relative_path, text, config), key=lambda reference: reference.start, reverse=True)
    for reference in list_reference:
        if reference.field not in set_ref_field or text[reference.start:reference.end].startswith(GUID_PREFIX):
            continue
        guid = map_path_to_guid.get(reference.path)
        if guid is None:
            if is_derived(reference.path, list_derived_folder):
                result.derived_count += 1
            else:
                result.list_unresolved.append(f"{relative_path}:{reference.line}: {reference.field}=\"{reference.path}\" 는 정체성 색인에 없다")
            continue
        result.text = result.text[:reference.start] + GUID_PREFIX + guid + result.text[reference.end:]
        result.rewrite_count += 1
    return result


def build_path_to_guid(project_root: Path) -> tuple[dict[str, str], list[str]]:
    """작업 트리의 저작 색인 — 소문자 리소스 id → GUID, 그리고 결함 줄."""
    rules = load_rules(project_root)
    list_issue, map_guid = collect_issues(read_worktree_texts(project_root, list_worktree_ids(project_root), rules), rules)
    return {asset_id.lower(): guid for guid, asset_id in map_guid.items()}, [str(issue) for issue in list_issue]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts migrate-asset-refs", description="AssetRef<T> 칸의 경로 글을 guid:<32자> 로 바꿔 쓴다")
    parser.add_argument("--fix", action="store_true", help="바꿔 쓴다(없으면 찍기만 한다)")
    args = parser.parse_args(argv)

    root = get_project_root()
    ref_field_table = load_ref_field_table(json.loads((root / GENERATED_PATH).read_text(encoding="utf-8")))
    if ref_field_table.list_owner_without_kind:
        print(f"[migrate-asset-refs] {GENERATED_PATH} 의 주인 {', '.join(ref_field_table.list_owner_without_kind)} 에 꼴별 칸(listAssetRefField)이 없다 — 그 구성을 지어 표를 갱신한 뒤 다시 돈다")
        return ExitCode.CHECK_INVALID
    if ref_field_table.list_conflict:
        print(f"[migrate-asset-refs] 이 이름은 AssetRef<T> 칸이기도 경로 글 칸이기도 해 글로 가를 수 없다 — 한쪽 칸 이름을 바꾼다: {', '.join(ref_field_table.list_conflict)}")
        return ExitCode.CHECK_INVALID
    map_path_to_guid, list_issue = build_path_to_guid(root)
    if list_issue:
        for issue in list_issue[:20]:
            print(f"  - {issue}")
        print(f"[migrate-asset-refs] 정체성 결함 {len(list_issue)}건 — `py -3 -m Scripts asset-ids --fix` 먼저")
        return ExitCode.CHECK_INVALID
    config = load_config(root)
    list_derived_folder = load_rules(root).list_derived_folder

    map_rewritten: dict[str, str] = {}
    list_unresolved: list[str] = []
    rewrite_count = 0
    derived_count = 0
    for relative_path in list_index_files(root):
        if not relative_path.startswith(CONTENT_FOLDER + "/") or not is_referrer(relative_path, config) or not (root / relative_path).is_file():
            continue
        text = (root / relative_path).read_bytes().decode("utf-8", "surrogateescape")
        result = migrate_text(relative_path, text, config, ref_field_table.set_ref_field, map_path_to_guid, list_derived_folder)
        list_unresolved.extend(result.list_unresolved)
        derived_count += result.derived_count
        if result.rewrite_count:
            rewrite_count += result.rewrite_count
            map_rewritten[relative_path] = result.text
    for line in list_unresolved:
        print(f"  - {line}")
    print(f"[migrate-asset-refs] 파일 {len(map_rewritten)} 개 · 참조 {rewrite_count} 개를 GUID 로, 임포트 산출물 참조 {derived_count} 개는 경로 글로 남김, 못 바꾸는 참조 {len(list_unresolved)} 개")
    if list_unresolved:
        print("[migrate-asset-refs] 못 바꾸는 참조가 있어 아무것도 쓰지 않았다 — 끊어진 참조를 고치거나 대상에 정체성을 준다(`asset-ids --fix`)")
        return ExitCode.VIOLATION
    if not args.fix:
        print("[migrate-asset-refs] --fix 없이 돌았다 — 아무것도 쓰지 않았다")
        return ExitCode.PASSED
    for relative_path, text in map_rewritten.items():
        (root / relative_path).write_bytes(text.encode("utf-8", "surrogateescape"))
    return ExitCode.PASSED
