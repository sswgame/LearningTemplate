#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""실패를 bool 로 알리는 함수 선언에 `[[nodiscard]]` 가 있는지 본다 — 결과를 버리는 호출을 컴파일러가 짚게.

이 저장소의 결함 가운데 가장 흔한 모양 하나는 **실패가 조용한 것**이었다. 읽기 · 쓰기 · 적용이 false 를 돌려줬는데 부른 쪽이
버리고 성공한 것처럼 갔다 — 되돌리기가 JSON 프리팹을 XML 로 읽다 실패해 인스턴스가 비었고(결함 57), 셰이더 컴파일 실패가 최신 굽기로
도장 찍혀 배포본에 옛 바이너리가 실렸고, 깨진 언어 파일을 빈 칸으로 다시 써 번역이 지워졌다. 2026-10-02 에 세어 보니 실패할 수 있는
동사의 bool 함수 362 개 가운데 `[[nodiscard]]` 가 붙은 것이 0 이었고, 결과를 버리는 호출이 182 곳이었다.

규칙: 이름이 아래 동사로 시작하고 bool 을 돌려주는 선언은 `[[nodiscard]]` 를 단다. 컴파일러는 `-Werror=unused-result`
(cmake/Modules/Compiler/Clang.cmake · GCC.cmake)로 버리는 호출에서 빌드를 세운다. 일부러 버릴 때는 `(void)호출();` 과 이유 한 줄.

  load · save · read · write · parse · deserialize · serialize · apply · restore · import · export · cook · compile · bake
  revert · convert · try · open · attach · spawn · instantiate · reload

`friend` 선언(정의가 아니면 속성을 달 수 없다)과 C-ABI 계약(`Source/RuntimeAPI`)은 보지 않는다. 한 줄에 반환 타입과 이름이 같이 있는
선언만 본다 — 여러 줄로 쪼갠 선언은 놓치는 대신 오탐이 없다.

  python Scripts/lint/gate/CheckFallibleNodiscard.py [--root <repo>] [--files a.h b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import normalizePath  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kListFallibleVerb = (
    "load", "save", "read", "write", "parse", "deserialize", "serialize", "apply", "restore", "import", "export",
    "cook", "compile", "bake", "revert", "convert", "try", "open", "attach", "spawn", "instantiate", "reload",
)

# 줄 머리의 지정자(static · virtual · 내보내기 매크로) 다음 `bool 이름(` — 이름이 실패할 수 있는 동사로 시작하는 선언.
_kDeclarationRe = re.compile(
    r"^\s*(?:(?:static|virtual|inline|constexpr|SW_API|SW_GF_API|SW_MODULE_API)\s+)*"
    r"bool\s+(?P<name>(?:" + "|".join(_kListFallibleVerb) + r")(?:[A-Z0-9]\w*)?)\s*\("
)

_kListScanRoot = ("Source",)
_kListSkippedFolder = ("Source/RuntimeAPI/",)


def findFallibleDeclarationsWithoutNodiscard(repositoryRoot: Path, listTargetFile: list[str] | None) -> list[str]:
    """`[[nodiscard]]` 가 없는 실패 가능 bool 선언을 모아 위반 문자열로 돌려줍니다."""
    violations: list[str] = []
    for path in LintGate.selectTargetFiles(repositoryRoot, listTargetFile, listScanRoot=_kListScanRoot, suffixes=(".h",)):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        if relative.startswith(_kListSkippedFolder):
            continue
        try:
            listLine = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue

        for lineIndex, line in enumerate(listLine):
            match = _kDeclarationRe.match(line)
            if match is None or "[[nodiscard]]" in line:
                continue
            previous = listLine[lineIndex - 1].strip() if lineIndex > 0 else ""
            if previous.endswith("[[nodiscard]]"):
                continue
            violations.append(f"[Fallible Nodiscard] {relative}:{lineIndex + 1}: {match.group('name')} — {line.strip()}")
    return violations


class CheckFallibleNodiscardGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "실패 가능 bool 함수의 [[nodiscard]] 검사"
    buildComment = "Checking fallible bool declarations for [[nodiscard]]..."
    timeoutSeconds = 30
    preCommitPattern = ("Source/*.h",)
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
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 헤더 (생략 시 Source 전체)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        violations = findFallibleDeclarationsWithoutNodiscard(repositoryRoot, args.files)
        return GateResult(listViolation=violations, summary="Source 의 실패 가능 bool 선언")


main = CheckFallibleNodiscardGate.run


if __name__ == "__main__":
    sys.exit(main())
