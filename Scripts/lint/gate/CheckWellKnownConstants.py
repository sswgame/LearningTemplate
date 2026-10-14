#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""잘 알려진 상수(π 계열 · √2 · e · 중력 · 황금비 해시 · splitmix64 · FNV-1a)를 리터럴로 다시 적은 곳을 잡는다.

이 값들은 집이 한 곳씩 있다:

    π · 2π · π/2 · 도↔라디안 · √2 · 1/√2 · e   Source/Core/Math/MathUtil.h        (MathUtil::kPi · kTwoPi · kHalfPi · kPi64 …)
                                             Resource/engine/shaders/common.hlsli (셰이더: kPi · kTwoPi · kHalfPi · kInvPi)
    중력 9.81                                 Source/Engine/Common/EngineDefines.h (constant::kDefaultGravity — 물리 설정 표의 기본값)
                                             런타임 값은 PhysicsSystem::getConfiguredGravity(Magnitude) 하나(셰이더는 머티리얼 · 루트 상수로 받는다)
    황금비 · splitmix64 · FNV-1a              Source/Core/Common/HashUtil.h        (HashUtil::kGoldenRatio64 · mix64 · combine · kFnv*)
    시간 · 바이트 단위(1e9 · 1024*1024)       Source/Core/Common/Defines.h         (constant::kNanosecondsPerSecond · kBytesPerMegabyte …)

리터럴을 파일마다 다시 적으면 자릿수가 갈린다. 실제로 `3.1415926535f` · `3.14159265358979f` · `6.2831853f` · `6.28318530718f` 가 섞여 있었고,
FNV 기저값 하나는 끝 자리가 빠진 채(`1469598103934665603`) 세 파일에 복사돼 있었다. float 로는 같은 값이어도 다음 사람은 어느 것이
원본인지 모른다.

시간 · 바이트 단위는 막는 것을 좁게 잡았다(오탐이 적은 것만). 막는 것: 시간 맥락(sec · nano · time · tick · clock · deadline)이 있는 줄의
정수 `1'000'000'000` · `1000000000`, 곱하거나 나누는 `1e9`(센티널 `1.0e9f` 는 연산이 아니라 통과), 바이트 맥락(byte · size · capacity ·
budget · memory · buffer …)이 있는 줄의 `1024 * 1024` · `1'048'576`. 막지 않고 **참고로만 찍는 것**: 시간 맥락(초 · 밀리 · 나노 · 마감)이
있는 줄의 `* 1000` · `/ 1000`(km 같은 단위일 수 있다).

예외: 리플렉션 메타데이터(`PROPERTY( ... Max = 6.2831853 )`)는 파서가 리터럴만 읽으므로 그 줄은 보지 않는다. 테스트(`Test/`)은 기대값을
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

from common import blankCommentsAndLiterals, kLintProductRelDirs, normalizePath  # noqa: E402
from LintGate import GateError, GateResult, LintGate  # noqa: E402

# 테스트는 기대값을 리터럴로 적는 곳이라 뺀다(kLintProductRelDirs).
_kListScanRoot = kLintProductRelDirs + ("Resource/engine/shaders", "Resource/common/shaders")
#: 숫자 구분자(`1'000'000'000`) — 문자 리터럴로 읽히지 않게 검사 전에 지운다(줄 번호는 그대로다).
_kDigitSeparatorRe = re.compile(r"(?<=\d)'(?=[0-9A-Fa-f]{3})")
_kSuffixes = (".h", ".hpp", ".inl", ".cpp", ".xxx", ".hlsl", ".hlsli")

#: 리플렉션 메타데이터 줄 — 파서가 리터럴만 읽는다.
_kMetadataLineRe = re.compile(r"\b(?:PROPERTY|FUNCTION|REFLECT|ENUM)\s*\(")


class WellKnownRule(NamedTuple):
    label: str
    pattern: re.Pattern
    replacement: str
    homes: tuple[str, ...]
    context: re.Pattern | None = None   #: 줄에 이 맥락이 있을 때만 본다
    bNote: bool = False                 #: True 면 실패시키지 않고 참고로만 찍는다


_kMathHome = ("Source/Core/Math/MathUtil.h", "Resource/engine/shaders/common.hlsli")
_kHashHome = ("Source/Core/Common/HashUtil.h",)
_kUnitHome = ("Source/Core/Common/Defines.h",)

