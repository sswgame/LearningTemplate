"""
에셋 참조 칸(`AssetRef<T>` · `DataPath` · `_base`)의 경로 글을 GUID 참조(`guid:<32자>`)로 한 번에 바꿔 쓴다 — 에셋 GUID 2 단계의 전수 이전.

  py -3 -m Scripts migrate-asset-refs          # 바꿀 참조 수와 못 바꾸는 참조만 찍는다(아무것도 쓰지 않는다)
  py -3 -m Scripts migrate-asset-refs --fix    # 바꿔 쓴다(못 바꾸는 참조가 하나라도 있으면 아무것도 쓰지 않는다)

- 어느 칸이 `AssetRef<T>` 인가는 리플렉션 파서가 낸 표(`Config/Dev/AssetReferenceFields.generated.json` 의 주인마다 `listAssetRefField`)가 정한다.
  `PROPERTY( AssetPath )` 글 칸(`listPathTextField`)은 경로 글 그대로 둔다. 같은 이름이 두 꼴로 쓰이면 글만으로 가를 수 없어 멈춘다(칸 이름을 바꾼다).
  꼴별 칸이 없는 주인(그 게임을 아직 새 파서로 짓지 않았다)이 있으면 멈춘다 — 모르는 칸을 경로로 남기거나 짐작해 바꾸지 않는다.
- `DataPath` 칸(`listDataPathField`)은 이름이 경로 글 칸 · 글 칸과 겹치지 않을 때만 바꾼다 — 읽기가 경로 글도 받으므로 겹친 이름은 남겨도 된다(멈추지 않는다).
  `_base` 는 상속의 파일 부분(`DataInheritance::resolveBaseText` — `guid:<32자>#<항목 id>`)이라 표와 상관없이 바꾼다. 두 꼴 다 색인에 없는 경로(팩 뿌리 폴더)는 경로 글로 남긴다.
- 표는 빌드가 낸다(파서) — 칸 꼴을 바꾼 커밋은 그 구성을 지은 뒤 이 도구를 돌린다. 도구 자신은 빌드 · App 을 부르지 않는다.
- 경로 칸이 아닌 글 칸(`listTextField` — `string` · `hashed_string`)도 표에 있다. 같은 이름이 어떤 주인에서는 `AssetRef<T>` 칸이고 다른 주인에서는 글 칸이면
  그 이름의 **경로 값**은 어느 칸의 것인지 글로 가를 수 없어 오류로 멈춘다(그 글 칸이 경로를 담으면 GUID 글을 경로로 받는다). 경로가 아닌 값(물리 재질 이름)은 바꾸지 않으므로 겹쳐도 된다.
  고치는 길: 글 칸이 에셋을 가리키면 `AssetRef<T>` 로 바꾸고, 아니면 이름을 가른다.
- 리플렉션 밖에서 손으로 읽는 칸(알림 인자 · JSON 키)은 표가 모른다. 그런 읽기는 칸의 이름 · 꼴을 리플렉션 구조체로 선언하고 그 구조체로 값을 받는다
  (`SceneEntityParams` · `PrefabInstanceParams` · `AnimationNotifySpawnPrefabParams`) — 선언이 표에 오르므로 위 검사가 그 칸도 본다.
- 경로 → GUID 는 저작 색인(작업 트리의 머리 `<?asset guid?>` · `.meta`, `asset-ids` 와 같은 규칙 · C++ `AssetDatabase`)으로 찾는다. 임포트 산출물(`models/x.mesh`)은 원본 GUID 와
  산출물 이름에서 난 GUID 다(`find_output_guid` — 산출물이 디스크에 없어도 된다). 색인에 결함(겹친 GUID · 머리 없음)이 있으면 `asset-ids --fix` 먼저. `AssetRef<T>` 칸의 색인에 없는 경로는 오류다.
- 읽기는 두 꼴을 다 받는다(`AssetRegistry::readReferenceText`). 다시 돌려도 이미 GUID 인 값은 그대로다 — 병합 뒤 새로 들어온 경로 글에 다시 돌린다.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass, field
from pathlib import Path

from common.exit_code import ExitCode
from common.paths import get_project_root
from lint.asset_identity import ImportedFolder, collect_issues, find_output_guid, list_worktree_ids, load_rules, read_worktree_texts
from lint.asset_references import CONTENT_FOLDER, GENERATED_PATH, FieldConfig, extract_references, is_referrer, list_index_files, load_config

GUID_PREFIX = "guid:"   # C++ `AssetRegistry::kReferencePrefix` 와 같은 머리
BASE_FIELD = "_base"    # C++ `DataInheritance::kBaseAttribute`


@dataclass(frozen=True)
class RefFieldTable:
    """GUID 로 쓰는 칸 이름(`AssetRef<T>`)과, 그 표를 믿을 수 없게 만드는 것들."""

    set_ref_field: frozenset[str]
    list_owner_without_kind: tuple[str, ...]   # 꼴별 칸이 없는 주인 — 그 구성을 새 파서로 지어야 한다
    list_conflict: tuple[str, ...]             # 한 이름이 `AssetRef<T>` 칸이기도 경로 글 칸이기도 하다
    set_ambiguous_field: frozenset[str]        # 한 이름이 `AssetRef<T>` 칸이기도 경로가 아닌 글 칸이기도 하다 — 경로 값을 이름으로 가를 수 없다
    set_data_field: frozenset[str]             # `DataPath` 칸 이름(다른 꼴과 겹치지 않는 것)


@dataclass
class MigrationResult:
    """글 하나를 바꾼 결과."""

    text: str
    rewrite_count: int = 0
    kept_count: int = 0                                    # 색인에 없어 경로 글로 남긴 `DataPath` · `_base` 값
    list_unresolved: list[str] = field(default_factory=list)
    list_ambiguous: list[str] = field(default_factory=list)   # 참조 칸이기도 글 칸이기도 한 이름의 경로 값 — 바꾸지 않고 멈춘다


def load_ref_field_table(generated: dict) -> RefFieldTable:
    """파서가 낸 표의 꼴별 칸을 모읍니다. 표 꼴이 틀리면 예외입니다."""
    set_ref: set[str] = set()
    set_path_text: set[str] = set()
    set_text: set[str] = set()
    set_data: set[str] = set()
    list_missing: list[str] = []
    for owner in generated["listOwner"]:
        if "listAssetRefField" not in owner or "listPathTextField" not in owner or "listTextField" not in owner:
            list_missing.append(owner["owner"] or "(engine)")
            continue
        set_ref.update(owner["listAssetRefField"])
        set_path_text.update(owner["listPathTextField"])
        set_text.update(owner["listTextField"])
        set_data.update(owner.get("listDataPathField", ()))   # 옛 파서의 줄은 그 이름이 listPathTextField 에 있다 — 겹친 이름으로 남겨진다
    return RefFieldTable(frozenset(set_ref), tuple(list_missing), tuple(sorted(set_ref & set_path_text)), frozenset(set_ref & set_text),
                         frozenset(set_data - set_path_text - set_text))


def migrate_text(relative_path: str, text: str, config: FieldConfig, ref_field_table: RefFieldTable, map_path_to_guid: dict[str, str],
                 list_imported: tuple[ImportedFolder, ...]) -> MigrationResult:
    """글 하나의 참조 칸 경로를 GUID 참조로 바꿉니다. 이미 GUID 인 값 · 다른 칸은 그대로 둡니다. 뒤에서부터 바꿔 앞 자리가 밀리지 않습니다.
    참조 칸이기도 글 칸이기도 한 이름의 경로 값은 바꾸지 않고 `list_ambiguous` 에 적습니다."""
    result = MigrationResult(text)
    list_reference = sorted(extract_references(relative_path, text, config), key=lambda reference: reference.start, reverse=True)
    for reference in list_reference:
        is_ref = reference.field in ref_field_table.set_ref_field
        if not is_ref and reference.field != BASE_FIELD and reference.field not in ref_field_table.set_data_field:
            continue
        if reference.field in ref_field_table.set_ambiguous_field:
            result.list_ambiguous.append(f"{relative_path}:{reference.line}: {reference.field}=\"{reference.path}\" — 이 이름은 참조 칸이기도 글 칸이기도 하다")
            continue
        guid = map_path_to_guid.get(reference.path) or find_output_guid(reference.path, map_path_to_guid, list_imported)
        if guid is None:
            if not is_ref:
                result.kept_count += 1
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
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts migrate-asset-refs", description="AssetRef<T> · DataPath · _base 의 경로 글을 guid:<32자> 로 바꿔 쓴다")
    parser.add_argument("--fix", action="store_true", help="바꿔 쓴다(없으면 찍기만 한다 — 마른 실행)")
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

    map_rewritten: dict[str, str] = {}
    list_unresolved: list[str] = []
    list_ambiguous: list[str] = []
    rewrite_count = 0
    kept_count = 0
    for relative_path in list_index_files(root):
        if not relative_path.startswith(CONTENT_FOLDER + "/") or not is_referrer(relative_path, config) or not (root / relative_path).is_file():
            continue
        text = (root / relative_path).read_bytes().decode("utf-8", "surrogateescape")
        result = migrate_text(relative_path, text, config, ref_field_table, map_path_to_guid, config.list_imported)
        list_unresolved.extend(result.list_unresolved)
        list_ambiguous.extend(result.list_ambiguous)
        kept_count += result.kept_count
        if result.rewrite_count:
            rewrite_count += result.rewrite_count
            map_rewritten[relative_path] = result.text
    for line in list_unresolved + list_ambiguous:
        print(f"  - {line}")
    print(f"[migrate-asset-refs] 파일 {len(map_rewritten)} 개 · 참조 {rewrite_count} 개를 GUID 로, 색인에 없는 데이터 경로 · _base {kept_count} 개는 경로 글로 남김, 못 바꾸는 참조 {len(list_unresolved)} 개")
    if list_unresolved:
        print("[migrate-asset-refs] 못 바꾸는 참조가 있어 아무것도 쓰지 않았다 — 끊어진 참조를 고치거나 대상에 정체성을 준다(`asset-ids --fix`)")
        return ExitCode.VIOLATION
    if list_ambiguous:
        print(f"[migrate-asset-refs] 참조 칸이기도 글 칸이기도 한 이름의 경로 값 {len(list_ambiguous)} 개 — 아무것도 쓰지 않았다. "
              f"그 이름의 글 칸을 AssetRef<T> 로 바꾸거나 이름을 가른다: {', '.join(sorted(ref_field_table.set_ambiguous_field))}")
        return ExitCode.VIOLATION
    if not args.fix:
        print("[migrate-asset-refs] --fix 없이 돌았다 — 아무것도 쓰지 않았다")
        return ExitCode.PASSED
    for relative_path, text in map_rewritten.items():
        (root / relative_path).write_bytes(text.encode("utf-8", "surrogateescape"))
    return ExitCode.PASSED
