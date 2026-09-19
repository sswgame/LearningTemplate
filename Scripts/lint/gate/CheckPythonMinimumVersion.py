#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckPythonMinimumVersion.py

이 저장소의 파이썬이 **CI 가 들고 있는 파이썬에서도 파싱되는지** 검사합니다.

[왜 필요한가 — 로컬에서는 절대 보이지 않는 종류다]
2026-09-19 에 리눅스 CI 가 통째로 멈췄다. 빌드도 테스트도 아니고 **CMake configure** 에서였다:

    File "Scripts/lint/gate/CheckIncludeOrder.py", line 156
        includeFull = f"{includeType}{includeName}{'>' if includeType == '<' else '\\"'}"
    SyntaxError: f-string expression part cannot include a backslash

f-string **식 안의 백슬래시**는 Python 3.12 의 PEP 701 부터 허용된다. 그 줄을 쓴 기계의 파이썬은
3.14 라 아무 문제가 없었고, 커밋 훅도 린트도 전부 초록이었다. CI 러너(ubuntu-22.04)의 `python3`
만 3.10 이라 거기서만 죽었다 — 그리고 그 자리가 `sw_executePythonScript` 라서 **설정 자체가
실패**해, 리눅스 잡 넷이 한 줄도 컴파일하지 못하고 끝났다.

즉 이것은 "실수했는데 못 봤다" 가 아니라 **"내 기계에서는 볼 방법이 없다"** 는 종류다. 그래서
사람의 주의가 아니라 게이트가 맡는다.

[왜 `ast.parse(feature_version=...)` 이 아닌가 — 해 봤고, 안 잡는다]
`ast.parse(src, feature_version=(3, 10))` 은 이 구문을 **그대로 통과시킨다.** PEP 701 은 문법
규칙이 아니라 **토크나이저**를 바꾼 것이라, 새 토크나이저로 읽는 이상 옛 제약이 되살아나지 않는다.
(2026-09-19 에 3.10·3.11·3.12 셋 다 통과하는 것을 확인했다.) 그래서 f-string 의 식 부분을
**소스에서 직접 잘라** 본다.

[무엇을 잡는가]
`kMinimumVersion` 보다 새 파이썬에서만 쓸 수 있는 f-string 식을 잡는다:

  - 식 안의 **백슬래시** (`f"{x if y else '\\"'}"`)
  - 식이 **여러 줄**에 걸친 것

둘 다 PEP 701 이전에는 `SyntaxError` 다. 잡는 방법은 간단하다 — 식의 소스 조각을 그대로 보면 된다.

[고치는 법은 늘 같다]
**식을 변수로 먼저 뽑는다.** 그러면 백슬래시도 줄바꿈도 f-string 밖에 있게 된다:

    closingBracket = ">" if includeType == "<" else '"'
    includeFull = f"{includeType}{includeName}{closingBracket}"
"""

from __future__ import annotations

import argparse
import ast
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from LintGate import GateResult, LintGate  # noqa: E402

#: CI 러너(ubuntu-22.04)의 `python3` 가 이 버전이다. 여기서 파싱되지 않으면 리눅스 CI 가 멈춘다.
kMinimumVersion = (3, 10)

#: 검사에서 빼는 경로 조각 — 남의 코드이거나 생성물이다.
_kExcludedPart = ("vcpkg", "build", "generated", ".git", "__pycache__", ".venv", "ThirdParty", "Tools")


def describeMinimumVersionInternal() -> str:
    """`3.10` 같은 문자열."""
    return f"{kMinimumVersion[0]}.{kMinimumVersion[1]}"


def findNewerSyntaxInternal(path: Path, repositoryRoot: Path) -> list[str]:
    """@brief 파일 하나에서 `kMinimumVersion` 이 거절할 f-string 식을 찾습니다."""
    try:
        source = path.read_text(encoding="utf-8")
    except OSError:
        return []

    try:
        tree = ast.parse(source)
    except SyntaxError as error:
        # 지금 인터프리터조차 못 읽는 파일이다 — 버전 문제 이전에 그것부터 말해 준다.
        return [f"{path.relative_to(repositoryRoot).as_posix()}:{error.lineno} -> 파싱 실패: {error.msg}"]

    listViolation: list[str] = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.JoinedStr):
            continue

        for part in node.values:
            if not isinstance(part, ast.FormattedValue):
                continue

            segment = ast.get_source_segment(source, part.value)
            if segment is None:
                continue

            relativePath = path.relative_to(repositoryRoot).as_posix()
            if "\\" in segment:
                listViolation.append(
                    f"{relativePath}:{part.value.lineno} -> f-string 식 안의 백슬래시는 "
                    f"Python {describeMinimumVersionInternal()} 에서 SyntaxError 입니다 "
                    f"(식을 변수로 먼저 뽑으세요): {segment.strip()[:60]}"
                )
            elif "\n" in segment:
                listViolation.append(
                    f"{relativePath}:{part.value.lineno} -> 여러 줄에 걸친 f-string 식은 "
                    f"Python {describeMinimumVersionInternal()} 에서 SyntaxError 입니다 "
                    f"(식을 변수로 먼저 뽑으세요)"
                )
    return listViolation


class CheckPythonMinimumVersionGate(LintGate):
    """로컬 파이썬이 새것이라 보이지 않는 구문 — CI 의 파이썬이 거절할 것을 미리 잡는다."""

    description = f"파이썬 스크립트가 Python {describeMinimumVersionInternal()} 에서도 파싱되는지 검사"
    buildComment = "Checking Python scripts parse on the CI interpreter..."
    timeoutSeconds = 30
    preCommitPattern = ("*.py",)
    preCommitFileArgument = "--files"
    violationHeader = f"Python {describeMinimumVersionInternal()} 에서 파싱되지 않는 구문"
    hint = (
        f"  CI 러너의 python3 는 {describeMinimumVersionInternal()} 입니다. f-string **식** 안에는 "
        "백슬래시도 줄바꿈도 넣을 수 없습니다 — 식을 변수로 먼저 뽑으세요."
    )
    selfTestCases = [
        {
            "name": "f-string 식 안의 백슬래시",
            "files": {
                "Scripts/Probe/BadFString.py": (
                    'def buildInclude(includeType, includeName):\n'
                    '    return f"{includeType}{includeName}{\'>\' if includeType == \'<\' else \'\\\\"\'}"\n'
                )
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--files", nargs="*", default=None, help="검사할 파일 (생략 시 전체)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        if args.files:
            listPath = [Path(item).resolve() for item in args.files]
            listPath = [path for path in listPath if path.suffix == ".py" and path.is_file()]
        else:
            listPath = sorted(
                path
                for path in repositoryRoot.rglob("*.py")
                if not any(part in _kExcludedPart for part in path.relative_to(repositoryRoot).parts)
            )

        listViolation: list[str] = []
        for path in listPath:
            listViolation.extend(findNewerSyntaxInternal(path, repositoryRoot))

        return GateResult(
            listViolation=listViolation,
            summary=f"{len(listPath)} python files parsed for Python {describeMinimumVersionInternal()}",
        )


main = CheckPythonMinimumVersionGate.run


if __name__ == "__main__":
    sys.exit(main())
