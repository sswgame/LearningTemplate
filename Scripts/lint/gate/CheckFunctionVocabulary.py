#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
함수 이름 어휘 검사 — 한 개념에 이름 하나.

**같은 일을 하는 함수가 두 이름을 갖는 것은 규칙이 없어서가 아니라 아무도 세지 않아서다.**
이 저장소는 실제로 `queryAABB` 와 `queryAabb`, `alloc*` 과 `allocate*`, `setup*` 과 `initialize*`
를 동시에 갖고 있었다. 읽는 사람은 둘 중 어느 쪽이 맞는지 알 수 없고, 다음 사람은 방금 본 쪽을
따라 쓴다. 그렇게 갈라진다.

규칙은 AGENTS.md "Function names" 에 적혀 있고 여기서 강제한다.

  1) AcronymRun  — 두문자어는 camelCase 낱말 하나다. `initRhi`, `queryAabb`, `bindComputeUav`.
                   대문자가 연달아 셋 이상이거나, 이름 **끝**이 대문자 둘 이상이면 잡는다.
                   (타입 이름 `IRHIDevice` · `AABB` 는 대상이 아니다 — 여기는 camelCase 식별자만 본다.)
  2) BannedVerb  — 한 개념에 동사 하나. `setup`/`startup`/`cleanup` → `initialize`/`shutdown`,
                   `alloc` → `allocate`, `fetch`/`retrieve`/`lookup`/`obtain` → `get`/`find`.
  3) CheckVerb   — `check*` 는 술어가 아니다. bool 이면 `is*`/`has*`, void 면 `assert*` 다.

**헤더만 본다.** 호출부까지 보면 우리 것이 아닌 이름(`vkGetPhysicalDeviceSurfaceCapabilitiesKHR`)
을 잡는다. 선언은 어차피 헤더에 있고, 규칙이 말하는 것도 그 표면이다.

  python Scripts/lint/gate/CheckFunctionVocabulary.py [--root <repo>] [--files <path>...]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import mapConcurrent  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

# 훑을 곳 — 우리가 이름을 정하는 코드만.
_kScanRoots = ("Source", "Test", "Tools/ReflectionParser")
_kHeaderSuffix = (".h", ".hpp", ".inl")

# 선언 한 줄: [지정자]* 반환형 이름( ... — 대입(`=`)이 앞에 오면 호출부이므로 뺀다.
_kDeclRe = re.compile(
    r"^[\t ]*(?:(?:SW_\w*API|static|virtual|inline|constexpr|explicit|friend|\[\[nodiscard\]\])[\t ]+)*"
    r"(?!return\b|else\b|delete\b|new\b|case\b)"
    r"[A-Za-z_][\w:<>,\t &*]*?[\t &*]([a-z]\w*)[\t ]*\("
)

# camelCase 이름 안의 대문자 달리기. 셋 이상은 어디서든, 둘은 이름 끝일 때 잡는다.
#   - `getGLTextureName`("GLT") · `queryAABB`("AABB") · `updateUI`(끝의 "UI") → 위반
#   - `bindVector2DCallback`("DC" = D + Callback) · `isVSyncEnabled`("VS" = V + Sync) → 위반 아님
_kAcronymRunRe = re.compile(r"[A-Z]{3,}|[A-Z]{2,}$")

# 금지 동사 → 써야 할 동사. 접두사 뒤에 대문자가 오거나 이름이 거기서 끝날 때만 본다
# (`allocate` 는 `alloc` + 소문자라 걸리지 않고, `fetch_add` 는 `_` 라 걸리지 않는다).
_kBannedVerb: dict[str, str] = {
    "setup": "initialize",
    "startup": "initialize",
    "teardown": "shutdown",
    "cleanup": "shutdown",
    "alloc": "allocate",
    "dealloc": "free (STL 할당자 계약인 deallocate 는 예외)",
    "dispose": "release / free",
    "fetch": "get / find",
    "retrieve": "get / find",
    "lookup": "find",
    "obtain": "get / acquire",
}
_kBannedVerbRe = re.compile(r"^(" + "|".join(sorted(_kBannedVerb, key=len, reverse=True)) + r")(?=[A-Z0-9]|$)")

_kCheckVerbRe = re.compile(r"^check(?=[A-Z])")

