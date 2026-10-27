#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckEditorInputCalls.py

에디터 코드가 **`ImGuiIO` 의 입력 필드**(`io.KeyCtrl` · `io.MouseDown` · `io.MouseDelta` · `io.MouseWheel` …)를 직접 읽는 곳을 센다.

에디터 입력은 엔진 입력 맵(`EditorInputActions`, `editor.input.xml`)과 맥락 메뉴 · 끌어 놓기 어댑터(`EditorInputGUI`)가 판정한다(docs/plans/EditorUX.md 10-4).
ImGui 입력 **함수**(`IsKeyPressed` · `OpenPopup` · `BeginDragDropSource` …)는 형식으로 막는다 — 에디터 코드 전체(모든 뷰 부분 · 루트 · 확장 모듈)는
imgui.h 끝에 `EditorInputBan.h` 를 붙여 `[[deprecated]]` 를 오류로 올린다(`SW_EDITOR_INPUT_BAN`). 이 게이트는 형식이 못 막는 틈만 센다:

  - `ImGuiIO` 입력 필드 — 필드는 함수가 아니라 `[[deprecated]]` 로 다시 선언할 수 없고, 필드를 내주는 `ImGui::GetIO()` 는 입력이 아닌 필드
    (`DisplaySize` · `Fonts` · `DeltaTime` · `ConfigFlags`)에도 쓰여 함수째 막으면 그것까지 막힌다.
  - 금지를 끄는 정의 `SW_EDITOR_INPUT_ADAPTER` 의 자리 — 매크로는 형식으로 폴더에 묶을 수 없다. 어댑터 두 파일(`_kAdapterFiles`) 밖에서 정의하면 센다.

예외(`rules/CheckEditorInputCalls.toml` 의 `[exemption]`, 경로 앞머리 → 이유): OS 입력을 ImGui 로 넣는 `Common/Backend`, 어댑터 자신 `Common/Input`.
예외 경로에 읽기가 하나도 없으면 낡은 예외로 실패한다.

기준선은 지금 건수다(`[baseline] total`). 커밋 훅은 추가한 줄에 걸친 읽기만, 전체 훑기는 기준선을 넘을 때만 실패한다(`LintGate.BaselineSourceGate`).

  python Scripts/lint/gate/CheckEditorInputCalls.py [--root <repo>] [--files a.cpp b.h] [--whole-files] [--baseline N] [--list]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.LintGate import BaselineSourceGate, CountedFinding  # noqa: E402
from lint.SourceScan import SourceFile  # noqa: E402

_kScanRoot = ("Source/Editor", "Source/GameFramework/PlugIn/Editor")
_kSuffixes = (".h", ".hpp", ".inl", ".cc", ".cpp", ".cxx")
#: `ImGuiIO` 의 입력 필드 — 키 · 수정 키 · 마우스 단추 · 위치 · 이동 · 휠 · 글자 큐.
_kInputFields = ("KeyCtrl", "KeyShift", "KeyAlt", "KeySuper", "KeyMods", "MouseDown", "MouseClicked", "MouseReleased", "MouseDoubleClicked",
                 "MouseClickedCount", "MouseDownDuration", "MouseDragMaxDistanceSqr", "MouseDelta", "MouseWheel", "MouseWheelH", "MousePos",
                 "MouseClickedPos", "InputQueueCharacters")
#: 받는 쪽은 `GetIO()` 호출이거나 이름에 `io` 가 든 변수(`io` · `imguiIO`)다 — 다른 구조체의 같은 이름 필드를 잡지 않는다.
_kIoFieldRe = re.compile(r"(?:\bGetIO\s*\(\s*\)|\b\w*[iI][oO]\b)\s*(?:\.|->)\s*(" + "|".join(_kInputFields) + r")\b")
#: 금지 헤더(EditorInputBan.h)를 받지 않게 하는 정의. 매크로는 형식으로 폴더에 묶을 수 없어 여기서 자리를 센다 — 이 두 파일 밖에서 정의하면 실패한다.
_kAdapterDefineRe = re.compile(r"^\s*#\s*define\s+SW_EDITOR_INPUT_ADAPTER\b")
_kAdapterFiles = ("Source/Editor/Common/Input/EditorInputGUI.cpp",      # 맥락 메뉴 · 끌어 놓기 · 포인터 · 편집 키 어댑터
                  "Source/Editor/Common/Backend/ImGuiKeyForward.cpp")   # ImGui 가 받은 키를 엔진 입력으로 옮기는 곳


