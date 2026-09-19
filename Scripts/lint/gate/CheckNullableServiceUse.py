#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
nullptr 을 돌려줄 수 있는 조회 함수를 곧바로 `->` 로 따라가는 곳을 잡는다.

에디터의 `editor::getService<T>()` 는 **문서대로 nullptr 을 돌려줄 수 있다** — 지역 등록도
없고 모듈 서비스 표에도 없으면 그렇다. 그런데 2026-09-18 에 세어 보니 열세 자리가 그 값을
확인 없이 `->` 로 따라가고 있었다. 커맨드 스택처럼 EditorModule 보다 오래 사는 서비스는
종료할 때 결합이 먼저 풀리므로, 그 창에 걸리면 널 역참조다.

같은 파일 안에서 어떤 호출부는 확인하고 어떤 호출부는 확인하지 않는 것이 문제의 모양이었다
(`EditorTransaction::push` 는 확인했고 그 위 일곱은 안 했다). 사람이 지킬 규칙이 아니라
기계가 볼 규칙으로 옮긴다.

**같은 함정에 문이 하나 더 있었다.** `EditorContext::get()` 은 속으로
`getService<EditorContext>()` 를 부르고 없으면 정적 폴백을 돌려주므로 이것도 nullptr 이 될 수
있다. 2026-09-20 에 세어 보니 여든두 자리는 받아서 확인하는데 **쉰두 자리가 그대로 `->` 로
따라가고 있었다** — 이 린트가 잡던 것과 정확히 같은 모양인데 이름만 달라서 지나갔다.

**세 번째 문은 "받아 두고 확인은 안 하는" 것이다.** `T* p = getService<T>();` 라고 적어 두면
이 린트의 한 줄짜리 정규식은 통과하지만, 그 뒤에 확인 없이 `p->` 를 쓰면 결과는 같다. 같은 날
백세 자리 중 **여섯 자리**가 그랬다. 그래서 선언 뒤를 함수 끝까지 훑어 "확인이 먼저인가
역참조가 먼저인가" 를 본다 — 확인으로 치는 모양은 아래 `_kCheckTemplate` 에 적혀 있다.

  python Scripts/lint/gate/CheckNullableServiceUse.py [--root <repo>] [--files a.cpp b.cpp]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import collectSourceFiles, normalizePath  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

# 곧바로 화살표가 붙는 모양만 잡는다 — 포인터를 받아 두고 확인한 뒤 쓰는 형태
# (`T* p = getService<T>();`)는 걸리지 않는다.
#
#   getService<T>()->        · getService< T >()->
#   EditorContext::get()->   — 속으로 getService<EditorContext>() 를 부르는 같은 함정의 다른 문
_kListNullableCallRe = (
    re.compile(r"\bgetService\s*<[^<>()]{1,80}>\s*\(\s*\)\s*->"),
    re.compile(r"\bEditorContext\s*::\s*get\s*\(\s*\)\s*->"),
)

# 세 번째 문: 받아 두기만 하고 확인하지 않는 것.
#   T* pName = getService<T>();   ·   EditorContext* pName = EditorContext::get();
_kNullableDeclRe = re.compile(
    r"^(\s*)(?:auto|[A-Za-z_][\w:<>,\* ]*?)\*\s+(p[A-Z]\w*)\s*=\s*"
    r"(?:(?:editor|game)::)?(?:getService\s*<|EditorContext\s*::\s*get\s*\()"
)

# 확인으로 치는 모양. 이 중 하나가 역참조보다 먼저 나오면 통과다.
_kCheckTemplate = (
    r"({0}\s*==\s*nullptr|{0}\s*!=\s*nullptr|nullptr\s*==\s*{0}|nullptr\s*!=\s*{0}"
    r"|if\s*\(\s*!?\s*{0}\s*\)|{0}\s*&&|&&\s*!?\s*{0}\b|{0}\s*\?"
    r"|\|\|\s*!\s*{0}\b|!\s*{0}\s*\|\||SW_ASSERT[A-Z_]*\s*\([^)]*{0}\b)"
)
_kDerefTemplate = r"(\b{0}\s*->|\*\s*{0}\b)"


