#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckPythonMinimumVersion.py

이 저장소의 파이썬이 **CI 가 들고 있는 파이썬에서도 파싱되는지** 검사합니다.

[왜 필요한가 — 로컬에서는 절대 보이지 않는 종류다]
f-string **식 안의 백슬래시**는 Python 3.12 의 PEP 701 부터 허용된다:

    includeFull = f"{includeType}{includeName}{'>' if includeType == '<' else '\\"'}"
    SyntaxError: f-string expression part cannot include a backslash      ← 3.10 에서만

새 파이썬을 쓰는 기계에서는 커밋 훅도 린트도 전부 초록인데, CI 러너(ubuntu-22.04)의 `python3` 는 3.10 이다.
그리고 이런 스크립트는 `sw_executePythonScript` 로 **CMake configure** 중에 돌기도 해서, 거기서 죽으면 설정 자체가
실패해 리눅스 잡 전체가 한 줄도 컴파일하지 못한다.

즉 이것은 "실수했는데 못 봤다" 가 아니라 **"내 기계에서는 볼 방법이 없다"** 는 종류다. 그래서
사람의 주의가 아니라 게이트가 맡는다.

[왜 `ast.parse(feature_version=...)` 이 아닌가 — 해 봤고, 안 잡는다]
`ast.parse(src, feature_version=(3, 10))` 은 이 구문을 **그대로 통과시킨다.** PEP 701 은 문법
규칙이 아니라 **토크나이저**를 바꾼 것이라, 새 토크나이저로 읽는 이상 옛 제약이 되살아나지 않는다
(feature_version 3.10 · 3.11 · 3.12 모두 통과시킨다). 그래서 f-string 의 식 부분을
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

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.Constants import kMinimumPythonVersion, kNotOurCodeDirNames  # noqa: E402
from lint.LintGate import LintGate  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile, SourceScanResult, SourceScanSet  # noqa: E402

#: 정본은 `common.Constants.kMinimumPythonVersion` 이다(CI 러너 ubuntu-22.04 의 `python3`). 여기서 파싱되지 않으면 리눅스 CI 가 멈춘다.
kMinimumVersion = kMinimumPythonVersion

#: 검사에서 빼는 경로 조각 — 남의 코드이거나 생성물이다.
_kExcludedDirName = kNotOurCodeDirNames


def describeMinimumVersionInternal() -> str:
    """`3.10` 같은 문자열."""
    return f"{kMinimumVersion[0]}.{kMinimumVersion[1]}"


def findNewerSyntaxInternal(relativePath: str, source: str) -> list[str]:
    """@brief 파일 하나(저장소 기준 경로 · 글)에서 `kMinimumVersion` 이 거절할 f-string 식을 찾습니다."""
    try:
        tree = ast.parse(source)
    except SyntaxError as error:
        # 지금 인터프리터조차 못 읽는 파일이다 — 버전 문제 이전에 그것부터 말해 준다.
        return [f"{relativePath}:{error.lineno} -> 파싱 실패: {error.msg}"]

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