#: 시간 맥락 — 변수 · 함수 이름에 이런 낱말이 든 줄만 본다(`1.0e9f` 센티널과 거리 `* 1000` 을 거른다).
_kTimeContextRe = re.compile(r"(?i)second|nano|milli|micro|deadline|timeout|\bms\b|[a-z]ms\b")
_kNanoContextRe = re.compile(r"(?i)sec|nano|time|tick|clock|deadline")
#: 바이트 맥락 — 개수(`kMaxCandidateCount = 4u * 1024u * 1024u`)와 바이트를 가른다.
_kByteContextRe = re.compile(r"(?i)byte|size(?!_t)|capacity|budget|memor|\bmb\b|buffer|arena|chunk")

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
    # 끝 자리가 빠진 FNV-1a 64 기저값 — 세 파일에 복사돼 있던 실제 오기다. 원본(14695981039346656037)이 아니다.
    WellKnownRule("1초의 나노초(정수)", re.compile(r"(?<![\w.])1000000000(?i:ull|ll|ul|u|l)?(?![\w.'])"),
                  "constant::kNanosecondsPerSecond", _kUnitHome,
                  context=_kNanoContextRe),
    WellKnownRule("1초의 나노초(실수)", re.compile(r"(?:[*/]\s*1(?:\.0)?e9f?|(?<![\w.])1(?:\.0)?e9f?\s*[*/])(?![\w.])"),
                  "constant::kNanosecondsPerSecond (곱하거나 나누는 변환만 본다 — `1.0e9f` 센티널은 통과)", _kUnitHome),
    WellKnownRule("1 MiB", re.compile(r"(?<![\w.'])(?:1024(?:\.0f?|ull|ll|ul|u|l)?\s*\*\s*1024(?:\.0f?|ull|ll|ul|u|l)?|1048576)(?![\w.'])"),
                  "constant::kBytesPerMegabyte (KiB 는 constant::kBytesPerKilobyte)", _kUnitHome,
                  context=_kByteContextRe),
    WellKnownRule("초 ↔ 밀리초 · 마이크로초 · 나노초(참고)", re.compile(r"[*/]\s*1000(?:\.0f?|ll|l)?(?![\w.'])"),
                  "constant::kMillisecondsPerSecond · kMicrosecondsPerMillisecond · kNanosecondsPerMicrosecond (시간이 아니면 무시)", _kUnitHome,
                  context=_kTimeContextRe, bNote=True),
    WellKnownRule("FNV-1a 기저 오기", re.compile(r"(?<![\w])1469598103934665603(?:ull|ul|u|l)?(?![\w])"),
                  "HashUtil::kFnvOffset64 (이 수는 끝 자리가 빠진 오기다)", ()),
)


def findWellKnownLiterals(repositoryRoot: Path, listTargetFile: list[str] | None) -> tuple[list[str], list[str]]:
    """집 밖에서 잘 알려진 상수를 리터럴로 쓴 줄을 (위반, 참고) 문자열 목록으로 돌려줍니다."""
    listPath = LintGate.selectTargetFiles(repositoryRoot, listTargetFile, listScanRoot=_kListScanRoot, suffixes=_kSuffixes)
    listViolation: list[str] = []
    listNote: list[str] = []
    for path, text in LintGate.readFiles(listPath):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        listOriginalLine = text.splitlines()
        text = _kDigitSeparatorRe.sub('', text)
        listRule = [rule for rule in _kListRule if relative not in rule.homes and rule.pattern.search(text) is not None]
        if not listRule:
            continue
        for lineIndex, line in enumerate(blankCommentsAndLiterals(text).splitlines(), start=1):
            if _kMetadataLineRe.search(line):
                continue
            for rule in listRule:
                if rule.context is not None and rule.context.search(line) is None:
                    continue
                match = rule.pattern.search(line)
                if match is not None:
                    message = f"{relative}:{lineIndex}: {rule.label} '{match.group(0)}' -> {rule.replacement}  | {listOriginalLine[lineIndex - 1].strip()}"
                    (listNote if rule.bNote else listViolation).append(message)
    return listViolation, listNote


class CheckWellKnownConstantsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "잘 알려진 상수(π · √2 · e · 중력 · 해시 상수)를 집 밖에서 리터럴로 적었는지 검사"
    buildComment = "Checking that well-known constants are not re-spelled as literals..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    violationHeader = "잘 알려진 상수를 리터럴로 적음"
    hint = (
        "  π · √2 · e 는 MathUtil(셰이더는 common.hlsli), 중력은 constant::kDefaultGravity, 해시 상수는 HashUtil,\n"
        "  시간 · 바이트 단위(1e9 · 1024*1024)는 Core/Common/Defines.h 의 constant 에 있습니다.\n"
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
        {"name": "1초의 나노초를 정수로 다시 적는다",
         "files": {"Source/Probe/ProbeNano.cpp": "long long probe( long long seconds ) { return seconds * 1'000'000'000LL; }\n"}},
        {"name": "초를 나노초로 바꾸며 1e9 를 적는다",
         "files": {"Source/Probe/ProbeNanoFloat.cpp": "long long probe( double seconds ) { return static_cast<long long>( seconds * 1.0e9 ); }\n"}},
        {"name": "1 MiB 를 곱으로 적는다",
         "files": {"Source/Probe/ProbeMiB.h": "#pragma once\ninline constexpr unsigned long long kProbeBuffer = 4ull * 1024ull * 1024ull;\n"}},
        {"name": "중력을 다시 적는다(ReflectionParser)",
         "files": {"Tools/ReflectionParser/ProbeGravity.cpp": "const float kProbeGravity = 9.81f;\n"}},
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 시험을 뺀 린트 대상 · 셰이더 전체)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        # 집이 사라지면(옮기면) 예외도 함께 낡는다 — 조용히 넘기지 않는다. 테스트용 가짜 저장소(자기 테스트)는 집이 없어도 된다.
        if (repositoryRoot / "Source/Core").is_dir():
            for rule in _kListRule:
                for home in rule.homes:
                    if not (repositoryRoot / home).is_file():
                        raise GateError(f"{home} 가 없습니다 — '{rule.label}' 의 집입니다. 옮겼다면 이 게이트의 homes 를 고치십시오.")
        violations, notes = findWellKnownLiterals(repositoryRoot, args.files)
        return GateResult(listViolation=violations, listNote=notes, summary=f"{' · '.join(kLintProductRelDirs)} · 셰이더의 잘 알려진 상수 리터럴")


main = CheckWellKnownConstantsGate.run


if __name__ == "__main__":
    sys.exit(main())
