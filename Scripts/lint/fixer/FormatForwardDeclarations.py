#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/fixer/FormatForwardDeclarations.py

C++ 소스코드 내 전방 선언(Forward Declaration)을
enum (및 enum class) -> struct -> class 순서로 정렬하고,
각 그룹 간 1개의 공백 라인을 삽입하여 구분합니다.
각 그룹 내에서는 타입 이름 기준 알파벳 순으로 정렬합니다.
규칙은 `Scripts/lint/ForwardDeclarationRules.py` 한 자리다(`inspectSource` — 파일 하나를 받아 고친 완성 글을 돌려주는 판정 함수).

사용법:
  py -3 Scripts/lint/fixer/FormatForwardDeclarations.py                    # Git 변경 파일 포맷팅
  py -3 Scripts/lint/fixer/FormatForwardDeclarations.py [파일들...]        # 지정한 파일들만 포맷팅
  py -3 Scripts/lint/fixer/FormatForwardDeclarations.py --all              # 전체 파일 포맷팅
  py -3 Scripts/lint/fixer/FormatForwardDeclarations.py --check            # 수정 없이 위반 여부만 검사
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.ForwardDeclarationRules import inspectSource  # noqa: E402
from lint.LintFixer import FixPass, LintFixer  # noqa: E402


class FormatForwardDeclarationsFixer(LintFixer):
    """전방 선언을 enum -> struct -> class 로 정렬하고 그룹 사이에 빈 줄을 넣는다."""

    tag = "ForwardDeclaration"
    description = "전방 선언(Forward Declaration) 정렬 (enum -> struct -> class 및 그룹 간 빈 줄 삽입)"
    listPass = (
        FixPass(
            inspect=inspectSource,
            problem="전방 선언 정렬(enum -> struct -> class 및 빈 줄)이 어긋났습니다.",
            done="전방 선언 정렬 완료",
            # class 가 enum 보다 앞에 있고 그룹 사이 빈 줄도 없다.
            badSample=(
                "#pragma once\n\nnamespace sw\n{\n"
                "    class Alpha;\n    enum class Beta;\n    struct Gamma;\n\n    void probe();\n}\n"
            ),
            # 고친 결과는 아래 goodSample 과 글자까지 같다.
            fixedSample=(
                "#pragma once\n\nnamespace sw\n{\n"
                "    enum class Beta;\n\n    struct Gamma;\n\n    class Alpha;\n\n    void probe();\n}\n"
            ),
            # 이미 정렬된 것은 건드리지 않는다 (건드리면 포맷이 매번 흔들린다).
            goodSample=(
                "#pragma once\n\nnamespace sw\n{\n"
                "    enum class Beta;\n\n    struct Gamma;\n\n    class Alpha;\n\n    void probe();\n}\n"
            ),
        ),
    )


main = FormatForwardDeclarationsFixer.run


if __name__ == "__main__":
    sys.exit(main())