#: 모듈별로 "이 이름은 이 버전부터"(표준 라이브러리). `kMinimumVersion` 보다 새것만 잡는다.
_kModuleNameSince: dict[str, dict[str, tuple[int, int]]] = {
    "typing": {"Self": (3, 11), "Never": (3, 11), "LiteralString": (3, 11), "assert_never": (3, 11), "assert_type": (3, 11),
               "reveal_type": (3, 11), "Required": (3, 11), "NotRequired": (3, 11), "Unpack": (3, 11), "TypeVarTuple": (3, 11),
               "dataclass_transform": (3, 11), "override": (3, 12), "TypeIs": (3, 13), "ReadOnly": (3, 13),
               "get_protocol_members": (3, 13), "is_protocol": (3, 13), "NoDefault": (3, 13)},
    "enum": {"StrEnum": (3, 11), "ReprEnum": (3, 11), "verify": (3, 11), "member": (3, 11), "nonmember": (3, 11), "EnumCheck": (3, 11)},
    "datetime": {"UTC": (3, 11)},
    "itertools": {"batched": (3, 12)},
    "warnings": {"deprecated": (3, 13)},
    "copy": {"replace": (3, 13)},
    "asyncio": {"TaskGroup": (3, 11), "timeout": (3, 11), "Runner": (3, 11)},
    "contextlib": {"chdir": (3, 11)},
    "math": {"sumprod": (3, 12), "fma": (3, 13), "cbrt": (3, 11), "exp2": (3, 11)},
    "os": {"process_cpu_count": (3, 13)},
    "os.path": {"isjunction": (3, 12), "splitroot": (3, 12)},
    "glob": {"translate": (3, 13)},
    "sys": {"monitoring": (3, 12)},
    "inspect": {"markcoroutinefunction": (3, 12)},
}
#: 모듈 전체가 새것인 것.
_kModuleSince: dict[str, tuple[int, int]] = {"tomllib": (3, 11), "annotationlib": (3, 14), "compression": (3, 14), "string.templatelib": (3, 14)}
#: 내장 이름.
_kBuiltinSince: dict[str, tuple[int, int]] = {"ExceptionGroup": (3, 11), "BaseExceptionGroup": (3, 11), "PythonFinalizationError": (3, 13)}
#: 어느 객체에 걸리든 새 API 뿐인 메서드(받는 객체의 종류를 정적으로 알 수 없어 이름이 겹치지 않는 것만 둔다).
_kMethodSince: dict[str, tuple[int, int]] = {"full_match": (3, 13), "is_junction": (3, 12), "from_uri": (3, 13), "add_note": (3, 11)}
#: 호출에 넘기면 안 되는 키워드 인자: (호출 이름, 키워드) -> 버전.
_kKeywordSince: dict[tuple[str | None, str], tuple[int, int]] = {
    ("read_text", "newline"): (3, 13),
    ("relative_to", "walk_up"): (3, 12),
    ("rmtree", "onexc"): (3, 12),
    ("glob", "recurse_symlinks"): (3, 13),
    ("rglob", "recurse_symlinks"): (3, 13),
    ("glob", "case_sensitive"): (3, 12),
    ("rglob", "case_sensitive"): (3, 12),
    ("add_argument", "deprecated"): (3, 13),
    ("add_subparsers", "deprecated"): (3, 13),
    ("add_parser", "deprecated"): (3, 13),
    ("ArgumentParser", "suggest_on_error"): (3, 14),
    ("ArgumentParser", "color"): (3, 14),
}


def isNewerInternal(since: tuple[int, int]) -> bool:
    """`kMinimumVersion` 보다 새 버전에서야 생긴 기능인가."""
    return since > kMinimumVersion


def callNameInternal(node: ast.Call) -> str | None:
    """호출 대상의 마지막 이름(`a.b.read_text(...)` -> `read_text`)."""
    if isinstance(node.func, ast.Attribute):
        return node.func.attr
    if isinstance(node.func, ast.Name):
        return node.func.id
    return None


