#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/EditorWidgetLabels.py

에디터 코드에서 ImGui 입력 위젯(`InputInt` · `InputText` · `DragFloat` · `SliderFloat` · `Combo` · `ColorEdit` …)에 **보이는 이름표**를 주는 호출을 막는다.
ImGui 는 그 이름표를 칸 **오른쪽**에 붙이고 칸이 창 폭을 따라 늘어, 좁은 칸에서 이름표 · 옆 칸이 잘렸다(시퀀서 · 타일 맵 · 스프라이트 클립 · 멀티플레이 · 캐릭터 에디터, uiaudit).
고칠 모양: 행이면 `EditorWidgets::drawPropertyRowLabel( 이름 )`, 도구줄이면 `EditorWidgets::drawInlineLabel( 이름 )` 을 앞에 두고 위젯 이름은 `"##id"`.
단위는 숫자 형식에 넣는다(`"%.2f s"`). 체크박스 · 라디오는 오른쪽 이름표가 맞아 보지 않는다.
콤보는 `EditorWidgets::beginCombo`, 글 칸은 `EditorWidgets::drawTextField` 가 보이는 이름표를 스스로 앞으로 옮긴다 — `ImGui::BeginCombo` 에 보이는 이름표를 주지 않는다.
**왜 형식이 아니라 린트인가.** 이름표는 문자열 내용이라 ImGui 선언을 다시 막아도(`EditorInputBan.h`) 가를 수 없다 — 첫 문자열 인자가 `##` 로 시작하는지,
첫 인자가 보이는 이름표를 만드는 `EditorThemeUtil::makeStableLabel( … )` 인지만 본다(그 모양이 린트를 빠져나가 Packaging · 테마 · 지형 붓 콤보의 이름표가 오른쪽에 남았다).
"""

from __future__ import annotations

import re
from pathlib import Path

kPattern = re.compile(
    r'ImGui::(InputInt|InputInt2|InputInt3|InputInt4|InputFloat|InputFloat2|InputFloat3|InputFloat4|InputDouble|InputText|InputTextMultiline|InputTextWithHint|InputScalar|'
    r'DragFloat|DragFloat2|DragFloat3|DragFloat4|DragInt|DragInt2|DragInt3|DragInt4|DragScalar|SliderFloat|SliderFloat2|SliderFloat3|SliderFloat4|SliderInt|'
    r'SliderAngle|SliderScalar|Combo|BeginCombo|ColorEdit3|ColorEdit4)\(\s*(?:"([^"]*)"|(EditorThemeUtil::makeStableLabel)\()'
)
kEditorRoots = ("Source/Editor/", "Source/GameFramework/Plugin/Editor/")


def isEditorFile(relative: str) -> bool:
    return relative.startswith(kEditorRoots) or "/Editor/" in relative and relative.startswith("Source/GameFramework/Kits/")


def checkEditorWidgetLabels(projectRoot: Path, listFile: list[str]) -> list[str]:
    """@p listFile 가운데 에디터 C++ 파일이 ImGui 입력 위젯에 보이는 이름표를 주면 그 줄을 돌려준다."""
    listViolation: list[str] = []
    for file in listFile:
        path = Path(file).resolve()
        try:
            relative = path.relative_to(projectRoot).as_posix()
        except ValueError:
            continue
        if not relative.endswith(".cpp") or not isEditorFile(relative):
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
            for match in kPattern.finditer(line):
                if match.group(2) is not None and match.group(2).startswith("##"):
                    continue
                label = f'"{match.group(2)}"' if match.group(2) is not None else "makeStableLabel(...)"
                listViolation.append(f'{relative}:{number}: ImGui::{match.group(1)}( {label} ... ) - label goes left: drawPropertyRowLabel / drawInlineLabel + "##id" (combo: EditorWidgets::beginCombo)')
    return listViolation


def main(argv: list[str] | None = None) -> int:  # noqa: ARG001 — `lint editor-widget-labels` 가 인자 목록을 넘긴다
    projectRoot = Path(__file__).resolve().parents[2]
    listFile = [str(path) for root in ("Source/Editor", "Source/GameFramework") for path in (projectRoot / root).rglob("*.cpp")]
    listViolation = checkEditorWidgetLabels(projectRoot, listFile)
    for line in listViolation:
        print(line)
    print(f"[EditorWidgetLabels] {len(listViolation)} violation(s)")
    return 1 if listViolation else 0


if __name__ == "__main__":
    raise SystemExit(main())