def findUncheckedStores(relative: str, lines: list[str]) -> list[str]:
    """받아 두고 확인 없이 역참조하는 곳을 모읍니다."""
    violations: list[str] = []
    for index, line in enumerate(lines):
        match = _kNullableDeclRe.match(line)
        if not match:
            continue
        indent, name = match.group(1), match.group(2)
        checkRe = re.compile(_kCheckTemplate.format(re.escape(name)))
        derefRe = re.compile(_kDerefTemplate.format(re.escape(name)))
        for scanIndex in range(index + 1, len(lines)):
            current = lines[scanIndex]
            stripped = current.strip()
            # 선언보다 얕은 들여쓰기의 `}` 면 그 블록(대개 함수)이 끝난 것이다.
            if stripped.startswith("}") and (len(current) - len(current.lstrip())) < len(indent):
                break
            if checkRe.search(current):
                break
            if derefRe.search(current):
                violations.append(f"[Nullable Service] {relative}:{index + 1}: {line.strip()}")
                break
    return violations

# 같은 함정이 세 곳에 있다 — 에디터의 `editor::getService`, 게임의 `game::getService`.
# 게임 쪽은 `SW_ASSERT( false )` 를 거치는데 **그 단정은 Shipping 에서 사라진다.**
_kListScanRoot = ( "Source/Editor", "Source/GameFramework", "Source/Games" )


def findDirectDereferences(repositoryRoot: Path, listTargetFile: list[str] | None) -> list[str]:
    """`getService<T>()->` 꼴을 모아 위반 문자열로 돌려줍니다."""
    if listTargetFile:
        listPath = [repositoryRoot / f for f in listTargetFile]
    else:
        listPath = collectSourceFiles([repositoryRoot / r for r in _kListScanRoot], {".cpp", ".h"})

    violations: list[str] = []
    for path in listPath:
        if not path.exists() or path.is_dir():
            continue
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        if not any(relative.startswith(r) for r in _kListScanRoot):
            continue

        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue

        lines = text.splitlines()
        for lineIndex, line in enumerate(lines, start=1):
            if any(regex.search(line) for regex in _kListNullableCallRe):
                violations.append(f"[Nullable Service] {relative}:{lineIndex}: {line.strip()}")
        violations.extend(findUncheckedStores(relative, lines))
    return violations


class CheckNullableServiceUseGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "nullptr 가능 서비스 조회를 확인 없이 역참조하는 곳 검사"
    buildComment = "Checking nullable editor service dereferences..."
    timeoutSeconds = 15
    preCommitPattern = ("Source/Editor/*", "Source/GameFramework/*", "Source/Games/*")
    preCommitFileArgument = "--files"
    violationHeader = "확인 없이 역참조한 서비스 조회"
    hint = (
        "  editor::getService<T>() 와 EditorContext::get() 은 nullptr 을 돌려줄 수 있습니다.\n"
        "  포인터를 받아 두고 확인한 뒤 쓰십시오:\n"
        "      CommandStack* pStack = editor::getService<CommandStack>();\n"
        "      if ( pStack == nullptr )\n"
        "          return;\n"
        "  나중에 불리는 람다·델리게이트 안에서는 바깥 포인터를 쓰지 말고 그 자리에서 다시 받으십시오."
    )
    selfTestCases = [
        {
            "name": "서비스 조회를 바로 역참조",
            "files": {
                "Source/Editor/Probe/ProbeService.cpp": (
                    "void probe()\n"
                    "{\n"
                    "    editor::getService<CommandStack>()->undo();\n"
                    "}\n"
                ),
            },
        },
        {
            "name": "EditorContext::get() 을 바로 역참조",
            "files": {
                "Source/Editor/Probe/ProbeContext.cpp": (
                    "void probe()\n"
                    "{\n"
                    "    EditorContext::get()->getWorkspace().clearSelection();\n"
                    "}\n"
                ),
            },
        },
        {
            "name": "받아 두고 확인 없이 역참조",
            "files": {
                "Source/Editor/Probe/ProbeStored.cpp": (
                    "void probe()\n"
                    "{\n"
                    "    InputManager* pInput = getService<InputManager>();\n"
                    "    pInput->update();\n"
                    "}\n"
                ),
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--files", nargs="*", default=None, help="검사할 특정 파일 (생략 시 Editor · GameFramework · Games 전체)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        violations = findDirectDereferences(repositoryRoot, args.files)
        return GateResult(listViolation=violations, summary="Editor · GameFramework · Games 의 서비스 조회 역참조")


main = CheckNullableServiceUseGate.run


if __name__ == "__main__":
    sys.exit(main())
