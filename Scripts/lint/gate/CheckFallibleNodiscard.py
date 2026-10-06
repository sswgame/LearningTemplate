#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""실패를 bool 로 알리는 함수 선언에 `[[nodiscard]]` 가 있는지 본다 — 결과를 버리는 호출을 컴파일러가 짚게.

주의: 가장 흔한 결함 모양 하나가 **조용한 실패**다. 읽기 · 쓰기 · 적용의 false 를 부른 쪽이 버리면 성공한 것처럼 진행한다 —
읽기 실패를 버린 되돌리기는 빈 인스턴스를 만들고, 컴파일 실패를 버린 쿠킹은 낡은 셰이더 바이너리를 최신으로 도장 찍고,
깨진 언어 파일을 읽은 척하면 빈 칸으로 다시 써 번역이 지워진다.

규칙: 이름이 아래 동사로(또는 `re` + 동사로 — `recreate` · `reopen`) 시작하고 bool 을 돌려주는 선언은 `[[nodiscard]]` 를 단다. 컴파일러는 `-Werror=unused-result`
(cmake/Modules/Compiler/Clang.cmake · GCC.cmake)로 버리는 호출에서 빌드를 세운다. 일부러 버릴 때는 `(void)호출();` 과 이유 한 줄.

  load · save · read · write · parse · deserialize · serialize · apply · restore · import · export · cook · compile
  revert · convert · try · open · attach · spawn · instantiate · reload
  remove · copy · create · delete · move · rename

동사의 뜻은 "false = 그 일이 일어나지 않았다" 다. 할 일이 없어 true 인 것(`FileUtil::removeFile` 은 이미 없으면 true)도, 없어서 false 인
컨테이너 도우미(`VectorUtil::removeSingleSwap`)도 그 뜻 안에 있다 — 버려도 되는 자리는 `(void)` 와 이유로 그렇다고 적는다.

린트 대상(Source · Tools · Test 의 C++ — `kLintTargetRelDirs`)의 헤더뿐 아니라 `.cpp` · `.inl` 도 본다 — 번역 단위 지역 함수(익명 네임스페이스 안 · `static`)도 결과를 버리면 같은 결함이다.
`.cpp` 에서 이름 있는 네임스페이스에 둔 정의(`namespace sw::internal { bool tryX() … }`)와 `Foo::loadX(` 같은 멤버 정의는 헤더에 선언된
것을 정의하는 자리라 보지 않는다 — 속성은 헤더 선언이 든다. 반환 타입만 한 줄에 두고 이름을 다음 줄에 쓴
선언도 본다. `[[nodiscard]]` 는 같은 줄이나, 주석 · `template <…>` 줄을 건너뛴 바로 위 줄에 있으면 된다.

`friend` 선언(정의가 아니면 속성을 달 수 없다)과 C-ABI 계약(`Source/RuntimeAPI`)은 보지 않는다.

  python Scripts/lint/gate/CheckFallibleNodiscard.py [--root <repo>] [--files a.h b.cpp]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import kLintTargetRelDirs, normalizePath  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kListFallibleVerb = (
    "load", "save", "read", "write", "parse", "deserialize", "serialize", "apply", "restore", "import", "export",
    "cook", "compile", "revert", "convert", "try", "open", "attach", "spawn", "instantiate", "reload",
    "remove", "copy", "create", "delete", "move", "rename",
)

# 실패할 수 있는 동사로(또는 `re` + 그 동사로) 시작하는 이름.
_kFallibleNamePattern = r"(?P<name>(?:re)?(?:" + "|".join(_kListFallibleVerb) + r")(?:[A-Z0-9]\w*)?)"
_kSpecifierPattern = r"(?:(?:static|virtual|inline|constexpr|SW_API|SW_GF_API|SW_MODULE_API)\s+)*"

# 줄 머리의 지정자(static · virtual · 내보내기 매크로) 다음 `bool 이름(` — 이름이 실패할 수 있는 동사로 시작하는 선언.
_kDeclarationRe = re.compile(r"^\s*" + _kSpecifierPattern + r"bool\s+" + _kFallibleNamePattern + r"\s*\(")
# 반환 타입만 있는 줄(`static bool`)과, 이름으로 시작하는 다음 줄 — 둘로 쪼갠 선언.
_kReturnTypeOnlyRe = re.compile(r"^\s*" + _kSpecifierPattern + r"bool\s*$")
_kNameOnlyRe = re.compile(r"^\s*" + _kFallibleNamePattern + r"\s*\(")
# `[[nodiscard]]` 를 찾을 때 건너뛰는 윗줄 — 주석과 템플릿 머리.
_kSkippedAboveRe = re.compile(r"^\s*(?://|/\*|\*|template\s*<)")

