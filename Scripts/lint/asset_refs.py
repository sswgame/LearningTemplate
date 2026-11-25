"""
Scripts/lint/asset_refs.py

끊어진 에셋 참조 린트 규칙과 작업 트리를 보는 명령 — 참조 칸 읽기 · 풀기는 `asset_references.py` 다.
"""

from __future__ import annotations

import argparse
from typing import Any, Mapping

from common.command import Command, ScriptContext
from common.exit_code import ExitCode
from lint.asset_identity import RULES_PATH
from lint.asset_references import AssetSet, CONFIG_PATH, CONTENT_FOLDER, GENERATED_PATH, Reference, check_working_tree, collect_reference_problems, extract_references, is_checked_path, is_referrer, load_config, read_index_blobs, resolves
from lint.index_cache import Facts, get_index_keys, open_facts
from lint.lint_rule import LintRule
from lint.lint_scope import LintScope


class AssetRefsRule(LintRule):
    id = "asset-refs"
    group = "참조"
    blocks = "끊어진 에셋 경로 · 옮긴 에셋의 옛 id(지우거나 옮긴 에셋을 가리키던 다른 파일도)"
    why_not_type = "에셋 칸은 AssetRef<T> · DataPath 이고 저장 꼴은 GUID 다(글 칸에 경로 표시를 다는 것은 파서 오류) — 글로 남는 참조(CMake · 명령 줄 · 정체성 없는 폴더 · 작업 사본)가 있는지는 형식이 말하지 못한다(고치는 것은 `py -3 -m Scripts move-asset`)"
    inputs = ("Config/Dev/AssetReferenceFields.json", "Config/Dev/AssetReferenceFields.generated.json", "Config/Dev/AssetIdentity.json")

    def evaluate(self, scope: LintScope, found: Mapping[str, object]) -> list[str]:
        """
        인덱스 전체의 끊어진 참조 — 지우거나 옮긴 에셋을 가리키던 다른 파일(역참조)까지 잡으려고 판정은 늘 전체로 낸다.
        참조 파일마다 뽑은 경로는 블롭 id 를 키로 한 색인(`index_cache`)에 두어 바뀐 파일만 다시 읽는다. 커밋 훅은 Content · CMake 참조 파일이 바뀌었을 때만 돈다
        (칸 목록이 바뀌면 `inputs` 가 저장소 전체로 돌린다).
        """
        config = load_config(self.root)
        if not scope.whole_repo and not scope.touches((CONTENT_FOLDER,), files=config.list_cmake_file):
            return []
        map_key = get_index_keys(self.root)
        list_index = list(map_key)
        prefix = CONTENT_FOLDER + "/"
        assets = AssetSet(path[len(prefix):] for path in list_index if path.startswith(prefix))
        list_referrer = [path for path in list_index if is_referrer(path, config)]
        def open_references(**options: Any) -> Facts:
            return open_facts(self.root, "asset-refs", [__file__, str(self.root / CONFIG_PATH), str(self.root / GENERATED_PATH), str(self.root / RULES_PATH)], list_referrer,
                             lambda path, text: [[reference.line, reference.field, reference.start, reference.end, reference.path] for reference in extract_references(path, text, config)],
                             map_key=map_key, read_texts=lambda list_path: read_index_blobs(self.root, list_path), contribute=lambda _, list_row: [row[4] for row in list_row], **options)

        # 판정은 참조한 경로만으로 난다(없는 경로) — 요약(경로마다 참조 수)으로 거르고, 걸린 것이 있을 때만 사실 전체를 읽어 줄 번호를 낸다.
        list_failing = [path for path in open_references().summary if is_checked_path(path, config) and not resolves(path, assets, config)]
        if not list_failing:
            return []
        map_fact = open_references(want=list_referrer).map_fact   # 걸린 때만 — 줄 번호를 내려고 모든 조각을 읽는다
        map_reference = {path: [Reference(*row) for row in list_row] for path, list_row in map_fact.items()}
        return collect_reference_problems(map_reference, assets, config)


class AssetRefsCommand(Command):
    name = "asset-refs"
    help = "작업 트리의 에셋 참조가 모두 있는지 본다(커밋 전 검사 · App --check-asset-refs 와 같은 칸 목록)"

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        list_problem = check_working_tree(context.root)
        for problem in list_problem:
            print(f"  - {problem}")
        print(f"[AssetReferences] {'FAIL' if list_problem else 'OK'} — 끊어진 참조 {len(list_problem)}건")
        return ExitCode.VIOLATION if list_problem else ExitCode.PASSED
