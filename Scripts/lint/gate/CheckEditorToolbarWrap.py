#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckEditorToolbarWrap.py

에디터 패널의 단추 · 체크박스 · 라디오 · 화살표 단추 · 토글 단추 줄이 **좁은 창에서 창 밖으로 나가지 않게** 한다. 그 항목 바로 앞의 인자 없는 `ImGui::SameLine();` 은 앞 항목 오른쪽에 무조건 이어 놓아서,
줄이 창보다 길면 뒤 단추가 창 밖에 놓여 사람도 시나리오도 누를 수 없다(좁은 창 · 넓은 도크 · 기본 배치 변경 · UI 배율).
그 앞에서는 `EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( 라벨 ) )` 로 들어갈 때만 같은 줄에 잇고 아니면 줄을 바꾼다
(폭 도우미: 단추 · 토글 단추 `measureSmallButton`, 화살표 단추 `measureArrowButton`, 체크박스 · 라디오 `measureCheckbox`).

왜 형식(타입)으로 못 막고 게이트를 두는가: 패널이 `<imgui.h>` 를 직접 포함해 `ImGui::SameLine()` 을 부르는 길 자체를 컴파일로 닫을 수 없다.
`ImGui::Button` 의 폭은 라벨 글자와 글꼴로 정해져 호출 쪽이 알 수 있는 것이 아니라 앞 항목과 한 쌍으로 재야 하므로, 한 쌍을 한 호출(`sameLineIfFits`)로 묶고 이 게이트가 맨 호출만 본다.

위반(주석 · 문자열은 가린 글에서 본다): 인자 없는 `ImGui::SameLine();` 의 바로 다음 문장이 `ImGui::Button(` · `ImGui::SmallButton(` · `ImGui::ArrowButton(` · `ImGui::Checkbox(` · `ImGui::RadioButton(` · `EditorWidgets::drawToggleButton(` 호출이다
(`if (` 조건 안, 사이의 `ImGui::BeginDisabled(…);` 는 건너뛴다).
고정 폭 단추(`Button( 라벨, 크기 )`)도 같다 — 폭으로 `sameLineIfFits( 폭 )` 를 쓴다.

보지 않는 것: 이 항목이 아닌 것(라벨 · 입력 칸 · 콤보 · 칩) 앞의 SameLine, 사이에 다른 문장이 낀 SameLine, 위치를 준 `SameLine( 오프셋 )`, 한 줄에 단추 하나만 두는 고정 배치(툴바 창 · 도구 모음 등 `Source/Editor/Common/` 이하는 검사하지 않는다).

기준선은 현재 건수다(`rules/CheckEditorToolbarWrap.toml`). 커밋 훅은 추가한 줄만, 전체 훑기는 기준선을 넘을 때만 실패한다.

  python Scripts/lint/gate/CheckEditorToolbarWrap.py [--root <repo>] [--files a.cpp b.h] [--whole-files] [--baseline N] [--list]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.CodeText import blankCommentsAndLiterals  # noqa: E402
from lint.LintGate import BaselineSourceGate, CountedFinding  # noqa: E402
from lint.SourceScan import SourceFile  # noqa: E402


_kScanRoot = ("Source/Editor/Panels", "Source/GameFramework/PlugIn/Editor")
_kSuffixes = (".cpp", ".cc", ".cxx")
#: 맨 SameLine 문장 → (건너뛸 BeginDisabled 문장들) → (`if (`) → 단추 호출.
_kBareSameLineBeforeButtonRe = re.compile(
    r"\bImGui::SameLine\s*\(\s*\)\s*;"
    r"(?:\s*ImGui::BeginDisabled\s*\([^;]*\)\s*;)*"
    r"\s*(?:if\s*\(\s*)?!?\s*(?:ImGui::(?:SmallButton|Button|ArrowButton|Checkbox|RadioButton)|EditorWidgets::drawToggleButton)\s*\("
)


def findBareSameLines(text: str, masked: str | None = None) -> list[int]:
    """위반 줄 번호(SameLine 이 있는 줄) 목록."""
    masked = blankCommentsAndLiterals(text) if masked is None else masked
    if len(masked) != len(text):
        return []
    return [text.count("\n", 0, match.start()) + 1 for match in _kBareSameLineBeforeButtonRe.finditer(masked)]


