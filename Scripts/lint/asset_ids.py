"""
Scripts/lint/asset_ids.py

에셋 정체성(GUID) 린트 규칙과 고치는 명령 — 규칙 자체(머리 · .meta 읽기 쓰기, 결함 모으기, 고치기)는 `asset_identity.py` 다.

  py -3 -m Scripts asset-ids              # 작업 트리 검사(결함을 찍는다)
  py -3 -m Scripts asset-ids --fix        # 없는 GUID 를 만들고, 짝 잃은 .meta 를 다시 붙이고, 복사본에 새 GUID 를 준다
  py -3 -m Scripts asset-ids --redirect <지울 id> <남길 id>  # 두 에셋을 합친다 — 지울 쪽 참조가 남길 쪽으로 풀리는 줄을 리다이렉트 표에 적는다
"""

from __future__ import annotations

import argparse
from typing import Mapping

from common.command import Command, ScriptContext
from common.exit_code import ExitCode
from lint.asset_identity import (
    CONTENT_FOLDER, FOUND, GUID_LENGTH, INVALID, MISSING, add_identity_redirect, check_staged_asset_identity, collect_issues, create_guid,
    find_output_guid, fix_worktree, is_guid, iterate_imported_sources, list_worktree_ids, load_rules, make_meta_text, make_output_guid, parse_meta,
    read_header, read_worktree_texts, redirect_identity, write_header,
)
from lint.lint_rule import LintRule
from lint.lint_scope import LintScope


class AssetIdsRule(LintRule):
    id = "asset-ids"
    group = "참조"
    blocks = "에셋 정체성 결함 — 머리 `<?asset guid?>` 없음 · 깨짐, 짝 없는 .meta · .meta 없는 원본, GUID 겹침, HEAD 경로의 GUID 바뀜"
    why_not_type = "정체성은 파일 글(머리 · 곁 .meta)이라 형식이 말하지 못한다(고치는 것은 `py -3 -m Scripts asset-ids --fix`)"
    inputs = ("Config/Dev/AssetIdentity.json",)

    def evaluate(self, scope: LintScope, found: Mapping[str, object]) -> list[str]:
        """커밋 훅은 스테이지의 바뀐 파일과 그 짝만, 저장소 전체면 작업 트리 전체."""
        if not scope.whole_repo:
            return check_staged_asset_identity(self.root, scope) or []
        rules = load_rules(self.root)
        list_issue, _ = collect_issues(read_worktree_texts(self.root, list_worktree_ids(self.root), rules), rules)
        return [str(issue) for issue in list_issue]

    def self_check(self) -> None:
        """C++ `AssetGUIDTest` 와 같은 사례 · 규칙 파일의 산출물 폴더가 참조 칸 표의 임포트 폴더와 같은가."""
        sample = "0123456789abcdef0011223344556677"
        with_declaration = write_header('<?xml version="1.0" encoding="utf-8"?>\n<!-- note -->\n<Root/>\n', sample)
        assert with_declaration == f'<?xml version="1.0" encoding="utf-8"?>\n<?asset guid="{sample}"?>\n<!-- note -->\n<Root/>\n', with_declaration
        assert read_header(with_declaration) == (FOUND, sample)
        assert write_header("<Root/>\r\n", sample) == f'<?asset guid="{sample}"?>\r\n<Root/>\r\n'
        other = create_guid()
        assert read_header(write_header(with_declaration, other)) == (FOUND, other) and len(write_header(with_declaration, other)) == len(with_declaration)
        assert read_header('<?xml version="1.0"?>\n<Root/>')[0] == MISSING
        assert read_header('<?asset guid="nope"?>\n<Root/>')[0] == INVALID
        assert read_header(f'<?asset guid="{sample}" extra="1"?>\n<Root/>')[0] == INVALID
        assert read_header(f'<Root/>\n<?asset guid="{sample}"?>')[0] == MISSING
        assert read_header(f'<?asset guid="{sample}"?>\n<!-- <?asset guid?> -->\n<Root/>') == (FOUND, sample)
        assert read_header('<?xml version="1.0"\n<Root/>')[0] == INVALID
        assert not is_guid(sample.upper()) and not is_guid("0" * GUID_LENGTH) and not is_guid(sample[:-1])
        assert parse_meta(make_meta_text("a/b/hit.ogg", sample)) == (sample, "hit.ogg", ())
        overrides = (("animationCodec", "raw"), ("generateMips", "false"))
        assert parse_meta(make_meta_text("a/horse.glb", sample, overrides)) == (sample, "horse.glb", overrides)
        assert parse_meta(make_meta_text("a/horse.glb", sample, overrides).replace('_value="raw"', '_value="raw" _note="x"')) is None
        assert parse_meta(f'<AssetMeta _guid="{sample}" _source="hit.ogg"><Other /></AssetMeta>') is None
        assert parse_meta(f'<AssetMeta _guid="{sample}" _source="hit.ogg" _extra="1" />') is None
        stored_source = f"store:{'0a' * 32}/horse.glb"   # 저장소 밖 원본 — C++ AssetMetaTest.StoredSourceAddressIsOneOfTwoForms 와 같은 사례
        stored = make_meta_text("a/horse.glb", sample, overrides, stored_source)
        assert parse_meta(stored) == (sample, stored_source, overrides)
        assert parse_meta(make_meta_text("b/pony.glb", sample, overrides, stored_source)) == (sample, f"store:{'0a' * 32}/pony.glb", overrides)
        for wrong in (f"store:{'0A' * 32}/horse.glb", "store:0a/horse.glb", f"store:{'0a' * 32}/", f"store:{'0a' * 32}/a/horse.glb", "a/horse.glb", "..", "C:horse.glb", ""):
            assert parse_meta(f'<AssetMeta _guid="{sample}" _source="{wrong}" />') is None, wrong

        rules = load_rules(self.root)
        assert make_output_guid("0123456789abcdef0011223344556677", ".MESH") == "28016f06802dfbf1a257d90c3347b2fb"   # C++ AssetGUIDTest.OutputIdentityComesFromSourceAndOutputName 과 같은 글
        assert next(iterate_imported_sources("common/models/a/b/c.mesh", rules.list_imported)) == ("common/models_raw/a/b/c.glb", ".mesh")
        map_source = {"common/models_raw/a.glb": sample}
        assert find_output_guid("common/models/a/b/c.mesh", map_source, rules.list_imported) == make_output_guid(sample, "/b/c.mesh")
        assert find_output_guid("common/models/a.mesh", map_source, rules.list_imported) == make_output_guid(sample, ".mesh")
        assert find_output_guid("common/models/_shared/x.material", map_source, rules.list_imported) is None
        assert find_output_guid("common/prefabs/a.prefab.xml", map_source, rules.list_imported) is None
        assert find_output_guid("common/models/a.facial.xml", map_source, rules.list_imported) is None   # glb 는 표정 표를 내지 않는다 — C++ `AssetReferenceFieldsTest.ImportedOutputsResolveFromTheirSource` 와 같은 사례
        assert find_output_guid("common/models/a/a.facial.xml", {"common/models_raw/a.vrm": sample}, rules.list_imported) == make_output_guid(sample, "/a.facial.xml")
        assert find_output_guid("common/models/a/clips", map_source, rules.list_imported) == make_output_guid(sample, "/clips")
        table = add_identity_redirect('<?asset guid="x"?>\n<Redirects>\n</Redirects>\n', sample, other)
        assert table == f'<?asset guid="x"?>\n<Redirects>\n\t<IdentityRedirect from="{sample}" to="{other}" />\n</Redirects>\n', table
        assert add_identity_redirect(table, sample, "0" * 31 + "1").count("IdentityRedirect") == 1   # 같은 옛 GUID 의 줄은 바꾼다
        list_issue, _ = collect_issues({"a/x.prefab.xml": write_header("<P/>", sample), "a/y.prefab.xml": write_header("<P/>", sample), "a/z.ogg": "",
                                      "a/w.ogg.meta": make_meta_text("a/w.ogg", other), "a/golden/q.ppm": ""}, rules)
        misplaced = write_header("<F/>", create_guid())
        kept, _ = collect_issues({"game/x/models/head.facial.xml": misplaced, "game/x/data/head.facial.xml": write_header("<F/>", sample),
                                 "game/x/models/head/head.mirrortable.xml": write_header("<M/>", other), "game/x/models_raw/head.facial.xml": write_header("<F/>", create_guid())}, rules)
        assert [(issue.kind, issue.asset_id) for issue in kept] == [("kept-in-derived", "game/x/models/head.facial.xml")], kept
        assert [(issue.kind, issue.asset_id) for issue in list_issue] == [("orphan-meta", "a/w.ogg.meta"), ("duplicate", "a/y.prefab.xml"), ("missing-meta", "a/z.ogg")], list_issue
        list_issue, map_guid = collect_issues({"a/horse.glb.meta": stored}, rules)   # 원본이 저장소 밖에 있다 — 짝 잃은 서술이 아니다
        assert not list_issue and map_guid == {sample: "a/horse.glb"}, list_issue
        list_issue, _ = collect_issues({"a/horse.glb.meta": stored, "a/horse.glb": ""}, rules)   # 곁에도 있으면 어느 쪽이 원본인지 둘이다
        assert [issue.kind for issue in list_issue] == ["invalid-meta"], list_issue


