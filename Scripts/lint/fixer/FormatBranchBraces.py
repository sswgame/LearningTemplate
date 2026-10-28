#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/fixer/FormatBranchBraces.py

중괄호의 모양을 정리합니다 (docs/agents/CppStyle.md '분기문 규칙'). 두 가지를 본다.

**1) 본문이 한 줄인 if / else if / else 의 중괄호를 제거한다.**

  - if 계열만 대상이다. for / while / do 는 본문이 한 줄이어도 중괄호를 유지한다.
  - else / else if 가 붙은 사슬은 **모든 갈래가 한 줄일 때만** 중괄호를 벗긴다.
    한 갈래라도 여러 줄이면 그 사슬은 전부 중괄호를 유지한다.
  - 블록 안에 주석 줄·전처리기 지시문이 있으면 한 줄이 아니므로 건드리지 않는다.

**2) 본문이 두 문장 이상인 switch 의 case / default 에 중괄호를 씌운다.**

  - 한 문장짜리 본문은 그대로 둔다 — `case A: return X;` 도, 라벨 다음 줄에 문장 하나가
    오는 형태도 대상이 아니다. 폴스루 라벨(본문이 없는 라벨)도 마찬가지다.
  - `break;` 는 본문의 한 문장으로 센다. 그래서 `문장 하나 + break;` 에는 중괄호가 붙는다.
    중괄호를 쓴 case 는 `break;` 를 중괄호 **안**에 둔다 — 이 저장소에서 break 는 본문의 일부지
    라벨의 종결자가 아니다.
  - 인자를 줄바꿈한 호출처럼 한 문장이 여러 줄에 걸친 것은 한 문장으로 센다.

**3) 한 switch 안에서 한 case 라도 중괄호가 있으면 본문이 있는 모든 case 에 중괄호를 씌운다.**

  - if 사슬의 "한 갈래라도 중괄호면 모두" 와 같은 일관성 규칙이다. `case A: return X;` 처럼 라벨과 같은 줄에
    본문이 있으면 라벨과 본문을 나눠 씌운다. 모든 case 가 한 문장인 표 모양 switch 는 그대로 둔다.
  - 본문이 없는 폴스루 라벨 · 전처리기 · 매크로 줄바꿈 · `[[fallthrough]]` 가 낀 본문은 2) 와 같이 건드리지 않는다.

**4) 본문에 중괄호가 없는 for · 범위 for · while 에 중괄호를 씌운다.**

  - 판정은 `Scripts/lint/LoopBraceRules.py` 한 자리다(`inspectSource`). 여러 줄에 걸친 한 문장 · if/else 사슬 · 중첩 반복
    본문은 그 전체를 한 문장으로 감싸고, 같은 줄 본문(`for ( … ) f();`)은 줄을 끊어 감싸며, 빈 본문(`while ( x );`)은 `{}` 로 쓴다.
  - `do { … } while ( … )` 의 꼬리는 반복문 머리가 아니다. 머리 다음 줄부터 본문 끝까지 주석 · 전처리기 · 매크로 줄바꿈이 끼면
    건드리지 않는다(게이트가 "손으로" 로 알린다).

1) ~ 3) 의 규칙은 `Scripts/lint/BranchBraceRules.py` 에 있다. 네 규칙 모두 파일 하나(`SourceFile`)를 받아 고친 완성 글을 돌려주는 판정 함수(`inspectSource` 꼴)이고,
`FixPass(inspect=…)` 가 부른다 — 줄 끝(CRLF) · BOM 은 `SourceFile.restore` 가 지킨다.

clang-format 은 둘 다 표현하지 못한다. RemoveBracesLLVM 은 for/while 까지 같이 벗겨내고,
InsertBraces 는 if/for/while 만 보고 case 라벨은 건드리지 않는다. 그래서 clang-format
앞단에서 이 스크립트가 돌고, 뒤이어 도는 clang-format 이 들여쓰기를 맞춘다.

사용법:
  py -3 Scripts/lint/fixer/FormatBranchBraces.py                    # Git 변경 파일 포맷팅
  py -3 Scripts/lint/fixer/FormatBranchBraces.py [파일들...]        # 지정한 파일들만 포맷팅
  py -3 Scripts/lint/fixer/FormatBranchBraces.py --all              # 전체 파일 포맷팅
  py -3 Scripts/lint/fixer/FormatBranchBraces.py --check            # 수정 없이 위반 여부만 검사
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.BranchBraceRules import inspectCaseBraces, inspectCaseConsistency, inspectIfBraces  # noqa: E402
from lint.LintFixer import FixPass, LintFixer  # noqa: E402
from lint.LoopBraceRules import inspectSource as inspectLoopBraces  # noqa: E402