def findNewerApiInternal(relativePath: str, source: str) -> list[str]:
    """@brief 파일 하나에서 `kMinimumVersion` 에 없는 표준 라이브러리 이름 · 인자 · 구문을 찾습니다. 파싱이 안 되면 문법 검사가 말하므로 빈 목록."""
    try:
        tree = ast.parse(source)
    except SyntaxError:
        return []

    version = describeMinimumVersionInternal()
    listViolation: list[str] = []

    def report(node: ast.AST, what: str, since: tuple[int, int]) -> None:
        listViolation.append(f"{relativePath}:{getattr(node, 'lineno', 0)} -> {what} 은(는) Python {since[0]}.{since[1]} 부터입니다 (CI 는 {version})")

    # `try: import tomllib / except ModuleNotFoundError:` 는 옛 파이썬에서 대체 경로로 간다 — 가드된 import 는 위반이 아니다.
    setGuardedImport: set[int] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Try) and any(
            isinstance(handler.type, ast.Name) and handler.type.id in ("ImportError", "ModuleNotFoundError") for handler in node.handlers
        ):
            for statement in node.body:
                for inner in ast.walk(statement):
                    if isinstance(inner, (ast.Import, ast.ImportFrom)):
                        setGuardedImport.add(id(inner))

    mapAlias: dict[str, str] = {}   # 지역 이름 -> 모듈 이름
    for node in ast.walk(tree):
        if id(node) in setGuardedImport:
            continue
        if isinstance(node, ast.Import):
            for alias in node.names:
                since = _kModuleSince.get(alias.name)
                if since is not None and isNewerInternal(since):
                    report(node, f"import {alias.name}", since)
                mapAlias[alias.asname or alias.name.split(".")[0]] = alias.name if alias.asname else alias.name.split(".")[0]
        elif isinstance(node, ast.ImportFrom) and node.module and node.level == 0:
            since = _kModuleSince.get(node.module)
            if since is not None and isNewerInternal(since):
                report(node, f"from {node.module} import", since)
            for alias in node.names:
                since = _kModuleSince.get(f"{node.module}.{alias.name}") or _kModuleNameSince.get(node.module, {}).get(alias.name)
                if since is not None and isNewerInternal(since):
                    report(node, f"{node.module}.{alias.name}", since)
        elif isinstance(node, ast.Attribute):
            if isinstance(node.value, ast.Name) and node.value.id in mapAlias:
                module = mapAlias[node.value.id]
                since = _kModuleNameSince.get(module, {}).get(node.attr)
                if since is not None and isNewerInternal(since):
                    report(node, f"{module}.{node.attr}", since)
            since = _kMethodSince.get(node.attr)
            if since is not None and isNewerInternal(since):
                report(node, f".{node.attr}", since)
        elif isinstance(node, ast.Name) and isinstance(node.ctx, ast.Load):
            since = _kBuiltinSince.get(node.id)
            if since is not None and isNewerInternal(since):
                report(node, node.id, since)
        elif isinstance(node, ast.Call):
            name = callNameInternal(node)
            for keyword in node.keywords:
                since = _kKeywordSince.get((name, keyword.arg or ""))
                if since is not None and isNewerInternal(since):
                    report(keyword.value, f"{name}({keyword.arg}=)", since)
        elif type(node).__name__ == "TryStar":
            report(node, "except*", (3, 11))
        elif type(node).__name__ == "TypeAlias":
            report(node, "type 문", (3, 12))
        elif type(node).__name__ == "TemplateStr":
            report(node, "t-string", (3, 14))
        elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)) and getattr(node, "type_params", None):
            report(node, "제네릭 문법 (PEP 695)", (3, 12))
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
        "백슬래시도 줄바꿈도 넣을 수 없습니다 — 식을 변수로 먼저 뽑으세요.\n"
        "  새 표준 라이브러리 이름 · 인자 · 구문(`read_text(newline=)` · `itertools.batched` · `tomllib` · `except*` · `type` 문 …)은\n"
        "  `_kModuleNameSince` · `_kKeywordSince` 표에 있는 것만 잡는다. 표에 없는 새 API 를 쓰면 표에 더한다."
    )
    selfTestCases = [
        {
            "name": "f-string 식 안의 백슬래시",
            "expectedViolation": "Scripts/Probe/BadFString.py:2 -> f-string 식 안의 백슬래시는 Python 3.10 에서 SyntaxError 입니다 (식을 변수로 먼저 뽑으세요): '>' if includeType == '<' else '\\\\\"'",
            "files": {
                "Scripts/Probe/BadFString.py": (
                    'def buildInclude(includeType, includeName):\n'
                    '    return f"{includeType}{includeName}{\'>\' if includeType == \'<\' else \'\\\\"\'}"\n'
                )
            },
        },
        {
            "name": "여러 줄에 걸친 f-string 식",
            "expectedViolation": "Scripts/Probe/MultiLine.py:2 -> 여러 줄에 걸친 f-string 식은 Python 3.10 에서 SyntaxError 입니다",
            "files": {"Scripts/Probe/MultiLine.py": 'kValue = 1\nkText = f"{kValue +\n    1}"\n'},
        },
        {
            "name": "3.13 에서야 생긴 read_text(newline=)",
            "expectedViolation": "Scripts/Probe/NewApi.py:3 -> read_text(newline=) 은(는) Python 3.13 부터입니다",
            "files": {"Scripts/Probe/NewApi.py": 'from pathlib import Path\n\nkText = Path("a").read_text(newline="")\n'},
        },
        {
            "name": "ModuleNotFoundError 로 가드한 tomllib import 는 통과한다 (옆 파일이 잡혀야 위반이 보인다)",
            "expectedViolation": "Scripts/Probe/Unguarded.py:1 -> import tomllib 은(는) Python 3.11 부터입니다",
            "unexpectedViolation": "Scripts/Probe/Guarded.py",
            "files": {
                "Scripts/Probe/Guarded.py": "try:\n    import tomllib\nexcept ModuleNotFoundError:\n    tomllib = None\n",
                "Scripts/Probe/Unguarded.py": "import tomllib\n",
            },
        },
        {
            "name": "표준 라이브러리의 새 이름 (itertools.batched)",
            "expectedViolation": "Scripts/Probe/NewName.py:3 -> itertools.batched 은(는) Python 3.12 부터입니다",
            "files": {"Scripts/Probe/NewName.py": "import itertools\n\nkList = list(itertools.batched([1, 2], 1))\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    sourceScanSets = (SourceScanSet("python", ("",), (".py",), excludedDirNames=_kExcludedDirName),)

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        if source.decodeError:
            return FileFindings(error=source.decodeError)   # 못 읽은 파일을 건너뛰면 검사가 그 파일을 본 척한다
        return FileFindings(listViolation=findNewerSyntaxInternal(source.relative, source.text) + findNewerApiInternal(source.relative, source.text))

    def describeSourceScan(self, result: SourceScanResult) -> str:
        return f"{result.fileCount} python files parsed for Python {describeMinimumVersionInternal()}"


main = CheckPythonMinimumVersionGate.run


if __name__ == "__main__":
    sys.exit(main())