class AssetIdsCommand(Command):
    name = "asset-ids"
    help = "에셋 정체성(GUID — 우리 형식 글의 머리 <?asset guid?> · 원본 곁 .meta) 검사, --fix 로 없는 GUID 만들기 · 짝 잃은 .meta 다시 붙이기 · 복사본 새 GUID"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--fix", action="store_true", help="없는 GUID 를 만들고, 짝 잃은 .meta 를 다시 붙이고, 복사본에 새 GUID 를 준다")
        parser.add_argument("--redirect", nargs=2, metavar=("지울_id", "남길_id"), help="지울 에셋을 가리키던 참조가 남길 에셋으로 풀리는 줄을 리다이렉트 표에 적는다")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        project_root = context.root
        if args.redirect:
            table_id = redirect_identity(project_root, *args.redirect)
            print(f"[asset-ids] {CONTENT_FOLDER}/{table_id} 에 정체성 대체 줄을 적었다 — 이제 '{args.redirect[0]}' 를 지운다")
            return ExitCode.PASSED
        if args.fix:
            left = fix_worktree(project_root)
            print(f"[asset-ids] {'FAIL' if left else 'OK'} — 남은 결함 {left}건")
            return ExitCode.VIOLATION if left else ExitCode.PASSED
        rules = load_rules(project_root)
        list_issue, map_guid = collect_issues(read_worktree_texts(project_root, list_worktree_ids(project_root), rules), rules)
        for issue in list_issue:
            print(f"  - {issue}")
        print(f"[asset-ids] {'FAIL' if list_issue else 'OK'} — 정체성 {len(map_guid)}개, 결함 {len(list_issue)}건")
        return ExitCode.VIOLATION if list_issue else ExitCode.PASSED