# 규칙보다 오래된 이름 중 **바꾸면 남의 계약이 깨지는 것**만 여기 적는다. 이유 없이 늘리지 말 것.
_kAllowedName: frozenset[str] = frozenset()


def scanFileInternal(filePath: Path, repositoryRoot: Path) -> list[str]:
    relativePath = filePath.relative_to(repositoryRoot).as_posix()
    try:
        text = filePath.read_text(encoding="utf-8", errors="replace")
    except OSError as exception:
        return [f"{relativePath}: 읽기 실패: {exception}"]

    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    violations: list[str] = []
    seenName: set[str] = set()

    for lineIndex, rawLine in enumerate(text.split("\n"), 1):
        line = re.sub(r"//.*$", "", rawLine)
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if "=" in line.split("(", 1)[0]:
            continue
        match = _kDeclRe.match(line)
        if match is None:
            continue

        name = match.group(1)
        if name in _kAllowedName or name in seenName:
            continue
        seenName.add(name)

        if _kAcronymRunRe.search(name):
            violations.append(
                f"{relativePath}:{lineIndex}: [AcronymRun] '{name}' — 두문자어는 camelCase 낱말 하나로 씁니다"
                f" (예: RHI→Rhi, AABB→Aabb, UAV→Uav, API→Api, UI→Ui)."
            )

        verbMatch = _kBannedVerbRe.match(name)
        if verbMatch is not None:
            verb = verbMatch.group(1)
            violations.append(
                f"{relativePath}:{lineIndex}: [BannedVerb] '{name}' — '{verb}' 대신 '{_kBannedVerb[verb]}' 를 씁니다."
            )

        if _kCheckVerbRe.match(name):
            violations.append(
                f"{relativePath}:{lineIndex}: [CheckVerb] '{name}' — check 는 술어가 아닙니다."
                f" bool 이면 is*/has*, void 로 단언하면 assert* 입니다."
            )

    return violations


def collectHeadersInternal(repositoryRoot: Path, explicitFiles: list[str] | None) -> list[Path]:
    # `--files` 를 빈 목록으로 준 것(= staged 헤더 없음)과 아예 주지 않은 것(= 전수 검사)은 다르다.
    if explicitFiles is not None:
        return [Path(one).resolve() for one in explicitFiles if one.endswith(_kHeaderSuffix)]
    found: list[Path] = []
    for scanRoot in _kScanRoots:
        rootPath = repositoryRoot / scanRoot
        if not rootPath.is_dir():
            continue
        for suffix in _kHeaderSuffix:
            found.extend(rootPath.rglob(f"*{suffix}"))
    return sorted(found)


class CheckFunctionVocabularyGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있어 어긋날 수 없다."""

    description = "함수 이름 어휘 검사"
    violationHeader = "이름 규칙 위반"
    hint = "\n규칙은 AGENTS.md 의 'Function names' 절에 있습니다."
    selfTestCases = [
        {
            "name": "두문자어가 대문자로 달린다",
            "files": {"Source/Engine/Probe.h": "namespace sw\n{\n    struct Probe\n    {\n        void queryAABB( int32 a );\n    };\n}\n"},
        },
        {
            "name": "initialize 대신 setup",
            "files": {"Source/Engine/Probe.h": "namespace sw\n{\n    struct Probe\n    {\n        bool setupLocalization( int32 a );\n    };\n}\n"},
        },
        {
            "name": "allocate 대신 alloc",
            "files": {"Source/Engine/Probe.h": "namespace sw\n{\n    struct Probe\n    {\n        void* allocSrvDescriptor( int32 a );\n    };\n}\n"},
        },
        {
            "name": "술어가 check 로 시작한다",
            "files": {"Source/Engine/Probe.h": "namespace sw\n{\n    struct Probe\n    {\n        bool checkCollision( int32 a ) const;\n    };\n}\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--files", nargs="*", default=None, help="이 파일들만 검사 (pre-commit 용)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        headers = collectHeadersInternal(repositoryRoot, args.files)
        if not headers:
            return GateResult(summary="검사할 헤더가 없습니다")

        violations: list[str] = []
        for fileViolations in mapConcurrent(lambda path: scanFileInternal(path, repositoryRoot), headers):
            violations.extend(fileViolations)
        return GateResult(listViolation=violations, summary=f"{len(headers)} headers scanned")


main = CheckFunctionVocabularyGate.run


if __name__ == "__main__":
    sys.exit(main())
