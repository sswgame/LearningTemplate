#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckEditorIcons.py

커밋된 에디터 아이콘 폰트와 글리프 상수 헤더가 그림 정의(`Scripts/common/EditorIconFont.py`)와 같은지 검사합니다.

두 파일은 같은 테이블에서 함께 만들어집니다. 헤더만 손으로 고치거나 그림만 바꾸고 다시 만들지 않으면, 상수가 다른 글리프를 가리키거나
폰트에 없는 코드포인트가 화면에 빈 상자로 나옵니다. 빌드와 테스트는 이것을 잡지 못합니다.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common.EditorIconFont import buildEditorIconFiles  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402


def normalizeNewlinesInternal(relative: str, data: bytes) -> bytes:
    """헤더는 글이라 체크아웃 줄끝(core.autocrlf — Windows CRLF, 리눅스 LF)을 따른다 — 줄끝을 빼고 견준다. 폰트는 바이트 그대로."""
    return data.replace(b"\r\n", b"\n") if relative.endswith(".h") else data


class CheckEditorIconsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 곳에 있다."""

    description = "에디터 아이콘 폰트 · 글리프 헤더가 그림 정의와 같은지 검사"
    buildComment = "Checking that the editor icon font and glyph header match their drawing table..."
    timeoutSeconds = 30
    preCommitPattern = ("Scripts/common/EditorIconFont.py", "Resource/editor/fonts/*", "Source/Editor/Common/GUI/EditorIconGlyphs.h")
    preCommitFileArgument = ""
    violationHeader = "에디터 아이콘 파일이 그림 정의와 다름"
    hint = "  다시 만든다:  py -3 Scripts/generate/GenerateEditorIcons.py  (폰트와 헤더를 함께 커밋한다)"
    selfTestCases = [
        {
            "name": "손으로 고친 글리프 헤더",
            "files": {"Source/Editor/Common/GUI/EditorIconGlyphs.h": "// hand edited\n"},
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        violations = []
        mapFile = buildEditorIconFiles()
        for relative, data in mapFile.items():
            path = repositoryRoot / relative
            if path.is_file() is False:
                violations.append(f"[EditorIcons] 파일이 없습니다: {relative}")
            elif normalizeNewlinesInternal(relative, path.read_bytes()) != normalizeNewlinesInternal(relative, data):
                violations.append(f"[EditorIcons] 그림 정의와 다릅니다: {relative}")
        return GateResult(listViolation=violations, summary=f"생성 파일 {len(mapFile)}개")


main = CheckEditorIconsGate.run


if __name__ == "__main__":
    sys.exit(main())