class CheckEditorToolbarWrapGate(BaselineSourceGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "에디터 패널의 단추 · 체크박스 · 라디오 · 화살표 · 토글 앞 맨 ImGui::SameLine() 은 EditorChrome::sameLineIfFits 로(훅은 추가한 줄만, 전체는 기준선 초과만)"
    buildComment = "Checking that editor panel button rows wrap instead of leaving the window..."
    timeoutSeconds = 120
    preCommitPattern = tuple(f"{root}/*" for root in _kScanRoot)
    sourceScanRoots = _kScanRoot
    sourceSuffixes = _kSuffixes
    violationHeader = "단추류 앞 맨 SameLine"
    countedNoun = "bare SameLine before a button"
    baselineExceededHint = "단추 앞은 sameLineIfFits 로"
    hint = ("  단추 · 체크박스 · 라디오 · 화살표 · 토글을 앞 항목 오른쪽에 이을 때는 EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( 라벨 ) ) 를 쓴다(고정 폭 단추는 그 폭, 화살표 measureArrowButton, 체크박스 · 라디오 measureCheckbox).\n"
            "  좁은 창에서 뒤 단추가 창 밖에 놓이지 않고 줄을 바꾼다. 전체를 보려면 --whole-files 또는 --list.")
    selfTestCases = [
        {"name": "단추 앞 맨 SameLine · if 조건 · BeginDisabled 뒤", "expectedViolation": "3  Source/Editor/Panels/A.cpp",
         "files": {"Source/Editor/Panels/A.cpp": ("void f()\n{\n    ImGui::SameLine();\n    ImGui::Button( \"A\" );\n"
                                                  "    ImGui::SameLine();\n    if ( ImGui::SmallButton( \"B\" ) )\n        g();\n"
                                                  "    ImGui::SameLine();\n    ImGui::BeginDisabled( bOff );\n    if ( ImGui::Button( \"C\" ) )\n        g();\n}\n")},
         "args": ["--baseline", "0"]},
        {"name": "라벨 · 입력 칸 앞 · 위치를 준 SameLine · sameLineIfFits 는 통과", "expectedViolation": "1  Source/Editor/Panels/B.cpp",
         "files": {"Source/Editor/Panels/B.cpp": ("void f()\n{\n    ImGui::SameLine();\n    ImGui::TextUnformatted( \"x\" );\n    ImGui::SameLine( 120.0f );\n    ImGui::Button( \"A\" );\n"
                                                  "    EditorChrome::sameLineIfFits( 10.0f );\n    ImGui::Button( \"B\" );\n    ImGui::SameLine();\n    ImGui::Button( \"C\" );\n}\n")},
         "args": ["--baseline", "0"]},
        {"name": "화살표 · 체크박스 · 라디오 · 토글 단추 앞 맨 SameLine", "expectedViolation": "4  Source/Editor/Panels/C.cpp",
         "files": {"Source/Editor/Panels/C.cpp": ("void f()\n{\n    ImGui::SameLine();\n    if ( ImGui::ArrowButton( \"##a\", ImGuiDir_Up ) )\n        g();\n"
                                                  "    ImGui::SameLine();\n    ImGui::Checkbox( \"B\", &b );\n    ImGui::SameLine();\n    if ( ImGui::RadioButton( \"C\", true ) )\n        g();\n"
                                                  "    ImGui::SameLine();\n    if ( EditorWidgets::drawToggleButton( \"D\", b, c ) )\n        g();\n"
                                                  "    ImGui::SameLine();\n    ImGui::Combo( \"E\", &i, arr, 3 );\n    EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( \"F\" ) );\n    ImGui::Checkbox( \"F\", &b );\n}\n")},
         "args": ["--baseline", "0"]},
    ]

    def findCounted(self, source: SourceFile) -> list[CountedFinding] | None:
        listLine = source.text.split("\n")
        return [CountedFinding("total", source.relative, line, line, "단추류 앞 맨 SameLine — EditorChrome::sameLineIfFits( 폭 ) 로 줄을 바꿀 수 있게", listLine[line - 1].strip())
                for line in findBareSameLines(source.text, source.masked)]


main = CheckEditorToolbarWrapGate.run

if __name__ == "__main__":
    sys.exit(main())
