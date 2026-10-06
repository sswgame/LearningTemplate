#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""잘 알려진 상수(π 계열 · √2 · e · 중력 · 황금비 해시 · splitmix64 · FNV-1a)를 리터럴로 다시 적은 곳을 잡는다.

이 값들은 집이 한 곳씩 있다:

    π · 2π · π/2 · 도↔라디안 · √2 · 1/√2 · e   Source/Core/Math/MathUtil.h        (MathUtil::kPi · kTwoPi · kHalfPi · kPi64 …)
                                             Resource/engine/shaders/common.hlsli (셰이더: kPi · kTwoPi · kHalfPi · kInvPi)
    중력 9.81                                 Source/Engine/Common/EngineDefines.h (constant::kDefaultGravity — 물리 설정 표의 기본값)
                                             런타임 값은 PhysicsSystem::getConfiguredGravity(Magnitude) 하나(셰이더는 머티리얼 · 루트 상수로 받는다)
    황금비 · splitmix64 · FNV-1a              Source/Core/Common/HashUtil.h        (HashUtil::kGoldenRatio64 · mix64 · combine · kFnv*)

리터럴을 파일마다 다시 적으면 자릿수가 갈린다 — 실제로 `3.1415926535f` · `3.14159265358979f` · `6.2831853f` · `6.28318530718f` 가 섞여 있었고,
FNV 기저값 하나는 끝 자리가 빠진 채(`1469598103934665603`) 세 파일에 복사돼 있었다. float 로는 같은 값이어도 다음 사람은 어느 것이
정본인지 모른다.

예외: 리플렉션 메타데이터(`PROPERTY( ... Max = 6.2831853 )`)는 파서가 리터럴만 읽으므로 그 줄은 보지 않는다. 시험(`Test/`)은 기대값을
리터럴로 적는 곳이라 보지 않는다. 주석 · 문자열 안은 보지 않는다.

  python Scripts/lint/gate/CheckWellKnownConstants.py [--root <repo>] [--files a.cpp b.hlsl]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import NamedTuple

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import blankCommentsAndLiterals, normalizePath, readTextFiles  # noqa: E402
from LintGate import GateError, GateResult, LintGate  # noqa: E402

_kListScanRoot = ("Source", "Tools/ReflectionParser", "Resource/engine/shaders", "Resource/common/shaders")
_kSuffixes = (".h", ".hpp", ".inl", ".cpp", ".xxx", ".hlsl", ".hlsli")

#: 리플렉션 메타데이터 줄 — 파서가 리터럴만 읽는다.
_kMetadataLineRe = re.compile(r"\b(?:PROPERTY|FUNCTION|REFLECT|ENUM)\s*\(")


class WellKnownRule(NamedTuple):
    label: str
    pattern: re.Pattern
    replacement: str
    homes: tuple[str, ...]


_kMathHome = ("Source/Core/Math/MathUtil.h", "Resource/engine/shaders/common.hlsli")
_kHashHome = ("Source/Core/Common/HashUtil.h",)

_kListRule = (
    WellKnownRule("π 계열",
                  re.compile(r"(?<![\w.])(?:3\.14159\d*|6\.28318\d*|1\.570796\d*|0\.785398\d*|57\.2957\d*|0\.0174532\d*|0\.318309\d*|0\.159154\d*)"),
                  "MathUtil::kPi · kTwoPi · kHalfPi · kPi64 · kDegreeToRadian · kRadianToDegree (셰이더는 common.hlsli 의 kPi · kTwoPi · kHalfPi · kInvPi)",
                  _kMathHome),
    WellKnownRule("√2 · 1/√2", re.compile(r"(?<![\w.])(?:1\.41421\d*|0\.707106\d*)"), "MathUtil::kSqrt2 · kInvSqrt2", _kMathHome),
    WellKnownRule("e", re.compile(r"(?<![\w.])2\.71828\d*"), "MathUtil::kEuler (거듭제곱이면 MathUtil::exp)", _kMathHome),
    WellKnownRule("중력 9.81", re.compile(r"(?<![\w.])9\.81(?![\d])"),
                  "PhysicsSystem::getConfiguredGravity(Magnitude) — 설정 표 기본값이 꼭 필요하면 constant::kDefaultGravity",
                  ("Source/Engine/Common/EngineDefines.h",)),
    WellKnownRule("황금비 해시", re.compile(r"(?i)(?<![\w])0x9E3779B9(?:7F4A7C15)?(?![0-9a-f])"),
                  "HashUtil::kGoldenRatio32 · kGoldenRatio64 · combine", _kHashHome),
    WellKnownRule("splitmix64", re.compile(r"(?i)(?<![\w])0x(?:BF58476D1CE4E5B9|94D049BB133111EB)(?![0-9a-f])"),
                  "HashUtil::mix64 (곱수만 필요하면 kSplitMixMultiplier0 · 1)", _kHashHome),
    WellKnownRule("FNV-1a", re.compile(r"(?i)(?<![\w])(?:2166136261|16777619|14695981039346656037|1099511628211|0x811C9DC5|0x01000193|0xCBF29CE484222325|0x100000001B3)(?:ull|ul|u|l)?(?![\w])"),
                  "HashUtil::kFnvOffset32 · kFnvPrime32 · kFnvOffset64 · kFnvPrime64", _kHashHome),
    # 끝 자리가 빠진 FNV-1a 64 기저값 — 세 파일에 복사돼 있던 실제 오기다. 정본(14695981039346656037)이 아니다.
    WellKnownRule("FNV-1a 기저 오기", re.compile(r"(?<![\w])1469598103934665603(?:ull|ul|u|l)?(?![\w])"),
                  "HashUtil::kFnvOffset64 (이 수는 끝 자리가 빠진 오기다)", ()),
)


