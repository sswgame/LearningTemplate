#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/folder_layout.py

파일 자리 규칙(`docs/agents/FolderLayout.md` 4 절)을 경로로 막는다 — 표는 `Config/Rules/Lint/FolderLayout.json`.
  1. 잡동사니 폴더 이름(`Util` · `Utility` · `Helpers` · `Misc` …) 금지 — 하는 일(Encoding · Algorithm · Hash …)로 묶는다.
  2. 기능 폴더의 `Component/` 에는 컴포넌트(`*Component` · 허용 끝말)만 — 씬 서비스 · 질의 · 검사는 `<기능>/Scene/`.

**왜 린트인가.** 폴더 자리는 형식이 말하지 못한다. 기준표 없이 "처음 쓰는 쪽" · 이름 낱말로 자리를 정해 RegistrationList(String 폴더) · StringUtil(Container) 같은
어긋남이 쌓였다(FolderMoves 13 절, 2026-11-08 전수 조사). 경로만 본다.
`Dev/` 폴더 자리는 린트가 아니라 configure 가 막는다(`cmake/Engine/ExcludedSources.cmake`, 표 `Config/Rules/Layers/DevFolders.json`) — 배포가 빼는 개발 폴더 목록과 한 표다.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.lint_scope import LintScope, make_whole_scope  # noqa: E402

CONFIG_PATH = "Config/Rules/Lint/FolderLayout.json"


def check_folder_layout(project_root: Path, scope: LintScope) -> list[str]:
    return find_problems(scope.select(("Source",)), json.loads((project_root / CONFIG_PATH).read_text(encoding="utf-8")))


def find_problems(list_path: list[str], table: dict) -> list[str]:
    banned = {name.lower(): reason for name, reason in table["_bannedFolderNames"].items()}
    component_roots = tuple(table["_componentFolderModel"])
    component_suffixes = tuple(table["_componentSuffixes"])
    list_problem: list[str] = []
    seen_folder: set[str] = set()
    for relative in list_path:
        parts = relative.split("/")
        for index, folder in enumerate(parts[:-1]):
            prefix = "/".join(parts[: index + 1]) + "/"
            if prefix in seen_folder:
                continue
            if folder.lower() in banned:
                seen_folder.add(prefix)
                list_problem.append(f"{prefix}: 잡동사니 폴더 '{folder}' — {banned[folder.lower()]}")
        if "Component" in parts[:-1] and "Tests" not in parts and not relative.startswith(component_roots):
            stem = parts[-1].split(".")[0]
            if not stem.endswith(component_suffixes):
                list_problem.append(f"{relative}: `Component/` 에는 컴포넌트만 — 씬 서비스 · 질의 · 검사는 `<기능>/Scene/`(FolderLayout 4 절)")
    return list_problem


def run_self_check() -> None:
    table = {"_bannedFolderNames": {"Utils": "기능 이름으로"}, "_componentFolderModel": ["Source/GameFramework/Runtime/"], "_componentSuffixes": ["Component"]}
    list_problem = find_problems(["Source/Engine/Utils/A.h", "Source/Engine/utils/B.h", "Source/Engine/X/Component/FooComponent.h",
                                "Source/Engine/X/Component/FooQuery.h", "Source/GameFramework/Runtime/Component/Any.h", "Source/Engine/X/Component/Tests/T.cpp"], table)
    assert list_problem == ["Source/Engine/Utils/: 잡동사니 폴더 'Utils' — 기능 이름으로", "Source/Engine/utils/: 잡동사니 폴더 'utils' — 기능 이름으로",
                           "Source/Engine/X/Component/FooQuery.h: `Component/` 에는 컴포넌트만 — 씬 서비스 · 질의 · 검사는 `<기능>/Scene/`(FolderLayout 4 절)"], list_problem


def main(list_argument: list[str] | None = None) -> int:  # `py -3 -m Scripts lint folder-layout [--self-check]` 가 인자 목록을 넘긴다
    if list_argument and list_argument[0] == "--self-check":
        run_self_check()
        print("[FolderLayout] self-check OK")
        return ExitCode.PASSED
    list_problem = check_folder_layout(get_project_root(), make_whole_scope(get_project_root()))
    for problem in list_problem:
        print(f"  - {problem}")
    print(f"[FolderLayout] {'OK' if not list_problem else f'FAIL {len(list_problem)}건'}")
    return ExitCode.PASSED if not list_problem else ExitCode.VIOLATION


if __name__ == "__main__":
    sys.exit(main())
