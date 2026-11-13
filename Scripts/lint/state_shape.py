#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/state_shape.py

런타임 상태를 손으로 짝지어 쓰고 읽는 모양을 막는다. 2026-11-07 — 125 형식이 `writeState` · `readState` 두 함수의 칸 순서를 사람이 맞췄고, 바이트 약속은 형식이 아니라 관례였다.
**왜 형식이 아니라 린트인가.** 길은 형식으로 냈다(`StateIO`, `SW_DECLARE_STATE_IO`), 그리고 스냅샷(`ComponentStateStore::capture`)은 `StateIO::writeObject` 로 부르므로
`serializeState` 없는 컴포넌트는 꽂을 수 없다. 그러나 `Archive` 는 엔진 곳곳(에셋 · 넷 · 세이브 · 되돌리기)이 바이트 그릇으로 쓰고 `StateIO` 의 인라인 칸 함수가 그
완전한 형식을 요구해 숨길 수 없다 — 그것을 받는 멤버 짝을 새로 짓는 것은 형식으로 막지 못한다. 예외 파일은 두지 않는다(마지막 둘 — 지형 · 카메라 디렉터 — 을 2026-11-10 옮겼다).

- `writeState( Archive& …` · `readState( Archive& …` 의 선언과 .cpp 정의는 위반이다. 형식은 `SW_DECLARE_STATE_IO()` 로 받고 본문은 `serializeState( StateIO& io )` 하나에 적는다.
- 머리의 인라인 정의는 `StateIO` 입구로 넘길 때만 된다(읽기에 카탈로그가 더 필요한 형식, 가상 본문을 쓰는 기반).

`py -3 -m Scripts lint state-shape [--files …|--all|--self-check]` 로 돌리고, 커밋 훅(`rules.py` 표)이 staged 파일에 돌린다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from lint.cpp_shape import is_target, strip_code  # noqa: E402
from lint.lint_scope import LintScope, make_file_scope, make_whole_scope, read_text  # noqa: E402

ENTRY_FILE = "Source/Engine/Serialization/Format/StateIO.h"
SIGNATURE = re.compile(r"\b(?:(\w+)\s*::\s*)?(writeState|readState)\s*\(\s*(?:sw::|::sw::)?Archive\s*&[^)]*\)\s*(?:const\s*)?(?:override\s*)?(?:=\s*0\s*)?([;{])")
ENTRY_CALL = re.compile(r"\bStateIO\s+\w+\s*\(|\bStateIO::(?:writeObject|readAtomic|readInPlace)\s*\(")


def inspect_text(text: str, relative: str) -> list[str]:
    """글 하나를 봅니다(자체 시험이 파일 없이 부를 수 있게 나눴다)."""
    list_violation: list[str] = []
    code = strip_code(text)
    for match in SIGNATURE.finditer(code):
        line = code.count("\n", 0, match.start()) + 1
        owner, name, end = match.group(1), match.group(2), match.group(3)
        if owner is not None or end == ";":
            list_violation.append(f"{relative}:{line}: 손으로 짝지은 `{name}( Archive& )` — `SW_DECLARE_STATE_IO()` 와 `serializeState( StateIO& io )` 하나로 적는다")
            continue
        if ENTRY_CALL.search(code[match.end():match.end() + 200]) is None:
            list_violation.append(f"{relative}:{line}: 인라인 `{name}( Archive& )` 는 `StateIO` 입구로만 넘긴다")
    return list_violation


def check_state_shape(project_root: Path, scope: LintScope) -> list[str]:
    """@p listFile 가운데 Source 의 C++ 파일을 보고 어긋남을 `경로:줄: 설명` 으로 돌려준다. 테스트는 일부러 꾸민 바이트를 다루므로 보지 않습니다."""
    list_file = scope.list_absolute(project_root, (".h", ".cpp"))
    list_violation: list[str] = []
    for file in list_file:
        path = Path(file).resolve()
        try:
            relative = path.relative_to(project_root).as_posix()
        except ValueError:
            continue
        text = read_text(path, errors="replace") if is_target(relative) and "/Tests/" not in relative and relative != ENTRY_FILE else None
        if text is not None:
            list_violation.extend(inspect_text(text, relative))
    return list_violation


def run_self_check() -> None:
    """규칙이 잡을 것을 잡고 거른 것을 놓아 주는지 봅니다."""
    assert len(inspect_text("void A::writeState( Archive& outArchive ) const\n{\n}\n", "a.cpp")) == 1
    assert len(inspect_text("bool A::readState( sw::Archive& archive )\n{\n}\n", "a.cpp")) == 1
    assert len(inspect_text("void writeState( Archive& outArchive ) const;\n", "a.h")) == 1
    assert len(inspect_text("void writeState( Archive& outArchive ) const { outArchive << _x; }\n", "a.h")) == 1
    assert inspect_text("void writeState( Archive& outArchive ) const { StateIO::writeObject( *this, outArchive ); }\n", "a.h") == []
    assert inspect_text("bool readState( Archive& archive, const C& c )\n{\n    StateIO io( archive );\n    return io.isOk();\n}\n", "a.h") == []
    assert inspect_text("void A::serializeState( StateIO& io )\n{\n}\nSW_DECLARE_STATE_IO();\n", "a.cpp") == []
    assert inspect_text("// void writeState( Archive& outArchive ) const;\n", "a.h") == []
    assert inspect_text("void writeState( BitWriter& writer ) const;\n", "a.h") == []


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="손으로 짝지은 writeState · readState( Archive& ) 를 본다")
    parser.add_argument("--files", nargs="*", default=None, help="볼 파일(없으면 Source 전체)")
    parser.add_argument("--all", action="store_true", help="Source 전체")
    parser.add_argument("--self-check", action="store_true", help="규칙 자체를 시험한다")
    args = parser.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")
    if args.self_check:
        run_self_check()
        print("[StateShape] self-check OK")
        return 0
    project_root = Path(__file__).resolve().parents[2]
    list_violation = check_state_shape(project_root, make_file_scope(project_root, args.files) if args.files else make_whole_scope(project_root))
    for line in list_violation:
        print(line)
    print(f"[StateShape] {'FAIL' if list_violation else 'OK'} — {len(list_violation)}건")
    return 1 if list_violation else 0


if __name__ == "__main__":
    sys.exit(main())