class FormatBranchBracesFixer(LintFixer):
    """
    if 를 먼저 벗기고 **그 다음에** case 를 센다 — 순서가 반대면 벗겨질 중괄호가 문장 수를 부풀린다.
    `listPass` 의 선언 순서가 그 계약이다.
    """

    tag = "BranchBraces"
    description = "한 줄짜리 if 본문의 중괄호 제거 · 여러 문장인 case 본문에 중괄호 추가 · 한 switch 안의 case 중괄호 일관성 · 반복문 본문 중괄호"
    listPass = (
        FixPass(
            inspect=inspectIfBraces,
            problem="한 줄짜리 if 본문에 불필요한 중괄호가 있습니다.",
            done="한 줄짜리 if 본문의 중괄호 제거 완료",
            # 한 줄짜리 if 는 벗긴다.
            badSample=(
                '#include "pch.h"\n\nvoid probe( int32 count )\n{\n'
                "    if ( count > 0 )\n    {\n        return;\n    }\n}\n"
            ),
            fixedSample=(
                '#include "pch.h"\n\nvoid probe( int32 count )\n{\n'
                "    if ( count > 0 )\n        return;\n}\n"
            ),
            # for 는 본문이 한 줄이어도 유지한다 — 이 조각이 바뀌면 규칙이 너무 넓어진 것이다.
            goodSample=(
                '#include "pch.h"\n\nvoid probe( int32 count )\n{\n'
                "    for ( int32 index = 0; index < count; ++index )\n    {\n        doThing();\n    }\n}\n"
            ),
        ),
        FixPass(
            inspect=inspectCaseBraces,
            problem="본문이 여러 문장인 case 에 중괄호가 없습니다.",
            done="여러 문장인 case 본문에 중괄호 추가 완료",
            # 두 문장(호출 + break)이면 씌운다.
            badSample=(
                '#include "pch.h"\n\nvoid probe( int32 mode )\n{\n    switch ( mode )\n    {\n'
                "    case 0:\n        doThing();\n        break;\n    }\n}\n"
            ),
            fixedSample=(
                '#include "pch.h"\n\nvoid probe( int32 mode )\n{\n    switch ( mode )\n    {\n'
                "    case 0:\n    {\n        doThing();\n        break;\n    }\n    }\n}\n"
            ),
            # 한 문장짜리 본문은 그대로 둔다.
            goodSample=(
                '#include "pch.h"\n\nvoid probe( int32 mode )\n{\n    switch ( mode )\n    {\n'
                "    case 0:\n        return;\n    }\n}\n"
            ),
        ),
        FixPass(
            inspect=inspectCaseConsistency,
            problem="같은 switch 안에서 중괄호를 쓴 case 와 쓰지 않은 case 가 섞여 있습니다.",
            done="switch 안의 case 중괄호를 한 모양으로 맞춤",
            # 중괄호를 쓴 case 옆의 한 문장 case(다음 줄 본문 · 같은 줄 본문)에도 씌운다.
            badSample=(
                '#include "pch.h"\n\nint32 probe( int32 mode )\n{\n    switch ( mode )\n    {\n'
                "    case 0:\n    {\n        doThing();\n        return 1;\n    }\n"
                "    case 1:\n        return 2;\n"
                "    case Mode::Two: return 3;\n"
                "    }\n    return 0;\n}\n"
            ),
            fixedSample=(
                '#include "pch.h"\n\nint32 probe( int32 mode )\n{\n    switch ( mode )\n    {\n'
                "    case 0:\n    {\n        doThing();\n        return 1;\n    }\n"
                "    case 1:\n    {\n        return 2;\n    }\n"
                "    case Mode::Two:\n    {\n        return 3;\n    }\n"
                "    }\n    return 0;\n}\n"
            ),
            # 모든 case 가 한 문장인 표 모양 switch · 본문 없는 폴스루 라벨은 그대로 둔다.
            goodSample=(
                '#include "pch.h"\n\nconst char* probe( int32 mode )\n{\n    switch ( mode )\n    {\n'
                "    case 0:\n    case 1:\n        return \"low\";\n"
                "    case 2: return \"mid\";\n"
                "    default:\n        return \"high\";\n    }\n}\n"
            ),
        ),
        FixPass(
            inspect=inspectLoopBraces,
            problem="본문에 중괄호가 없는 반복문(for · while)이 있습니다.",
            done="반복문 본문에 중괄호 추가 완료",
            # 다음 줄 본문 · 같은 줄 본문 · 빈 본문을 모두 감싼다.
            badSample=(
                '#include "pch.h"\n\nvoid probe( int32 count )\n{\n'
                "    for ( int32 index = 0; index < count; ++index )\n        doThing( index );\n"
                "    for ( int32 value : listValue ) doThing( value );\n"
                "    while ( poll() );\n}\n"
            ),
            # 줄 끝 공백(`listValue ) `)과 `){}` 의 띄어쓰기는 뒤의 clang-format 이 맞춘다(`LoopBraceRules`) — 이 패스의 결과는 그 앞 글이다.
            fixedSample=(
                '#include "pch.h"\n\nvoid probe( int32 count )\n{\n'
                "    for ( int32 index = 0; index < count; ++index )\n    {\n        doThing( index );\n    }\n"
                "    for ( int32 value : listValue ) \n    {\n        doThing( value );\n    }\n"
                "    while ( poll() ){}\n}\n"
            ),
            # 중괄호가 있는 반복문 · do-while 꼬리 · 머리와 본문 사이에 주석이 낀 반복문(손으로 고친다)은 그대로 둔다.
            goodSample=(
                '#include "pch.h"\n\nvoid probe( int32 count )\n{\n'
                "    for ( int32 index = 0; index < count; ++index )\n    {\n        doThing( index );\n    }\n"
                "    do\n    {\n        doThing( 0 );\n    } while ( poll() );\n"
                "    while ( poll() )\n        // 손으로 고칠 자리\n        doThing( 1 );\n}\n"
            ),
        ),
    )


main = FormatBranchBracesFixer.run


if __name__ == "__main__":
    sys.exit(main())
