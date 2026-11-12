#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/FolderLayout.py

파일 자리 규칙(`docs/agents/FolderLayout.md` 4 절)을 경로로 막는다 — 표는 `Config/Rules/Lint/FolderLayout.json`.
  1. 잡동사니 폴더 이름(`Util` · `Utility` · `Helpers` · `Misc` …) 금지 — 하는 일(Encoding · Algorithm · Hash …)로 묶는다.
  2. 기능 폴더의 `Component/` 에는 컴포넌트(`*Component` · 허용 끝말)만 — 씬 서비스 · 질의 · 검사는 `<기능>/Scene/`.

**왜 린트인가.** 폴더 자리는 형식이 말하지 못한다. 기준표 없이 "처음 쓰는 쪽" · 이름 낱말로 자리를 정해 RegistrationList(String 폴더) · StringUtil(Container) 같은
어긋남이 쌓였다(FolderMoves 13 절, 2026-11-08 전수 조사). 경로만 본다.
`Dev/` 폴더 자리는 린트가 아니라 configure 가 막는다(`cmake/Engine/UnbuiltSources.cmake`, 표 `Config/Rules/Layers/DevFolders.json`) — 배포가 빼는 개발 폴더 목록과 한 표다.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint.LintScope import LintScope, makeWholeScope  # noqa: E402

kConfigPath = "Config/Rules/Lint/FolderLayout.json"


def checkFolderLayout(projectRoot: Path, scope: LintScope) -> list[str]:
    table = json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))
    banned = {name.lower(): reason for name, reason in table["_bannedFolderNames"].items()}
    componentRoots = tuple(table["_componentFolderModel"])
    componentSuffixes = tuple(table["_componentSuffixes"])
    listPath = scope.select(("Source",))
    listProblem: list[str] = []
    seenFolder: set[str] = set()
    for relative in listPath:
        parts = relative.split("/")
        for index, folder in enumerate(parts[:-1]):
            prefix = "/".join(parts[: index + 1]) + "/"
            if prefix in seenFolder:
                continue
            if folder.lower() in banned:
                seenFolder.add(prefix)
                listProblem.append(f"{prefix}: 잡동사니 폴더 '{folder}' — {banned[folder.lower()]}")
        if "Component" in parts[:-1] and "Tests" not in parts and not relative.startswith(componentRoots):
            stem = parts[-1].split(".")[0]
            if not stem.endswith(componentSuffixes):
                listProblem.append(f"{relative}: `Component/` 에는 컴포넌트만 — 씬 서비스 · 질의 · 검사는 `<기능>/Scene/`(FolderLayout 4 절)")
    return listProblem


def main(listArgument: list[str] | None = None) -> int:  # noqa: ARG001 — `py -3 -m Scripts lint folder-layout` 가 인자 목록을 넘긴다
    listProblem = checkFolderLayout(getProjectRoot(), makeWholeScope(getProjectRoot()))
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[FolderLayout] {'OK' if not listProblem else f'FAIL {len(listProblem)}건'}")
    return ExitCode.Passed if not listProblem else ExitCode.Violation


if __name__ == "__main__":
    sys.exit(main())
