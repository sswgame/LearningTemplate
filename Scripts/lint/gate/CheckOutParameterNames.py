#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
out 매개변수 이름 검사 — 맨이름 `out` 을 막는다.

AGENTS.md "Naming › C++" 는 out 매개변수를 `out` + 채우는 것(`outBody` · `outListItem`, 포인터면 `pOutPath`)으로
쓰게 한다. 맨이름 `out` 은 접두사만 있고 무엇을 채우는지가 빠져, 호출부와 본문에서 같은 함수의 다른 out 과 구별되지 않는다.

**헤더 선언만 본다.** 참조 · 포인터 바로 뒤의 이름이 딱 `out` 이면 잡는다(`Body& out )`). 선언이 여러 줄이면
매개변수 줄 앞에서 마지막으로 연 `이름(` 을 함수 이름으로 쓴다. 규칙 전부터 있던 선언은 `mapExemption` 에 올라 있다 —
**새 위반만 막는다.**

  python Scripts/lint/gate/CheckOutParameterNames.py [--root <repo>] [--files <path>...]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import kLintTargetRelDirs, mapConcurrent  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kScanRoots = kLintTargetRelDirs
_kHeaderSuffix = (".h", ".hpp", ".inl")

# 참조 · 포인터 뒤에 이름이 딱 `out` 인 매개변수(기본값 `= x` 도 본다, `==` 는 식이라 뺀다).
_kBareOutRe = re.compile(r"[&*]\s*out\s*(?:[,)]|=(?!=))")
_kOpenCallRe = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
_kControlKeyword = frozenset({"if", "for", "while", "switch", "return", "sizeof", "decltype", "alignof", "noexcept",
                              "static_cast", "reinterpret_cast", "const_cast", "dynamic_cast"})


def scanFileInternal(filePath: Path, repositoryRoot: Path) -> list[str]:
    relativePath = filePath.relative_to(repositoryRoot).as_posix()
    try:
        text = filePath.read_text(encoding="utf-8", errors="replace")
    except OSError as exception:
        return [f"{relativePath}: 읽기 실패: {exception}"]

    # 주석의 줄바꿈은 남긴다 — 보고하는 줄 번호가 원문과 맞아야 한다.
    text = re.sub(r"/\*.*?\*/", lambda comment: "\n" * comment.group(0).count("\n"), text, flags=re.S)
    violations: list[str] = []
    lastOpenName = ""
    for lineIndex, rawLine in enumerate(text.split("\n"), 1):
        line = re.sub(r"//.*$", "", rawLine)
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        bareOut = _kBareOutRe.search(line)
        for openMatch in _kOpenCallRe.finditer(line):
            if bareOut is not None and openMatch.start() > bareOut.start():
                break
            if openMatch.group(1) not in _kControlKeyword:
                lastOpenName = openMatch.group(1)
        if bareOut is None:
            continue
        key = f"{filePath.stem}::{lastOpenName}"
        if key in CheckOutParameterNamesGate.mapExemption:
            CheckOutParameterNamesGate.useExemption(key)
            continue
        violations.append(
            f"{relativePath}:{lineIndex}: [BareOut] '{lastOpenName}' — 맨이름 'out' 매개변수입니다."
            f" 채우는 것을 이름에 담아 'outXxx' (포인터면 'pOutXxx') 로 씁니다."
        )
    return violations


class CheckOutParameterNamesGate(LintGate):
    description = "out 매개변수 이름 검사"
    buildComment = "Checking that output parameters name what they fill (no bare 'out')..."
    timeoutSeconds = 30
    preCommitPattern = ("*.h", "*.hpp", "*.inl")
    preCommitFileArgument = "--files"
    violationHeader = "out 매개변수 이름 위반"
    hint = "\n규칙은 AGENTS.md 의 'Naming › C++' 절(Output parameters)에 있습니다."
    # 규칙 전부터 있던 선언 — 키는 `헤더 파일 이름(확장자 없음)::함수 이름`, 이유의 "개명 예정 X" 가 바꿀 매개변수 이름이다.
    mapExemption = {
        "MaterialUtil::parsePermutationNode": "개명 예정 outDesc — 규칙 전부터 있던 선언",
        "ReflectAny::tryGet": "개명 예정 outValue — 규칙 전부터 있던 선언",
        "ReflectAny::tryGetFrom": "개명 예정 outValue — 규칙 전부터 있던 선언",
        "ReflectionRPC::packCall": "개명 예정 outEnvelope — 규칙 전부터 있던 선언",
        "PhysicsWorld::tryGetBody": "개명 예정 outBody — 규칙 전부터 있던 선언",
        "ReflectionEnumNames::tryParseContainerKind": "개명 예정 outKind — 규칙 전부터 있던 선언",
        "ReflectionEnumNames::tryParseFunctionNetRole": "개명 예정 outRole — 규칙 전부터 있던 선언",
        "ActionRoom::updateActors": "개명 예정 outResult — 규칙 전부터 있던 선언",
        "ActionRoom::resolvePlayerHits": "개명 예정 outResult — 규칙 전부터 있던 선언",
        "ActionRoom::refreshCleared": "개명 예정 outResult — 규칙 전부터 있던 선언",
        "LiveReloadManager::prepareShadowCopy": "개명 예정 outShadow — 규칙 전부터 있던 선언",
        "AnnotationMeta::tryParseAnnotationKind": "개명 예정 outKind — 규칙 전부터 있던 선언",
        "CodeEmit::CodeEmit": "개명 예정 outBuffer — 규칙 전부터 있던 선언",
        "CodeGenerator::emitFileHeader": "개명 예정 outBuffer — 규칙 전부터 있던 선언",
        "CodeGenerator::emitTypeRegistrar": "개명 예정 outBuffer — 규칙 전부터 있던 선언",
        "CodeGenerator::emitReflectTypeTraits": "개명 예정 outBuffer — 규칙 전부터 있던 선언",
        "CodeGenerator::emitTypeInfoAccessors": "개명 예정 outBuffer — 규칙 전부터 있던 선언",
        "CodeGenerator::emitEnumRegistrar": "개명 예정 outBuffer — 규칙 전부터 있던 선언",
        "CodeGenerator::appendTemplate": "개명 예정 outBuffer — 규칙 전부터 있던 선언",
    }
    selfTestCases = [
        {
            "name": "맨이름 out 참조 매개변수",
            "files": {"Source/Engine/Probe.h": "namespace sw\n{\n    struct Probe\n    {\n        bool tryGetBody( int32 handle, Body& out ) const;\n    };\n}\n"},
        },
        {
            "name": "여러 줄 선언의 맨이름 out 포인터 매개변수",
            "files": {"Source/Engine/Probe.h": "namespace sw\n{\n    struct Probe\n    {\n        void parse( int32 a,\n                    Desc* out = nullptr );\n    };\n}\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--files", nargs="*", default=None, help="이 파일들만 검사 (pre-commit 용)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        headers = [] if args.files == [] else self.selectTargetFiles(repositoryRoot, args.files, listScanRoot=_kScanRoots,
                                                                     suffixes=_kHeaderSuffix)
        if not headers:
            return GateResult(summary="검사할 헤더가 없습니다")

        violations: list[str] = []
        for fileViolations in mapConcurrent(lambda path: scanFileInternal(path, repositoryRoot), headers):
            violations.extend(fileViolations)
        return GateResult(listViolation=violations, summary=f"{len(headers)} headers scanned")


main = CheckOutParameterNamesGate.run


if __name__ == "__main__":
    sys.exit(main())