#: 린트 대상 전체(Source · Tools · Test 의 C++) — 시험 도우미의 false 를 버리면 시험이 통과한 척 한다.
_kListScanRoot = kLintTargetRelDirs
_kListScanSuffix = (".h", ".cpp", ".inl")


def hasNodiscardAboveInternal(listLine: list[str], lineIndex: int) -> bool:
    """`lineIndex` 줄 바로 위(주석 · 템플릿 머리는 건너뛴다)가 `[[nodiscard]]` 로 끝나는지 봅니다."""
    aboveIndex = lineIndex - 1
    while aboveIndex >= 0 and (_kSkippedAboveRe.match(listLine[aboveIndex]) is not None or listLine[aboveIndex].strip() == ""):
        if listLine[aboveIndex].strip().endswith("[[nodiscard]]"):
            return True
        aboveIndex -= 1
    return aboveIndex >= 0 and listLine[aboveIndex].strip().endswith("[[nodiscard]]")


_kAnonymousNamespaceLines = ("namespace", "namespace {", "namespace{")
_kStringLiteralRe = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
_kLineCommentRe = re.compile(r"//.*$|/\*.*?\*/")


def findTranslationUnitLocalLinesInternal(listLine: list[str]) -> set[int]:
    """`.cpp` 에서 익명 네임스페이스 안에 있는 줄 번호(0 부터)를 모읍니다 — 중괄호 깊이로 블록의 끝까지."""
    setLocalLine: set[int] = set()
    depth = 0
    listAnonymousDepth: list[int] = []
    bPendingAnonymous = False
    for lineIndex, line in enumerate(listLine):
        if listAnonymousDepth:
            setLocalLine.add(lineIndex)
        if line.strip() in _kAnonymousNamespaceLines:
            bPendingAnonymous = True
        for ch in _kLineCommentRe.sub("", _kStringLiteralRe.sub("", line)):
            if ch == "{":
                depth += 1
                if bPendingAnonymous:
                    listAnonymousDepth.append(depth)
                    bPendingAnonymous = False
            elif ch == "}":
                if listAnonymousDepth and listAnonymousDepth[-1] == depth:
                    listAnonymousDepth.pop()
                depth -= 1
    return setLocalLine


def findFallibleNameInternal(listLine: list[str], lineIndex: int) -> str | None:
    """이 줄에서 시작하는 실패 가능 bool 선언의 이름 — 한 줄짜리든, 반환 타입과 이름을 두 줄로 쪼갠 것이든. 아니면 None."""
    line = listLine[lineIndex]
    match = _kDeclarationRe.match(line)
    if match is not None:
        return match.group("name")
    if _kReturnTypeOnlyRe.match(line) is not None and lineIndex + 1 < len(listLine):
        nameMatch = _kNameOnlyRe.match(listLine[lineIndex + 1])
        if nameMatch is not None:
            return nameMatch.group("name")
    return None


def findFallibleDeclarationsWithoutNodiscard(repositoryRoot: Path, listTargetFile: list[str] | None) -> list[str]:
    """`[[nodiscard]]` 가 없는 실패 가능 bool 선언을 모아 위반 문자열로 돌려줍니다."""
    gate = CheckFallibleNodiscardGate
    violations: list[str] = []
    listPath = LintGate.selectTargetFiles(repositoryRoot, listTargetFile, listScanRoot=_kListScanRoot, suffixes=_kListScanSuffix)
    for path, text in LintGate.readFiles(listPath):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        # 예외 키는 폴더 패턴(`Source/RuntimeAPI/*`)이거나 `<파일>:<이름>` 하나다.
        folderKey = gate.findExemptionKey(relative)
        if folderKey is not None:
            gate.seeExemption(folderKey)
        listLine = text.splitlines()
        # 헤더가 아니면 번역 단위 지역 함수(익명 네임스페이스 안 · static)만 본다 — 나머지는 헤더 선언의 정의다.
        bHeader = path.suffix.lower() == ".h"
        setLocalLine = set() if bHeader else findTranslationUnitLocalLinesInternal(listLine)

        for lineIndex, line in enumerate(listLine):
            name = findFallibleNameInternal(listLine, lineIndex)
            if name is None or "[[nodiscard]]" in line or hasNodiscardAboveInternal(listLine, lineIndex):
                continue
            if bHeader is False and lineIndex not in setLocalLine and re.match(r"^\s*static\b", line) is None:
                continue
            declarationKey = f"{relative}:{name}"
            if folderKey is not None or declarationKey in gate.mapExemption:
                gate.useExemption(folderKey if folderKey is not None else declarationKey)
                continue
            violations.append(f"[Fallible Nodiscard] {relative}:{lineIndex + 1}: {name} — {line.strip()}")

    return violations


class CheckFallibleNodiscardGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    #: 폴더 패턴(fnmatch) 또는 `<파일>:<이름>` → 속성을 달지 않는 까닭.
    mapExemption = {
        "Source/RuntimeAPI/*": "C-ABI 계약(extern \"C\" 함수 포인터 표) — 속성을 달 자리가 아니다",
    }

    description = "실패 가능 bool 함수의 [[nodiscard]] 검사"
    buildComment = "Checking fallible bool declarations for [[nodiscard]]..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*{suffix}" for root in _kListScanRoot for suffix in _kListScanSuffix)
    preCommitFileArgument = "--files"
    violationHeader = "실패를 bool 로 알리는데 [[nodiscard]] 가 없는 선언"
    hint = (
        "  실패할 수 있는 동사(load · save · parse · apply …)의 bool 함수는 [[nodiscard]] 를 답니다:\n"
        "      [[nodiscard]] static bool loadFromFile( string_view path );\n"
        "  그러면 결과를 버리는 호출에서 빌드가 섭니다(-Werror=unused-result). 일부러 버릴 때는 (void)호출(); 과 이유 한 줄."
    )
    selfTestCases = [
        {
            "name": "실패 가능 동사의 bool 선언에 [[nodiscard]] 없음",
            "files": {
                "Source/Probe/ProbeLoader.h": (
                    "class ProbeLoader\n"
                    "{\n"
                    "public:\n"
                    "    static bool loadFromFile( string_view path );\n"
                    "};\n"
                ),
            },
        },
        {
            "name": "내보내기 매크로 · virtual 이 붙은 선언",
            "files": {
                "Source/Probe/ProbeWriter.h": (
                    "class SW_API ProbeWriter\n"
                    "{\n"
                    "public:\n"
                    "    virtual bool saveDocument() = 0;\n"
                    "};\n"
                ),
            },
        },
        # 만들기 · 지우기 · 옮기기 동사는 하나씩 — 한 조각에 모으면 하나만 잡혀도 통과해 나머지가 죽은 것을 모른다.
        *(
            {
                "name": f"만들기 · 지우기 · 옮기기 동사 — {declaration.strip()}",
                "files": {"Source/Probe/ProbeFileOps.h": "struct ProbeFileOps\n{\n" + declaration + "};\n"},
            }
            for declaration in (
                "    static bool removeFile( string_view path );\n",
                "    static bool copyFile( string_view source, string_view destination );\n",
                "    bool createSurface( void* pWindowHandle );\n",
                "    static bool deleteAssetFile( string_view absolutePath );\n",
                "    bool moveProperty( hashed_string fromProp, hashed_string toProp ) const;\n",
                "    static bool rename( GameObject* pObj, const utf8* pNewName );\n",
            )
        ),
        {
            "name": "re + 동사 — recreate",
            "files": {"Source/Probe/ProbeSurface.h": "struct ProbeSurface\n{\n    virtual bool recreateSurface() = 0;\n};\n"},
        },
        {
            "name": ".cpp 의 번역 단위 지역 함수",
            "files": {
                "Source/Probe/ProbeLocal.cpp": (
                    "namespace\n{\n    struct ProbeLocalInternal\n    {\n"
                    "        static bool readHeader( const uint8* pData, size_t size );\n    };\n} // namespace\n"
                ),
            },
        },
        {
            "name": ".cpp 의 익명 네임스페이스 안 자유 함수(static 없이)",
            "files": {
                "Source/Probe/ProbeAnonymous.cpp": (
                    "namespace sw\n{\n    namespace\n    {\n        bool parseToken( string_view token )\n        {\n"
                    "            return token.empty() == false;\n        }\n    } // namespace\n} // namespace sw\n"
                ),
            },
        },
        {
            "name": "반환 타입과 이름을 두 줄로 쪼갠 선언",
            "files": {"Source/Probe/ProbeSplit.h": "struct ProbeSplit\n{\n    static bool\n    parseLine( string_view line );\n};\n"},
        },
        {
            "name": "주석을 사이에 둔 [[nodiscard]] 는 인정하지만, 주석 너머 다른 선언의 것은 아니다",
            "files": {
                "Source/Probe/ProbeAbove.h": (
                    "struct ProbeAbove\n{\n    [[nodiscard]] bool isReady() const;\n"
                    "    /** @brief 읽습니다. */\n    bool loadData();\n};\n"
                ),
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 린트 대상(시험 제외)의 .h · .cpp · .inl 전체)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        violations = findFallibleDeclarationsWithoutNodiscard(repositoryRoot, args.files)
        return GateResult(listViolation=violations, summary=f"{' · '.join(_kListScanRoot)} 의 .h · .cpp · .inl 실패 가능 bool 선언")


main = CheckFallibleNodiscardGate.run


if __name__ == "__main__":
    sys.exit(main())
