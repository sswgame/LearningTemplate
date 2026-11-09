#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/EditorViewIncludes.py

MVVM 으로 옮긴 에디터 패널의 View(`Source/Editor/Panels/**` 의 `Model` 밖 .h · .cpp)가 모델 · 씬 헤더를 include 하는지 본다
(계약: `docs/agents/EditorUI.md` 'MVVM' 절, 표: `Config/Dev/EditorViewIncludes.json`).

**왜 린트인가(임시).** 끝 모양은 형식이다 — View 부분(`Source/Editor/CMakeLists.txt` 의 `EditorTier<N>View`)에 모델 · 씬 헤더 자리마다 `#error` 껍데기를 두어
컴파일이 막는다(모델 층의 ImGui 금지와 같은 장치). 그 장치는 부분 전체에 한 번에 걸리므로 패널을 하나씩 옮기는 동안에는 걸 수 없다. 그래서 옮긴 View 목록(데이터)만 이
린트가 보고, 모든 패널을 옮기면 껍데기를 걸고 이 린트와 표를 지운다.

보는 것: 목록의 View 가 직접 include 한 헤더, 그리고 그 View 가 include 한 패널 `Model` 폴더 헤더(ViewModel · ViewState)가 거쳐 include 하는 헤더(Model 폴더 안에서 따라간다) —
ViewModel 헤더가 모델 헤더를 끌고 오면 껍데기를 건 날 View 가 멈추므로 지금 잡는다. 금지 폴더 안의 값 · 창구 헤더는 표의 `_allowedHeaders` 가 까닭과 함께 허용한다.
목록의 View 가 사라졌으면(이름을 바꿨으면) 낡은 줄로 알린다.

`py -3 -m Scripts lint editor-view-includes [--self-check]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Callable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402

kConfigPath = "Config/Dev/EditorViewIncludes.json"
kViewExtensions = (".h", ".cpp")
kInclude = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
kPanelModelHeader = re.compile(r'^Editor/Panels/(?:[^/]+/)+Model/[^/]+\.h$')


def findProblems(table: dict, readSource: Callable[[str], str | None]) -> list[str]:
    """`readSource(경로)` 는 저장소 상대 경로의 글을 돌려준다(없으면 None)."""
    listBanned = [re.compile(pattern) for pattern in table["_bannedIncludePatterns"]]
    allowed = table["_allowedHeaders"]

    def isBanned(header: str) -> bool:
        return header not in allowed and any(pattern.search(header) for pattern in listBanned)

    listProblem: list[str] = []
    for view in table["_migratedViews"]:
        listFile = [f"{view}{extension}" for extension in kViewExtensions if readSource(f"{view}{extension}") is not None]
        if not listFile:
            listProblem.append(f"{kConfigPath}: 옮긴 View `{view}` 의 .h · .cpp 가 없다 — 이름을 바꿨으면 표를 고친다")
            continue
        for path in listFile:
            setSeen: set[str] = set()
            listPending = [(header, None) for header in kInclude.findall(readSource(path) or "")]
            while listPending:
                header, via = listPending.pop()
                if header in setSeen:
                    continue
                setSeen.add(header)
                if isBanned(header):
                    route = f" (`{via}` 를 거쳐)" if via else ""
                    listProblem.append(f"{path}: MVVM View 가 모델 · 씬 헤더 `{header}` 를 include 한다{route} — 값은 ViewModel 의 `gather` 가 ViewState 에 담고, 바꾸는 일은 ViewModel 명령으로 부른다")
                    continue
                if kPanelModelHeader.match(header):
                    text = readSource(f"Source/{header}")
                    if text is not None:
                        listPending.extend((nested, via or header) for nested in kInclude.findall(text))
    return listProblem


def checkEditorViewIncludes(projectRoot: Path) -> list[str]:
    table = json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))

    def readSource(relativePath: str) -> str | None:
        path = projectRoot / relativePath
        return path.read_text(encoding="utf-8") if path.is_file() else None

    return findProblems(table, readSource)


def runSelfCheck() -> None:
    table = {
        "_bannedIncludePatterns": ["^Editor/Common/Workspace/", "^Engine/Scene/"],
        "_allowedHeaders": {"Editor/Common/Workspace/EditorPanelID.h": "값"},
        "_migratedViews": ["Source/Editor/Panels/X/XPanel"],
    }

    def makeReader(files: dict[str, str]) -> Callable[[str], str | None]:
        return files.get

    clean = {
        "Source/Editor/Panels/X/XPanel.h": '#include "Editor/Panels/X/Model/XViewModel.h"\n#include "Editor/Common/Workspace/EditorPanelID.h"\n',
        "Source/Editor/Panels/X/XPanel.cpp": '#include "Editor/Panels/X/XPanel.h"\n#include <imgui.h>\n',
        "Source/Editor/Panels/X/Model/XViewModel.h": '#include "Core/Container/vector.h"\n',
    }
    assert findProblems(table, makeReader(clean)) == [], "값 헤더 · 허용 헤더 · ViewModel 은 통과한다"

    direct = dict(clean)
    direct["Source/Editor/Panels/X/XPanel.cpp"] = '#include "Engine/Scene/Scene.h"\n'
    assert len(findProblems(table, makeReader(direct))) == 1, "View 의 직접 include"

    viaModel = dict(clean)
    viaModel["Source/Editor/Panels/X/Model/XViewModel.h"] = '#include "Editor/Common/Workspace/EditorService.h"\n'
    listViaModel = findProblems(table, makeReader(viaModel))
    assert len(listViaModel) == 1 and "XViewModel.h" in listViaModel[0], "ViewModel 헤더를 거친 include(패널 .h 가 자기 줄로 본다)"

    viewModelSource = dict(clean)
    viewModelSource["Source/Editor/Panels/X/Model/XViewModel.cpp"] = '#include "Engine/Scene/Scene.h"\n'
    assert findProblems(table, makeReader(viewModelSource)) == [], "ViewModel 의 .cpp 는 모델을 본다"

    assert len(findProblems(table, makeReader({}))) == 1, "사라진 View 는 낡은 줄"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="MVVM 으로 옮긴 에디터 패널 View 가 모델 · 씬 헤더를 include 하는지 본다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[EditorViewIncludes] self-check OK")
        return ExitCode.Passed
    listProblem = checkEditorViewIncludes(getProjectRoot())
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[EditorViewIncludes] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
