"""
Scripts/lint/format.py

C++ · 셰이더 소스와 vcpkg 매니페스트를 고친다 — 고치는 명령은 `format` 하나다: clang-format(include 순서 · 묶음도 `.clang-format` 의 IncludeCategories 가 정한다)과
`vcpkg format-manifest`(모든 `vcpkg.json` — 저장소 매니페스트 · 오버레이 포트). 린트의 format 안내도 이 명령을 가리킨다.
대상은 `--files` > `--all` > git 변경분 > 전체다 — 변경분이 없으면 전체를 본다(처음 받은 저장소에서도 한 번에 맞춰진다). 줄 끝 · BOM 은 건드리지 않는다.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from common.command import Command, ScriptContext
from lint.format_files import run_clang_format_batch, run_vcpkg_manifest_format, select_format_files, select_vcpkg_manifests
from lint.lint_scope import LintScope, make_changed_scope, make_file_scope, make_whole_scope


class FormatCommand(Command):
    name = "format"
    help = "clang-format(C++ 는 include 순서 포함, 셰이더) · vcpkg format-manifest(vcpkg.json) — 린트 format 의 고치는 명령"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--files", nargs="*", default=None, help="대상 파일(없으면 git 변경 파일, 그것도 없으면 전체)")
        parser.add_argument("--all", action="store_true", help="저장소 전체")
        parser.add_argument("--check", action="store_true", help="고치지 않고 다른 파일만 알린다")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        scope = self.select_scope(context.root, args)
        manifest_result = run_vcpkg_manifest_format([context.root / path for path in select_vcpkg_manifests(scope)], check_only=args.check)
        list_file = [context.root / path for path in select_format_files(scope)]
        print(f"[format] {len(list_file)}개 파일에 clang-format {'검사' if args.check else '적용'}{' (저장소 전체)' if scope.whole_repo else ''}", flush=True)
        return run_clang_format_batch(list_file, check_only=args.check, cwd=context.root) or manifest_result

    @staticmethod
    def select_scope(root: Path, args: argparse.Namespace) -> LintScope:
        if args.files:
            return make_file_scope(root, args.files)
        changed = None if args.all else make_changed_scope(root)
        return changed if changed is not None and (select_format_files(changed) or select_vcpkg_manifests(changed)) else make_whole_scope(root)
