"""
Scripts/lint/folder_layout.py

파일 자리 규칙(`docs/agents/FolderLayout.md` 4 절)을 경로로 막는다 — 표는 `Config/Rules/Lint/FolderLayout.json`.
  1. 잡동사니 폴더 이름(`Util` · `Utility` · `Helpers` · `Misc` …) 금지 — 하는 일(Encoding · Algorithm · Hash …)로 묶는다.
  2. 기능 폴더의 `Component/` 에는 컴포넌트(`*Component` · 허용 끝말)만 — 씬 서비스 · 질의 · 검사는 `<기능>/Scene/`.

**왜 린트인가.** 폴더 자리는 형식이 말하지 못한다. 기준표 없이 "처음 쓰는 쪽" · 이름 낱말로 자리를 정해 RegistrationList(String 폴더) · StringUtil(Container) 같은
어긋남이 쌓였다(FolderMoves 13 절, 2026-11-08 전수 조사). 경로만 본다.
`Dev/` 폴더 자리는 린트가 아니라 configure 가 막는다(`cmake/Modules/Build/ExcludedSources.cmake`, 표 `Config/Rules/Layers/DevFolders.json`) — 배포가 빼는 개발 폴더 목록과 한 표다.
"""

from __future__ import annotations

from typing import Mapping

from lint.lint_rule import LintRule
from lint.lint_scope import LintScope


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


class FolderLayoutRule(LintRule):
    id = "folder-layout"
    group = "경계"
    blocks = "잡동사니 폴더 · Component 폴더"
    why_not_type = "파일 자리는 형식이 아니다"
    tables = ("",)

    def evaluate(self, scope: LintScope, found: Mapping[str, object]) -> list[str]:
        return find_problems(scope.select(("Source",)), self.read_table())

    def self_check(self) -> None:
        table = {"_bannedFolderNames": {"Utils": "기능 이름으로"}, "_componentFolderModel": ["Source/GameFramework/Runtime/"], "_componentSuffixes": ["Component"]}
        list_problem = find_problems(["Source/Engine/Utils/A.h", "Source/Engine/utils/B.h", "Source/Engine/X/Component/FooComponent.h",
                                    "Source/Engine/X/Component/FooQuery.h", "Source/GameFramework/Runtime/Component/Any.h", "Source/Engine/X/Component/Tests/T.cpp"], table)
        assert list_problem == ["Source/Engine/Utils/: 잡동사니 폴더 'Utils' — 기능 이름으로", "Source/Engine/utils/: 잡동사니 폴더 'utils' — 기능 이름으로",
                               "Source/Engine/X/Component/FooQuery.h: `Component/` 에는 컴포넌트만 — 씬 서비스 · 질의 · 검사는 `<기능>/Scene/`(FolderLayout 4 절)"], list_problem