class CheckEditorInputCallsGate(BaselineSourceGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "에디터 코드가 ImGuiIO 입력 필드(io.KeyCtrl · io.MouseDown …)를 직접 읽지 않는지(입력 함수는 EditorInputBan.h 가 형식으로 막는다 — 훅은 추가한 줄만, 전체는 기준선 초과만)"
    buildComment = "Checking that editor code reads input through the editor input map, not ImGuiIO fields..."
    timeoutSeconds = 120
    preCommitPattern = tuple(f"{root}/*" for root in _kScanRoot)
    sourceScanRoots = _kScanRoot
    sourceSuffixes = _kSuffixes
    violationHeader = "에디터 코드의 ImGuiIO 입력 필드 직접 읽기"
    countedNoun = "ImGuiIO input reads"
    baselineExceededHint = "단축키 · 단추 · 몸짓은 에디터 입력 맵(editor.input.xml)으로, 맥락 메뉴 · 끌어 놓기는 EditorInputGUI 로"
    hint = ("  키 · 수정 키 · 마우스 단추 · 끌기는 에디터 입력 맵(EditorInputActions, Resource/editor/input/editor.input.xml)의 액션으로 읽는다.\n"
            "  맥락 메뉴 · 끌어 놓기는 Common/Input/EditorInputGUI 를 쓴다(docs/plans/EditorUX.md 10-4). 전체를 보려면 --whole-files 또는 --list.")
    selfTestCases = [
        {"name": "패널이 io 수정 키 · 마우스 필드를 읽는다", "expectedViolation": "2  Source/Editor/Panels/Probe/ProbePanel.cpp",
         "files": {"Source/Editor/Panels/Probe/ProbePanel.cpp": "void f()\n{\n    const ImGuiIO& io = ImGui::GetIO();\n    if ( io.KeyCtrl && ImGui::GetIO().MouseDown[0] )\n        return;\n    const float w = io.DisplaySize.x;\n}\n"},
         "args": ["--baseline", "0"]},
        {"name": "--files + --whole-files 는 위치를 찍는다", "expectedViolation": "Source/Editor/Panels/Probe/Wheel.cpp:3: ImGuiIO.MouseWheel",
         "files": {"Source/Editor/Panels/Probe/Wheel.cpp": "void f( const ImGuiIO& io )\n{\n    float wheel = io.MouseWheel;\n}\n"},
         "args": ["--files", "Source/Editor/Panels/Probe/Wheel.cpp", "--whole-files"]},
        {"name": "어댑터 두 파일 밖에서 금지를 끄는 정의를 둔다", "expectedViolation": "Source/Editor/Panels/Probe/Escape.cpp:2: SW_EDITOR_INPUT_ADAPTER",
         "files": {"Source/Editor/Panels/Probe/Escape.cpp": "#include \"pch.h\"\n#define SW_EDITOR_INPUT_ADAPTER\n#include <imgui.h>\n"},
         "args": ["--files", "Source/Editor/Panels/Probe/Escape.cpp", "--whole-files"]},
    ]

    def findCounted(self, source: SourceFile) -> list[CountedFinding] | None:
        relative = source.relative
        listExemptKey = [key for key in self.mapExemption if relative.startswith(key)]
        listFound: list[CountedFinding] = []
        if relative not in _kAdapterFiles:
            listAdapterDefine = [CountedFinding("total", relative, lineIndex, lineIndex, "SW_EDITOR_INPUT_ADAPTER", line.strip())
                                 for lineIndex, line in enumerate(source.listLine, start=1) if _kAdapterDefineRe.match(line)]
            if listAdapterDefine:
                return listAdapterDefine   # 예외 경로여도 센다 — 금지를 끄는 자리는 두 파일뿐이다
        for lineIndex, line in enumerate(source.listMaskedLine, start=1):
            for match in _kIoFieldRe.finditer(line):
                listFound.append(CountedFinding("total", relative, lineIndex, lineIndex, f"ImGuiIO.{match.group(1)}", source.listLine[lineIndex - 1].strip()))
        for key in listExemptKey:
            self.seeExemption(key)
            if listFound:
                self.useExemption(key)
        if listExemptKey:
            return None
        return listFound


main = CheckEditorInputCallsGate.run


if __name__ == "__main__":
    sys.exit(main())