def findWellKnownLiterals(repositoryRoot: Path, listTargetFile: list[str] | None) -> list[str]:
    """집 밖에서 잘 알려진 상수를 리터럴로 쓴 줄을 위반 문자열로 돌려줍니다."""
    listPath = LintGate.selectTargetFiles(repositoryRoot, listTargetFile, listScanRoot=_kListScanRoot, suffixes=_kSuffixes)
    listViolation: list[str] = []
    for path, text in readTextFiles(listPath):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        listRule = [rule for rule in _kListRule if relative not in rule.homes and rule.pattern.search(text) is not None]
        if not listRule:
            continue
        listOriginalLine = text.splitlines()
        for lineIndex, line in enumerate(blankCommentsAndLiterals(text).splitlines(), start=1):
            if _kMetadataLineRe.search(line):
                continue
            for rule in listRule:
                match = rule.pattern.search(line)
                if match is not None:
                    listViolation.append(f"{relative}:{lineIndex}: {rule.label} '{match.group(0)}' -> {rule.replacement}  | {listOriginalLine[lineIndex - 1].strip()}")
    return listViolation


class CheckWellKnownConstantsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "잘 알려진 상수(π · √2 · e · 중력 · 해시 상수)를 집 밖에서 리터럴로 적었는지 검사"
    buildComment = "Checking that well-known constants are not re-spelled as literals..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    violationHeader = "잘 알려진 상수를 리터럴로 적음"
    hint = (
        "  π · √2 · e 는 MathUtil(셰이더는 common.hlsli), 중력은 constant::kDefaultGravity, 해시 상수는 HashUtil 에 있습니다.\n"
        "  리플렉션 메타데이터(PROPERTY 줄)와 Test/ 는 보지 않습니다."
    )
    selfTestCases = [
        {"name": "2π 를 자릿수만 바꿔 다시 적는다",
         "files": {"Source/Probe/ProbeAngle.cpp": "float probe( float t ) { return t * 6.2831853f; }\n"}},
        {"name": "셰이더에서 π 를 다시 적는다",
         "files": {"Resource/engine/shaders/probe.hlsl": "static const float kProbePi = 3.14159265f;\n"}},
        {"name": "황금비 해시 상수를 다시 적는다",
         "files": {"Source/Probe/ProbeHash.h": "#pragma once\ninline unsigned long long probe( unsigned long long h ) { return h * 0x9e3779b97f4a7c15ull; }\n"}},
        {"name": "FNV 소수를 다시 적는다",
         "files": {"Source/Probe/ProbeFnv.cpp": "unsigned probe( unsigned h, unsigned char c ) { return ( h ^ c ) * 16777619u; }\n"}},
        {"name": "끝 자리 빠진 FNV 기저값을 적는다",
         "files": {"Source/Probe/ProbeFnvBasis.cpp": "unsigned long long probe() { return 1469598103934665603ull; }\n"}},
        {"name": "중력을 다시 적는다(ReflectionParser)",
         "files": {"Tools/ReflectionParser/ProbeGravity.cpp": "const float kProbeGravity = 9.81f;\n"}},
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 Source · Tools/ReflectionParser · 셰이더 전체)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        # 집이 사라지면(옮기면) 예외도 함께 낡는다 — 조용히 넘기지 않는다. 시험용 가짜 저장소(자기 시험)는 집이 없어도 된다.
        if (repositoryRoot / "Source/Core").is_dir():
            for rule in _kListRule:
                for home in rule.homes:
                    if not (repositoryRoot / home).is_file():
                        raise GateError(f"{home} 가 없습니다 — '{rule.label}' 의 집입니다. 옮겼다면 이 게이트의 homes 를 고치십시오.")
        violations = findWellKnownLiterals(repositoryRoot, args.files)
        return GateResult(listViolation=violations, summary="Source · Tools/ReflectionParser · 셰이더의 잘 알려진 상수 리터럴")


main = CheckWellKnownConstantsGate.run


if __name__ == "__main__":
    sys.exit(main())
